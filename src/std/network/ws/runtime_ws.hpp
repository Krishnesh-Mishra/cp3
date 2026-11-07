// =============================================================================
// src/std/network/runtime_ws.hpp - PRIVATE std/network WebSocket surfaces.
//
// OWNERSHIP: the std/network family (src/std/network/**). Shared by runtime_ws.cpp
// (the public WebSocket / WebSocketServer classes) and runtime_ws_client.cpp (the
// libuv client transport); never installed and never part of the generated-code
// contract.
//
// One `WsImpl` serves both directions: a client instance owns a `WsClient`
// transport (runtime_ws_client.cpp), a server instance wraps an accepted
// uWS::WebSocket whose native pointer is `native` (runtime_ws.cpp). Handlers
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

/// The four listener shapes a documented WebSocket event can have (the
/// message payload is `string | Buffer`, docs 04 §network):
///   "open" -> `()`, "message" -> `(string | Buffer)`, "close" ->
///   `(number, string)`, "error" -> `(unknown)`; a handler may also declare fewer parameters and
/// ignore the payload, which lands in `none`.
struct WsSlots {
    std::function<void()> none;
    std::function<void(const std::string&)> text;
    std::function<void(const buffer::Buffer&)> binary;
    std::function<void(double, const std::string&)> pair;
    std::function<void(const Thrown&)> thr;
};

/// The client transport state (`runtime_ws_client.cpp` owns the definition).
struct WsClient;

struct WsImpl {
    enum class Kind { Client, Server };

    Kind kind = Kind::Client;
    bool opened = false;
    bool closed = false;
    bool ssl = false;  // server-side native is uWS::WebSocket<true, true, WsUserData>*
    void* native = nullptr;  // server side: uWS::WebSocket<false|true, true, WsUserData>*
    std::shared_ptr<WsClient> client;
    std::map<std::string, WsSlots> slots;

    void emitOpen();
    /// A message frame: `isBinary` picks the Buffer slot over the string one.
    void emitMessage(const std::string& data, bool isBinary = false);
    void emitClose(double code, const std::string& reason);
    void emitError(const std::string& message);
};

/// RFC 6455 protocol helpers (runtime_ws_frames.cpp). `randomBytes` is the
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

/// Parses `ws://host[:port][/path]` or `wss://host[:port][/path]`; sets
/// `isTls` for the `wss://` form (default port 443). Returns an empty string
/// on success, else the reason to throw.
std::string parseWsUrl(const std::string& url, std::string& host, int& port, std::string& target, bool& isTls);

/// Client transport entry points (runtime_ws_client.cpp). `host`/`port`/
/// `target` are already parsed; `start` retains the runtime loop.
void wsClientStart(const std::shared_ptr<WsImpl>& impl, const std::string& host, int port,
                   const std::string& target, bool isTls);
void wsClientSend(const std::shared_ptr<WsImpl>& impl, const std::string& data);
/// Sends a binary frame (opcode 0x2) carrying `data` bytes.
void wsClientSendBinary(const std::shared_ptr<WsImpl>& impl, const std::string& data);
void wsClientClose(const std::shared_ptr<WsImpl>& impl, double code, const std::string& reason);

}  // namespace cp3::runtime::network::detail
