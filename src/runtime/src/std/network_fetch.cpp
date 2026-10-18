// =============================================================================
// src/runtime/src/std/network_fetch.cpp - `std/network` HTTP client (`fetch`).
//
// OWNERSHIP: the network wave. Same selection rule as network.cpp (the
// std/network native package row, D68): compiled + linked only for a program
// that imports "std/network".
// One request, one TCP connection, `Connection: close`: beta-one fetch is a
// plain HTTP/1.1 client - no connection reuse, no redirects, no TLS, no request
// headers option (all MISSING in the wave report); the body is fully buffered
// (Content-Length, chunked and close-delimited), so `res.text()` /
// `res.json<T>()` never suspend. A FetchOp stays alive through shared_ptr
// holders in `handle->data` (tcp, timer, DNS request, in-flight write) and
// counts as pending work until it settles.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <uv.h>

#include "network_loop.hpp"

namespace cp3::runtime::network {
namespace {

// =============================================================================
// URL parsing
// =============================================================================
struct UrlParts {
    std::string host;
    int port = 80;
    std::string target = "/";  // path + query, exactly as sent on the request line
};

bool parsePort(const std::string& text, int& port) {
    if (text.empty() || text.size() > 5) return false;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value <= 0 || value > 65535) return false;
    port = value;
    return true;
}

/// Parses `http://host[:port][/path]`; empty result string = success, else the
/// rejection message (https:// gets its own, TLS-off message).
std::string parseHttpUrl(const std::string& url, UrlParts& out) {
    if (url.rfind("https://", 0) == 0) {
        return "std/network: https:// is not supported in this build (TLS is off)";
    }
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) {
        return "std/network: fetch() needs an http:// URL, got: " + url;
    }
    const std::string rest = url.substr(prefix.size());
    const std::size_t slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    if (slash != std::string::npos) {
        out.target = rest.substr(slash);
    }
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        out.host = authority;
    } else {
        out.host = authority.substr(0, colon);
        if (!parsePort(authority.substr(colon + 1), out.port)) {
            return "std/network: bad port in URL: " + url;
        }
    }
    if (out.host.empty()) {
        return "std/network: missing host in URL: " + url;
    }
    return {};
}

// =============================================================================
// One in-flight request
// =============================================================================
enum class BodyState { Head, SizeLine, Chunk, AfterChunk, Trailers, Length, Close };

struct FetchOp {
    Promise<FetchResponse> promise;  // pending until settle below
    UrlParts url;
    FetchInit init;
    std::string authority;  // host:port, for Host: and diagnostics

    uv_tcp_t tcp{};
    uv_connect_t connect{};
    uv_getaddrinfo_t dns{};
    uv_timer_t timeout{};
    bool tcpReady = false;
    bool timerReady = false;
    bool done = false;

    std::string raw;         // every byte received so far
    std::size_t cursor = 0;  // parse cursor into `raw`
    double statusCode = 0.0;
    std::string statusText;
    long long contentLength = -1;
    BodyState bodyState = BodyState::Head;
    long long chunkLeft = 0;
    std::string decoded;  // the decoded body
};

void attachOp(uv_handle_t* handle, const std::shared_ptr<FetchOp>& op) {
    handle->data = new std::shared_ptr<FetchOp>(op);
}

/// Closes a handle and drops the reference it held; the op dies with its last
/// reference (in-flight DNS requests and writes keep it alive). The holder is
/// released LAST: dropping it can free the op inside libuv's close callback.
void closeOpHandle(uv_handle_t* handle) {
    if (uv_is_closing(handle)) return;
    uv_close(handle, [](uv_handle_t* closing) {
        auto* holder = static_cast<std::shared_ptr<FetchOp>*>(closing->data);
        closing->data = nullptr;
        delete holder;
    });
}

std::shared_ptr<FetchOp> opOf(const uv_handle_t* handle) {
    auto* holder = static_cast<std::shared_ptr<FetchOp>*>(handle->data);
    return holder == nullptr ? nullptr : *holder;
}

void failOp(const std::shared_ptr<FetchOp>& op, const std::string& message);
void succeedOp(const std::shared_ptr<FetchOp>& op);

void releaseOp(const std::shared_ptr<FetchOp>& op) {
    releaseNetwork();
    if (op->timerReady) {
        uv_timer_stop(&op->timeout);
        closeOpHandle(reinterpret_cast<uv_handle_t*>(&op->timeout));
    }
    if (op->tcpReady) {
        closeOpHandle(reinterpret_cast<uv_handle_t*>(&op->tcp));
    }
}

void succeedOp(const std::shared_ptr<FetchOp>& op) {
    if (op->done) return;
    op->done = true;
    op->promise.fulfill(FetchResponse(op->statusCode, op->statusText, std::move(op->decoded)));
    releaseOp(op);
}

void failOp(const std::shared_ptr<FetchOp>& op, const std::string& message) {
    if (op->done) return;
    op->done = true;
    op->promise.rejectWith(Thrown::make(message));
    releaseOp(op);
}

// =============================================================================
// Request head and response parsing
// =============================================================================
std::string buildRequest(const FetchOp& op) {
    std::string request = op.init.method.empty() ? "GET" : op.init.method;
    request += " " + op.url.target + " HTTP/1.1\r\nHost: " + op.authority +
               "\r\nAccept: */*\r\nConnection: close\r\n";
    if (op.init.hasBody) {
        request += "Content-Length: " + std::to_string(op.init.body.size()) + "\r\n";
    }
    request += "\r\n";
    if (op.init.hasBody) {
        request += op.init.body;
    }
    return request;
}

std::string lowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string trimAscii(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

bool parseStatusCode(const std::string& text, double& code) {
    if (text.size() != 3) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    code = static_cast<double>((text[0] - '0') * 100 + (text[1] - '0') * 10 + (text[2] - '0'));
    return true;
}

bool parseContentLength(const std::string& text, long long& length) {
    if (text.empty() || text.size() > 18) return false;
    long long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    length = value;
    return true;
}

/// Parses the status line and headers (headEnd = offset of the "\r\n\r\n" that
/// closes the head). False means "malformed".
bool parseHead(FetchOp& op, std::size_t headEnd) {
    const std::string& text = op.raw;
    const std::size_t firstEnd = text.find("\r\n");
    if (firstEnd == std::string::npos || firstEnd < 12) return false;
    if (!parseStatusCode(text.substr(9, 3), op.statusCode)) return false;
    op.statusText = firstEnd > 13 ? text.substr(13, firstEnd - 13) : std::string();
    op.contentLength = -1;
    bool chunked = false;
    for (std::size_t pos = firstEnd + 2; pos < headEnd;) {
        const std::size_t end = text.find("\r\n", pos);
        if (end == std::string::npos || end > headEnd) return false;
        const std::string line = text.substr(pos, end - pos);
        pos = end + 2;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = lowerAscii(line.substr(0, colon));
        const std::string value = trimAscii(line.substr(colon + 1));
        if (key == "content-length") {
            if (!parseContentLength(value, op.contentLength)) return false;
        } else if (key == "transfer-encoding" &&
                   lowerAscii(value).find("chunked") != std::string::npos) {
            chunked = true;
        }
    }
    op.cursor = headEnd + 4;
    op.bodyState = chunked ? BodyState::SizeLine
                           : (op.contentLength >= 0 ? BodyState::Length : BodyState::Close);
    return true;
}

bool parseChunkSize(const std::string& text, long long& size) {
    if (text.empty() || text.size() > 15) return false;
    long long value = 0;
    for (char c : text) {
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        value = value * 16 + digit;
    }
    size = value;
    return true;
}

void parseBody(const std::shared_ptr<FetchOp>& op) {
    while (!op->done) {
        const std::string& text = op->raw;
        const std::size_t available = text.size() - op->cursor;
        if (op->bodyState == BodyState::SizeLine) {
            const std::size_t end = text.find("\r\n", op->cursor);
            if (end == std::string::npos) return;
            const std::size_t semi = text.find(';', op->cursor);
            const std::size_t hexEnd = semi != std::string::npos && semi < end ? semi : end;
            long long size = 0;
            if (!parseChunkSize(trimAscii(text.substr(op->cursor, hexEnd - op->cursor)), size)) {
                failOp(op, "std/network: malformed chunked response");
                return;
            }
            op->cursor = end + 2;
            op->chunkLeft = size;
            op->bodyState = size == 0 ? BodyState::Trailers : BodyState::Chunk;
            continue;
        }
        if (op->bodyState == BodyState::Chunk) {
            const std::size_t take = static_cast<std::size_t>(
                available < static_cast<std::size_t>(op->chunkLeft) ? available : op->chunkLeft);
            op->decoded.append(text, op->cursor, take);
            op->cursor += take;
            op->chunkLeft -= static_cast<long long>(take);
            if (op->chunkLeft > 0) return;
            op->bodyState = BodyState::AfterChunk;
            continue;
        }
        if (op->bodyState == BodyState::AfterChunk) {
            if (available < 2) return;
            op->cursor += 2;
            op->bodyState = BodyState::SizeLine;
            continue;
        }
        if (op->bodyState == BodyState::Trailers) {
            const std::size_t end = text.find("\r\n", op->cursor);
            if (end == std::string::npos) return;
            const bool empty = end == op->cursor;
            op->cursor = end + 2;
            if (empty) {
                succeedOp(op);
                return;
            }
            continue;
        }
        if (op->bodyState == BodyState::Length) {
            const std::size_t need = static_cast<std::size_t>(
                op->contentLength - static_cast<long long>(op->decoded.size()));
            const std::size_t take = available < need ? available : need;
            op->decoded.append(text, op->cursor, take);
            op->cursor += take;
            if (static_cast<long long>(op->decoded.size()) >= op->contentLength) {
                succeedOp(op);
            }
            return;
        }
        op->decoded.append(text, op->cursor, available);  // close-delimited
        op->cursor = text.size();
        return;
    }
}

void consume(const std::shared_ptr<FetchOp>& op) {
    if (op->done) return;
    if (op->bodyState == BodyState::Head) {
        const std::size_t headEnd = op->raw.find("\r\n\r\n");
        if (headEnd == std::string::npos) {
            if (op->raw.size() > 65536) {
                failOp(op, "std/network: response head too large");
            }
            return;
        }
        if (!parseHead(*op, headEnd)) {
            failOp(op, "std/network: malformed response head");
            return;
        }
    }
    parseBody(op);
}

void finishOnEof(const std::shared_ptr<FetchOp>& op) {
    if (op->bodyState == BodyState::Head) {
        failOp(op, "std/network: connection closed before the response head");
        return;
    }
    if (op->bodyState == BodyState::Close) {
        succeedOp(op);
        return;
    }
    failOp(op, "std/network: response body truncated");
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

}  // namespace

Promise<FetchResponse> fetch(const std::string& url, const FetchInit& init) {
    UrlParts parts;
    const std::string error = parseHttpUrl(url, parts);
    if (!error.empty()) {
        return Promise<FetchResponse>::reject(Thrown::make(error));
    }
    ensureLoop();
    const std::shared_ptr<FetchOp> op = beginFetch(parts, init);
    return op->promise;
}

}  // namespace cp3::runtime::network
