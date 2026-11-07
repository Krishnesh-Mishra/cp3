// =============================================================================
// src/std/network/runtime_fetch.cpp - `std/network` HTTP client (`fetch`).
//
// OWNERSHIP: the std/network family (src/std/network/**). Same selection rule as
// runtime.cpp (the std/network native package row, D68): compiled + linked only
// for a program that imports "std/network".
// One request, one TCP connection, `Connection: close`: beta-one fetch is a
// plain HTTP/1.1 client - no connection reuse, no redirects, no TLS. The
// request headers option is the documented `Map<string, string>`, the response
// header block comes back as the documented `res.headers` Map, and the body is
// fully buffered (Content-Length, chunked and close-delimited), so
// `res.text()` / `res.json<T>()` never suspend. A FetchOp stays alive through
// shared_ptr holders in `handle->data` (tcp, timer, DNS request, in-flight
// write) and counts as pending work until it settles; the libuv transport
// (handle attachment, callbacks, beginFetch) lives in the sibling
// runtime_fetch_io.cpp.
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

// =============================================================================
// URL parsing
// =============================================================================
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

// =============================================================================
// Settling and handle lifetime
// =============================================================================
/// The documented Map-shaped fields (`res.headers`, and the `headers` option)
/// are `Map<string, string>` (docs 04 §network); this turns one such pair list
/// into the Map.
StringMap headerMap(const std::vector<std::pair<std::string, std::string>>& pairs) {
    StringMap map = collections::makeMap<std::string, std::string>();
    for (const auto& [name, value] : pairs) {
        map->set(name, value);
    }
    return map;
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
    op->promise.fulfill(FetchResponse(op->statusCode, op->statusText, std::move(op->decoded),
                                     headerMap(op->responseHeaders)));
    releaseOp(op);
}

// =============================================================================
// Request head and response parsing
// =============================================================================
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

bool parseStatusCode(const std::string& text, std::uint16_t& code) {
    if (text.size() != 3) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    code = static_cast<std::uint16_t>((text[0] - '0') * 100 +
                                      (text[1] - '0') * 10 + (text[2] - '0'));
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
    op.responseHeaders.clear();
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
        // Every header reaches the documented `res.headers` Map; the framing
        // names are also read out below.
        op.responseHeaders.emplace_back(key, value);
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

}  // namespace

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

void failOp(const std::shared_ptr<FetchOp>& op, const std::string& message) {
    if (op->done) return;
    op->done = true;
    op->promise.rejectWith(Thrown::make(message));
    releaseOp(op);
}

std::string buildRequest(const FetchOp& op) {
    std::string request = op.init.method.empty() ? "GET" : op.init.method;
    request += " " + op.url.target + " HTTP/1.1\r\nHost: " + op.authority +
               "\r\nAccept: */*\r\nConnection: close\r\n";
    // The documented `RequestInit.headers` Map (docs 04 §network): one
    // `Name: value` line per entry. The framing-owned names stay ours: a user
    // override of host/content-length/connection would malform the request.
    if (op.init.headers) {
        // `entries()` returns an owning `Array` (a `Ref`); keep it in a named
        // local so the vector outlives the loop (no deref of a prvalue Ref).
        const auto entries = op.init.headers->entries();
        for (const auto& [name, value] : *entries) {
            const std::string lowered = lowerAscii(name);
            if (lowered == "host" || lowered == "content-length" ||
                lowered == "connection") {
                continue;
            }
            request += name + ": " + value + "\r\n";
        }
    }
    if (op.init.hasBody) {
        request += "Content-Length: " + std::to_string(op.init.body.size()) + "\r\n";
    }
    request += "\r\n";
    if (op.init.hasBody) {
        request += op.init.body;
    }
    return request;
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

}  // namespace cp3::runtime::network::detail

// =============================================================================
// Entry point
// =============================================================================
namespace cp3::runtime::network {

Promise<FetchResponse> fetch(const std::string& url, const FetchInit& init) {
    detail::UrlParts parts;
    const std::string error = detail::parseHttpUrl(url, parts);
    if (!error.empty()) {
        return Promise<FetchResponse>::reject(Thrown::make(error));
    }
    ensureLoop();
    FetchInit effective = init;  // Ledger L (appended): AbortSignal.timeout
    if (effective.signal && effective.signal->timeoutMs > 0 &&
        (effective.timeoutMs <= 0 || effective.signal->timeoutMs < effective.timeoutMs)) {
        effective.timeoutMs = effective.signal->timeoutMs;
    }
    const std::shared_ptr<detail::FetchOp> op = detail::beginFetch(parts, effective);
    return op->promise;
}

}  // namespace cp3::runtime::network
