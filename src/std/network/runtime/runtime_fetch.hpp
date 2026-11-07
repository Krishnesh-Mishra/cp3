// =============================================================================
// src/std/network/runtime_fetch.hpp - PRIVATE std/network fetch surfaces.
//
// OWNERSHIP: the std/network family (src/std/network/**). Shared by
// runtime_fetch.cpp (URL parsing, the request head, the response readers and
// the op lifecycle) and runtime_fetch_io.cpp (handle attachment, the libuv
// callbacks and `beginFetch`); never installed and never part of the
// generated-code contract.
//
// One `FetchOp` per in-flight request. `parseHttpUrl`, `failOp`,
// `buildRequest`, `consume` and `finishOnEof` are defined in runtime_fetch.cpp,
// `beginFetch` in runtime_fetch_io.cpp; every other helper stays TU-local
// (anonymous namespace), like the other network units.
// =============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <uv.h>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::detail {

// =============================================================================
// URL parsing
// =============================================================================
struct UrlParts {
    std::string host;
    int port = 80;
    std::string target = "/";  // path + query, exactly as sent on the request line
    bool isTls = false;
};

/// Parses `http://host[:port][/path]` or `https://host[:port][/path]`;
/// empty result string = success, else the rejection message.
std::string parseHttpUrl(const std::string& url, UrlParts& out);

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
    // https:// only: owns the `tls::TlsSocket` until the op settles (weak
    // captures in its slot lambdas keep the op ↔ socket cycle breakable).
    std::shared_ptr<void> tls;

    std::string raw;         // every byte received so far
    std::size_t cursor = 0;  // parse cursor into `raw`
    std::uint16_t statusCode = 0;
    std::string statusText;
    std::vector<std::pair<std::string, std::string>> responseHeaders;  // name lower cased
    long long contentLength = -1;
    BodyState bodyState = BodyState::Head;
    long long chunkLeft = 0;
    std::string decoded;  // the decoded body
};

// =============================================================================
// Shared entry points
// =============================================================================
/// Settles `op` as rejected and releases its handles (runtime_fetch.cpp).
void failOp(const std::shared_ptr<FetchOp>& op, const std::string& message);

/// The request head (and body) built from `op.init` (runtime_fetch.cpp).
std::string buildRequest(const FetchOp& op);

/// Feeds every received byte through the response state machine, settling the
/// promise once the body is complete (runtime_fetch.cpp).
void consume(const std::shared_ptr<FetchOp>& op);

/// Handles the connection closing before the body is complete
/// (runtime_fetch.cpp).
void finishOnEof(const std::shared_ptr<FetchOp>& op);

/// Starts tcp + timeout + DNS/connect for one request (runtime_fetch_io.cpp).
std::shared_ptr<FetchOp> beginFetch(const UrlParts& url, const FetchInit& init);

}  // namespace cp3::runtime::network::detail
