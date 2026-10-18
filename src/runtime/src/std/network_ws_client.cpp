// =============================================================================
// src/runtime/src/std/network_ws_client.cpp - `std/network` WebSocket client.
//
// OWNERSHIP: the network wave. Same selection rule as network.cpp (the
// std/network native package row, D68).
//
// Hand-rolled RFC 6455 client on libuv (uWS v20 ships only a stub ClientApp):
// TCP connect (+ uv_getaddrinfo for a host name), the HTTP/1.1 upgrade
// handshake with a `Sec-WebSocket-Accept` check (SHA-1 + base64), masked
// client frames, fragmentation reassembly, ping/pong and the close handshake.
// One socket, one request at a time; the runtime loop counts the client as
// pending work from `wsClientStart` until the socket is done.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <uv.h>

#include "network_loop.hpp"
#include "network_ws.hpp"

namespace cp3::runtime::network::detail {

/// The client transport state. At namespace scope on purpose: WsImpl::client is
/// a shared_ptr to it, so the implementation in the anonymous namespace below
/// and the entry points at the bottom share one definition.
struct WsClient {
    std::shared_ptr<WsImpl> impl;
    uv_tcp_t tcp{};
    uv_connect_t connect{};
    uv_getaddrinfo_t dns{};
    bool tcpReady = false;
    bool finished = false;
    bool closing = false;  // a close frame was queued; retire once it is out
    double closeCode = 1000.0;
    std::string closeReason;
    std::string host;
    int port = 80;
    std::string target;
    std::string key;
    std::string buffer;  // received bytes
    std::string outbox;  // bytes queued for the socket
    bool writeActive = false;
    std::uint8_t fragmentOpcode = 0;
    std::string fragment;
};

namespace {

// =============================================================================
// URL parsing (network_ws.hpp)
// =============================================================================
std::string parsePortText(const std::string& text, int& port) {
    if (text.empty() || text.size() > 5) return "bad port";
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return "bad port";
        value = value * 10 + (c - '0');
    }
    if (value <= 0 || value > 65535) return "bad port";
    port = value;
    return {};
}

// =============================================================================
// The client transport helpers
// =============================================================================
void attachClient(uv_handle_t* handle, const std::shared_ptr<WsClient>& client) {
    handle->data = new std::shared_ptr<WsClient>(client);
}

std::shared_ptr<WsClient> clientOf(const uv_handle_t* handle) {
    auto* holder = static_cast<std::shared_ptr<WsClient>*>(handle->data);
    return holder == nullptr ? nullptr : *holder;
}

void detachClient(uv_handle_t* handle) {
    // Release the holder LAST: dropping it can free the client (and this
    // handle) while libuv is still inside the close callback.
    auto* holder = static_cast<std::shared_ptr<WsClient>*>(handle->data);
    handle->data = nullptr;
    delete holder;
}

void closeClientSocket(const std::shared_ptr<WsClient>& client) {
    if (!client->tcpReady) return;
    uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&client->tcp);
    if (uv_is_closing(handle)) return;
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&client->tcp));
    uv_close(handle, [](uv_handle_t* closing) { detachClient(closing); });
}

/// Retires the client once: releases the runtime loop and closes the socket.
void finishClient(const std::shared_ptr<WsClient>& client) {
    if (client->finished) return;
    client->finished = true;
    releaseNetwork();
    closeClientSocket(client);
}

void closeClient(const std::shared_ptr<WsClient>& client, double code, const std::string& reason) {
    if (client->finished) return;
    finishClient(client);
    client->impl->emitClose(code, reason);
}

void failClient(const std::shared_ptr<WsClient>& client, const std::string& message) {
    if (client->finished) return;
    if (!client->impl->closed) {
        client->impl->emitError(message);
    }
    const bool alreadyClosed = client->impl->closed;
    finishClient(client);
    if (!alreadyClosed) {
        client->impl->emitClose(1006.0, "");
    }
}

void flushWrites(const std::shared_ptr<WsClient>& client);

struct WriteRequest {
    uv_write_t req{};
    std::string data;
    std::shared_ptr<WsClient> client;
};

void onWriteDone(uv_write_t* req, int status) {
    auto* write = reinterpret_cast<WriteRequest*>(req);
    const std::shared_ptr<WsClient> client = std::move(write->client);
    delete write;
    if (client->finished) return;
    client->writeActive = false;
    if (status < 0) {
        failClient(client, "std/network: could not write to the WebSocket");
        return;
    }
    if (client->closing && client->outbox.empty()) {
        const double code = client->closeCode;
        const std::string reason = client->closeReason;
        closeClient(client, code, reason);
        return;
    }
    flushWrites(client);
}

void flushWrites(const std::shared_ptr<WsClient>& client) {
    if (client->finished || client->writeActive || client->outbox.empty()) return;
    auto* write = new WriteRequest;
    write->client = client;
    write->data = std::move(client->outbox);
    client->outbox.clear();
    client->writeActive = true;
    uv_buf_t buf = uv_buf_init(write->data.data(), static_cast<unsigned int>(write->data.size()));
    uv_stream_t* stream = reinterpret_cast<uv_stream_t*>(&client->tcp);
    if (uv_write(&write->req, stream, &buf, 1, onWriteDone) != 0) {
        delete write;
        client->writeActive = false;
        failClient(client, "std/network: could not write to the WebSocket");
    }
}

/// Queues an outgoing close frame; the socket retires once it is flushed.
void queueClose(const std::shared_ptr<WsClient>& client, double code, const std::string& reason) {
    if (client->closing) return;
    client->closing = true;
    client->closeCode = code;
    client->closeReason = reason;
    std::string payload(2, '\0');
    const int codeInt = static_cast<int>(code);
    payload[0] = static_cast<char>((codeInt >> 8) & 0xFF);
    payload[1] = static_cast<char>(codeInt & 0xFF);
    payload += reason;
    client->outbox += wsMakeFrame(0x8, payload);
    flushWrites(client);
}

// =============================================================================
// Handshake
// =============================================================================
void tryFinishHandshake(const std::shared_ptr<WsClient>& client) {
    const std::size_t end = client->buffer.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (client->buffer.size() > 16384) {
            failClient(client, "std/network: WebSocket handshake response too large");
        }
        return;
    }
    const std::string head = client->buffer.substr(0, end);
    client->buffer.erase(0, end + 4);
    if (head.rfind("HTTP/1.1 101", 0) != 0 && head.rfind("HTTP/1.1 101 ", 0) != 0) {
        const std::size_t space = head.find(' ');
        const std::string code =
            space == std::string::npos ? std::string() : head.substr(space + 1, 3);
        failClient(client, "std/network: WebSocket upgrade refused (HTTP " + code + ")");
        return;
    }
    const std::string accept = wsHeaderValue(head, "sec-websocket-accept");
    if (!accept.empty() && accept != wsAcceptToken(client->key)) {
        failClient(client, "std/network: WebSocket handshake accept token mismatch");
        return;
    }
    client->impl->emitOpen();
}

// =============================================================================
// Frames
// =============================================================================
void handleTextFrame(const std::shared_ptr<WsClient>& client, std::uint8_t opcode,
                     const std::string& payload) {
    if (opcode == 0x2u) {
        client->impl->emitMessage(payload);  // binary arrives as a string (Buffer == string)
        return;
    }
    client->impl->emitMessage(payload);
}

void handleFrame(const std::shared_ptr<WsClient>& client, bool fin, std::uint8_t opcode,
                 const std::string& payload) {
    if (opcode == 0x8u) {  // close
        double code = 1005.0;
        std::string reason;
        if (payload.size() >= 2) {
            code = static_cast<double>((static_cast<unsigned char>(payload[0]) << 8) |
                                       static_cast<unsigned char>(payload[1]));
            reason = payload.substr(2);
        }
        queueClose(client, code, reason);
        return;
    }
    if (opcode == 0x9u) {  // ping -> pong
        client->outbox += wsMakeFrame(0xAu, payload);
        flushWrites(client);
        return;
    }
    if (opcode == 0xAu) return;  // pong
    if (opcode == 0x0u) {        // continuation
        if (client->fragmentOpcode == 0) return;
        client->fragment += payload;
        if (fin) {
            const std::string message = std::move(client->fragment);
            const std::uint8_t first = client->fragmentOpcode;
            client->fragment.clear();
            client->fragmentOpcode = 0;
            handleTextFrame(client, first, message);
        }
        return;
    }
    if (!fin) {
        client->fragmentOpcode = opcode;
        client->fragment = payload;
        return;
    }
    handleTextFrame(client, opcode, payload);
}

void parseFrames(const std::shared_ptr<WsClient>& client) {
    while (!client->finished) {
        const std::string& buffer = client->buffer;
        if (buffer.size() < 2) return;
        const std::uint8_t first = static_cast<std::uint8_t>(buffer[0]);
        const std::uint8_t second = static_cast<std::uint8_t>(buffer[1]);
        const bool fin = (first & 0x80u) != 0;
        const std::uint8_t opcode = first & 0x0Fu;
        const bool masked = (second & 0x80u) != 0;
        std::uint64_t length = second & 0x7Fu;
        std::size_t offset = 2;
        if (length == 126u) {
            if (buffer.size() < 4) return;
            length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer[2])) << 8) |
                     static_cast<std::uint8_t>(buffer[3]);
            offset = 4;
        } else if (length == 127u) {
            if (buffer.size() < 10) return;
            length = 0;
            for (int i = 0; i < 8; ++i) {
                length = (length << 8) | static_cast<std::uint8_t>(buffer[2 + i]);
            }
            offset = 10;
        }
        std::uint8_t mask[4] = {0, 0, 0, 0};
        if (masked) {
            if (buffer.size() < offset + 4) return;
            std::memcpy(mask, buffer.data() + offset, 4);
            offset += 4;
        }
        if (length > 16u * 1024u * 1024u) {
            failClient(client, "std/network: WebSocket frame too large");
            return;
        }
        if (buffer.size() < offset + length) return;
        std::string payload = buffer.substr(offset, static_cast<std::size_t>(length));
        client->buffer.erase(0, offset + static_cast<std::size_t>(length));
        if (masked) {
            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ mask[i & 3]);
            }
        }
        handleFrame(client, fin, opcode, payload);
    }
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
    const std::shared_ptr<WsClient> client = clientOf(reinterpret_cast<uv_handle_t*>(stream));
    if (client != nullptr && nread > 0) {
        client->buffer.append(buf->base, static_cast<std::size_t>(nread));
    }
    if (buf->base != nullptr) delete[] buf->base;
    if (client == nullptr || client->finished || nread == 0) return;
    if (nread < 0) {
        if (nread == UV_EOF) {
            closeClient(client, 1006.0, "");
        } else {
            failClient(client, "std/network: WebSocket connection error");
        }
        return;
    }
    if (!client->impl->opened) {
        tryFinishHandshake(client);
        if (!client->impl->opened || client->finished) return;
    }
    parseFrames(client);
}

void onConnected(uv_connect_t* req, int status) {
    const std::shared_ptr<WsClient> client = clientOf(reinterpret_cast<uv_handle_t*>(req->handle));
    if (client == nullptr || client->finished) return;
    if (status < 0) {
        failClient(client, "std/network: could not connect to " + client->host);
        return;
    }
    client->outbox += "GET " + client->target + " HTTP/1.1\r\nHost: " + client->host + ":" +
                      std::to_string(client->port) +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
                      client->key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    flushWrites(client);
    uv_read_start(req->handle, onAlloc, onRead);
}

void onResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    std::unique_ptr<std::shared_ptr<WsClient>> holder(
        static_cast<std::shared_ptr<WsClient>*>(req->data));
    const std::shared_ptr<WsClient> client = *holder;
    if (client->finished) {
        if (res != nullptr) uv_freeaddrinfo(res);
        return;
    }
    if (status != 0 || res == nullptr) {
        if (res != nullptr) uv_freeaddrinfo(res);
        failClient(client, "std/network: DNS lookup failed for " + client->host);
        return;
    }
    const int rc = uv_tcp_connect(&client->connect, &client->tcp, res->ai_addr, onConnected);
    uv_freeaddrinfo(res);
    if (rc != 0) {
        failClient(client, "std/network: could not start the connection");
    }
}

}  // namespace

std::string parseWsUrl(const std::string& url, std::string& host, int& port, std::string& target) {
    if (url.rfind("wss://", 0) == 0) {
        return "std/network: wss:// is not supported in this build (TLS is off)";
    }
    const std::string prefix = "ws://";
    if (url.rfind(prefix, 0) != 0) {
        return "std/network: WebSocket needs a ws:// URL, got: " + url;
    }
    const std::string rest = url.substr(prefix.size());
    const std::size_t slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    if (slash != std::string::npos) {
        target = rest.substr(slash);
    }
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        host = authority;
    } else {
        host = authority.substr(0, colon);
        const std::string error = parsePortText(authority.substr(colon + 1), port);
        if (!error.empty()) {
            return "std/network: " + error + " in URL: " + url;
        }
    }
    if (host.empty()) {
        return "std/network: missing host in URL: " + url;
    }
    return {};
}

void wsClientStart(const std::shared_ptr<WsImpl>& impl, const std::string& host, int port,
                   const std::string& target) {
    auto client = std::make_shared<WsClient>();
    client->impl = impl;
    client->host = host;
    client->port = port;
    client->target = target;
    std::uint8_t keyBytes[16];
    randomBytes(keyBytes, sizeof(keyBytes));
    client->key = base64Encode(keyBytes, sizeof(keyBytes));
    impl->client = client;
    retainNetwork();
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) {
        failClient(client, "std/network: could not create the libuv loop");
        return;
    }
    uv_tcp_init(loop, &client->tcp);
    attachClient(reinterpret_cast<uv_handle_t*>(&client->tcp), client);
    client->tcpReady = true;
    struct sockaddr_in addr{};
    if (uv_ip4_addr(host.c_str(), port, &addr) == 0) {
        const int rc = uv_tcp_connect(&client->connect, &client->tcp,
                                      reinterpret_cast<const struct sockaddr*>(&addr), onConnected);
        if (rc != 0) failClient(client, "std/network: could not start the connection");
        return;
    }
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    client->dns.data = new std::shared_ptr<WsClient>(client);
    const std::string portText = std::to_string(port);
    const int rc =
        uv_getaddrinfo(loop, &client->dns, onResolved, host.c_str(), portText.c_str(), &hints);
    if (rc != 0) {
        delete static_cast<std::shared_ptr<WsClient>*>(client->dns.data);
        client->dns.data = nullptr;
        failClient(client, "std/network: DNS lookup could not start");
    }
}

void wsClientSend(const std::shared_ptr<WsImpl>& impl, const std::string& data) {
    if (impl == nullptr || impl->client == nullptr || impl->client->finished) return;
    impl->client->outbox += wsMakeFrame(0x1u, data);
    flushWrites(impl->client);
}

void wsClientClose(const std::shared_ptr<WsImpl>& impl, double code, const std::string& reason) {
    if (impl == nullptr || impl->client == nullptr || impl->client->finished) return;
    queueClose(impl->client, code, reason);
}

}  // namespace cp3::runtime::network::detail
