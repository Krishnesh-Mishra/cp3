// =============================================================================
// src/runtime/src/std/network_ws.hpp - PRIVATE std/network WebSocket surfaces.
//
// OWNERSHIP: the network wave. Shared by network_ws.cpp (the public WebSocket /
// WebSocketServer classes) and network_ws_client.cpp (the libuv client
// transport); never installed and never part of the generated-code contract.
//
// One `WsImpl` serves both directions: a client instance owns a `WsClient`
// transport (network_ws_client.cpp), a server instance wraps an accepted
// uWS::WebSocket whose native pointer is `native` (network_ws.cpp). Handlers
// are stored per event name in the shape the documented listener has, so the
// two transports can both emit through `emit*` below.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::detail {

/// The four listener shapes a documented WebSocket event can have:
///   "open" -> `()`, "message" -> `(string)`, "close" -> `(number, string)`,
///   "error" -> `(unknown)`; a handler may also declare fewer parameters and
/// ignore the payload, which lands in `none`.
struct WsSlots {
    std::function<void()> none;
    std::function<void(const std::string&)> text;
    std::function<void(double, const std::string&)> pair;
    std::function<void(const Thrown&)> thr;
};

/// The client transport state (`network_ws_client.cpp` owns the definition).
struct WsClient;

struct WsImpl {
    enum class Kind { Client, Server };

    Kind kind = Kind::Client;
    bool opened = false;
    bool closed = false;
    void* native = nullptr;  // server side: uWS::WebSocket<false, true, WsUserData>*
    std::shared_ptr<WsClient> client;
    std::map<std::string, WsSlots> slots;

    void emitOpen();
    void emitMessage(const std::string& data);
    void emitClose(double code, const std::string& reason);
    void emitError(const std::string& message);
};

/// RFC 6455 protocol helpers (network_ws_frames.cpp). `randomBytes` is the
/// masking-key/key-nonce source: `std::mt19937_64`, so the bytes are random but
/// NOT cryptographically secure (documented caveat, no TLS in this build).
void randomBytes(std::uint8_t* out, std::size_t size);
std::string base64Encode(const std::uint8_t* data, std::size_t size);
/// base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")), the expected
/// `Sec-WebSocket-Accept` value.
std::string wsAcceptToken(const std::string& key);
/// One masked client frame (RFC 6455 §5.3).
std::string wsMakeFrame(std::uint8_t opcode, const std::string& payload);
/// Case-insensitive `name:` header lookup over a raw HTTP head block.
std::string wsHeaderValue(const std::string& head, const std::string& name);

/// Parses `ws://host[:port][/path]` (`wss://` reports the TLS-off message).
/// Returns an empty string on success, else the reason to throw.
std::string parseWsUrl(const std::string& url, std::string& host, int& port, std::string& target);

/// Client transport entry points (network_ws_client.cpp). `host`/`port`/
/// `target` are already parsed; `start` retains the runtime loop.
void wsClientStart(const std::shared_ptr<WsImpl>& impl, const std::string& host, int port,
                   const std::string& target);
void wsClientSend(const std::shared_ptr<WsImpl>& impl, const std::string& data);
void wsClientClose(const std::shared_ptr<WsImpl>& impl, double code, const std::string& reason);

}  // namespace cp3::runtime::network::detail
