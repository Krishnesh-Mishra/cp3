// =============================================================================
// src/std/network/runtime_fetch_io.cpp - `std/network` fetch transport.
//
// OWNERSHIP: the std/network family (src/std/network/**). Same selection rule as
// runtime.cpp (the std/network native package row, D68): compiled + linked only
// for a program that imports "std/network".
//
// The libuv side of the HTTP client: the shared_ptr holder behind
// `handle->data`, the connect / DNS / timeout / write callbacks and `beginFetch`
// (tcp + optional timeout + IPv4/DNS connect). The URL parsing, request head
// and response readers live in runtime_fetch.cpp; both files share FetchOp
// through runtime_fetch.hpp.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <uv.h>

#include "runtime_fetch.hpp"
#include "runtime_loop.hpp"

namespace cp3::runtime::network::detail {
namespace {

void attachOp(uv_handle_t* handle, const std::shared_ptr<FetchOp>& op) {
    handle->data = new std::shared_ptr<FetchOp>(op);
}

std::shared_ptr<FetchOp> opOf(const uv_handle_t* handle) {
    auto* holder = static_cast<std::shared_ptr<FetchOp>*>(handle->data);
    return holder == nullptr ? nullptr : *holder;
}

// =============================================================================
// libuv callbacks
// =============================================================================
void onAlloc(uv_handle_t*, std::size_t suggested, uv_buf_t* buf) {
    const std::size_t size = suggested == 0 ? 65536 : suggested;
    buf->base = new char[size];
    buf->len = size;
}

void onRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    const std::shared_ptr<FetchOp> op = opOf(reinterpret_cast<uv_handle_t*>(stream));
    if (op != nullptr && nread > 0) {
        op->raw.append(buf->base, static_cast<std::size_t>(nread));
    }
    if (buf->base != nullptr) delete[] buf->base;
    if (op == nullptr || op->done || nread == 0) return;
    if (nread < 0) {
        if (nread == UV_EOF) {
            finishOnEof(op);
        } else {
            failOp(op, "std/network: connection error while reading the response");
        }
        return;
    }
    consume(op);
}

struct WriteRequest {
    uv_write_t req{};
    std::string data;
    std::shared_ptr<FetchOp> op;
};

void onWriteDone(uv_write_t* req, int status) {
    auto* write = reinterpret_cast<WriteRequest*>(req);
    const std::shared_ptr<FetchOp> op = std::move(write->op);
    delete write;
    if (status < 0) {
        failOp(op, "std/network: could not send the request");
    }
}

void onTimeout(uv_timer_t* timer) {
    const std::shared_ptr<FetchOp> op = opOf(reinterpret_cast<uv_handle_t*>(timer));
    if (op != nullptr) {
        failOp(op, "std/network: request timed out after " +
                       std::to_string(static_cast<long long>(op->init.timeoutMs)) + " ms");
    }
}

void onConnected(uv_connect_t* req, int status);

int connectTo(const std::shared_ptr<FetchOp>& op, const struct sockaddr* addr) {
    return uv_tcp_connect(&op->connect, &op->tcp, addr, onConnected);
}

void onConnected(uv_connect_t* req, int status) {
    const std::shared_ptr<FetchOp> op = opOf(reinterpret_cast<uv_handle_t*>(req->handle));
    if (op == nullptr || op->done) return;
    if (status < 0) {
        failOp(op, "std/network: could not connect to " + op->authority);
        return;
    }
    auto* write = new WriteRequest;
    write->op = op;
    write->data = buildRequest(*op);
    uv_buf_t buf = uv_buf_init(write->data.data(), static_cast<unsigned int>(write->data.size()));
    if (uv_write(&write->req, req->handle, &buf, 1, onWriteDone) != 0) {
        delete write;
        failOp(op, "std/network: could not send the request");
        return;
    }
    uv_read_start(req->handle, onAlloc, onRead);
}

void onResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    std::unique_ptr<std::shared_ptr<FetchOp>> holder(
        static_cast<std::shared_ptr<FetchOp>*>(req->data));
    const std::shared_ptr<FetchOp> op = *holder;
    if (status != 0 || res == nullptr) {
        if (res != nullptr) uv_freeaddrinfo(res);
        failOp(op, "std/network: DNS lookup failed for " + op->url.host);
        return;
    }
    const int rc = connectTo(op, res->ai_addr);
    uv_freeaddrinfo(res);
    if (rc != 0) {
        failOp(op, "std/network: could not start the connection");
    }
}

}  // namespace

// =============================================================================
// Entry point
// =============================================================================
std::shared_ptr<FetchOp> beginFetch(const UrlParts& url, const FetchInit& init) {
    auto op = std::make_shared<FetchOp>();
    op->url = url;
    op->init = init;
    op->authority = url.host + ":" + std::to_string(url.port);
    retainNetwork();
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) {
        failOp(op, "std/network: could not create the libuv loop");
        return op;
    }
    uv_tcp_init(loop, &op->tcp);
    attachOp(reinterpret_cast<uv_handle_t*>(&op->tcp), op);
    op->tcpReady = true;
    if (init.timeoutMs > 0) {
        uv_timer_init(loop, &op->timeout);
        attachOp(reinterpret_cast<uv_handle_t*>(&op->timeout), op);
        op->timerReady = true;
        uv_timer_start(&op->timeout, onTimeout, static_cast<std::uint64_t>(init.timeoutMs), 0);
    }
    struct sockaddr_in addr{};
    if (uv_ip4_addr(url.host.c_str(), url.port, &addr) == 0) {
        const int rc = connectTo(op, reinterpret_cast<const struct sockaddr*>(&addr));
        if (rc != 0) failOp(op, "std/network: could not start the connection");
        return op;
    }
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    op->dns.data = new std::shared_ptr<FetchOp>(op);
    const std::string port = std::to_string(url.port);
    const int rc =
        uv_getaddrinfo(loop, &op->dns, onResolved, url.host.c_str(), port.c_str(), &hints);
    if (rc != 0) {
        delete static_cast<std::shared_ptr<FetchOp>*>(op->dns.data);
        op->dns.data = nullptr;
        failOp(op, "std/network: DNS lookup could not start");
    }
    return op;
}

}  // namespace cp3::runtime::network::detail
