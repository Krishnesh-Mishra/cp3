// =============================================================================
// src/std/network/runtime.cpp - `std/network` loop plumbing + JSON readers.
//
// OWNERSHIP: the std/network family (src/std/network/**). Selected (compiled +
// linked, with uSockets + libuv) only when a program imports "std/network" -
// see src/std/network/native.hpp.
//
// Backend notes: libuv v1.53.0 + uWebSockets v20.80.0 static (third_party/**,
// D70). Every TU that sees the uWS headers is compiled with -DUWS_NO_ZLIB
// (D68 wires it); TLS is off, so beta-one fetch is http:// only.
//
// This TU owns the per-thread libuv loop (runtime_loop.hpp) and the scalar
// JSON readers the response/request types share. The HTTP client behind
// `fetch` is runtime_fetch.cpp, the server side runtime_server.cpp, the
// WebSocket side runtime_ws.cpp + runtime_ws_client.cpp.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <uv.h>

#include "Loop.h"  // uWS::Loop (borrows the uv loop this TU creates)

#include "runtime_loop.hpp"

namespace cp3::runtime::network {

// =============================================================================
// The per-thread libuv loop (runtime_loop.hpp)
// =============================================================================
namespace {

/// Everything the per-thread loop needs. Created once per thread and leaked on
/// purpose: it outlives every uWS context and every cp3 object living on it.
struct LoopState {
    uv_loop_t loop{};
    uv_timer_t bridge{};  // one-shot bound armed around each blocking turn
    uv_async_t wake{};    // thread-safe nudge used by postFromThread
};

void onBridgeFire(uv_timer_t*) {}  // arming is the whole job
void onWake(uv_async_t*) {}        // waking the poll is the whole job

LoopState* createLoopState() {
    LoopState* state = new LoopState;
    if (uv_loop_init(&state->loop) != 0) {
        delete state;
        return nullptr;
    }
    uv_timer_init(&state->loop, &state->bridge);
    uv_async_init(&state->loop, &state->wake, onWake);
    // Hand the loop to uWS BEFORE any App exists: uSockets borrows it
    // (cleanMe = false, never freed) and every uWS context on this thread then
    // lives on it. integrate() hooks uWS's own unref'd timers onto it.
    uWS::Loop::get(&state->loop)->integrate();
    // Drive libuv from the runtime loop's idle phase (eventloop.hpp).
    currentLoop().setExternalSource(
        [state] { return uv_run(&state->loop, UV_RUN_NOWAIT) != 0; },
        [state](std::int64_t maxWaitMs) {
            if (maxWaitMs >= 0) {
                uv_timer_start(&state->bridge, onBridgeFire,
                               static_cast<std::uint64_t>(maxWaitMs), 0);
            } else {
                uv_timer_stop(&state->bridge);
            }
            uv_run(&state->loop, UV_RUN_ONCE);
            uv_timer_stop(&state->bridge);
        },
        [state] { uv_async_send(&state->wake); });
    return state;
}

LoopState* loopState() {
    static thread_local LoopState* state = createLoopState();
    return state;
}

}  // namespace

void ensureLoop() { (void)loopState(); }

void* loopHandle() {
    LoopState* state = loopState();
    return state == nullptr ? nullptr : &state->loop;
}

void retainNetwork() { currentLoop().retainExternal(); }
void releaseNetwork() { currentLoop().releaseExternal(); }

// =============================================================================
// JSON scalar readers (cp3/std/network.hpp `detail`)
// =============================================================================
namespace detail {
namespace {

struct Cursor {
    const std::string& text;
    std::size_t pos = 0;
};

[[noreturn]] void fail(const char* expected) {
    throw Thrown::make(std::string("std/network: JSON payload is not a ") + expected);
}

void skipSpace(Cursor& cursor) {
    while (cursor.pos < cursor.text.size() &&
           std::isspace(static_cast<unsigned char>(cursor.text[cursor.pos])) != 0) {
        ++cursor.pos;
    }
}

void requireEnd(const Cursor& cursor, const char* expected) {
    if (cursor.pos != cursor.text.size()) fail(expected);
}

std::uint32_t hexQuad(const std::string& text, std::size_t at) {
    if (at + 4 > text.size()) fail("string");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = text[at + i];
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
        else fail("string");
        value = value * 16 + digit;
    }
    return value;
}

void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80u) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (code >> 6)));
        out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else if (code < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (code >> 12)));
        out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (code >> 18)));
        out.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    }
}

/// Decodes one `\uXXXX` escape (a surrogate pair when the low half follows) and
/// appends the UTF-8 bytes. `cursor.pos` points at the first hex digit; it is
/// advanced past every code unit that was consumed.
void appendEscapedCodeUnit(const std::string& text, Cursor& cursor, std::string& out) {
    std::uint32_t code = hexQuad(text, cursor.pos);
    cursor.pos += 4;
    const bool paired = code >= 0xD800u && code <= 0xDBFFu &&
                        cursor.pos + 6 <= text.size() &&
                        text.compare(cursor.pos, 2, "\\u") == 0;
    if (paired) {
        const std::uint32_t low = hexQuad(text, cursor.pos + 2);
        if (low >= 0xDC00u && low <= 0xDFFFu) {
            code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
            cursor.pos += 6;
        }
    }
    appendUtf8(out, code);
}

}  // namespace

std::string jsonString(const std::string& text) {
    Cursor cursor{text};
    skipSpace(cursor);
    if (cursor.pos >= text.size() || text[cursor.pos] != '"') fail("string");
    ++cursor.pos;
    std::string out;
    while (true) {
        if (cursor.pos >= text.size()) fail("string");
        const char c = text[cursor.pos++];
        if (c == '"') break;
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (cursor.pos >= text.size()) fail("string");
        const char escape = text[cursor.pos++];
        switch (escape) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                appendEscapedCodeUnit(text, cursor, out);
                break;
            default: fail("string");
        }
    }
    skipSpace(cursor);
    requireEnd(cursor, "string");
    return out;
}

double jsonNumber(const std::string& text) {
    Cursor cursor{text};
    skipSpace(cursor);
    const char* start = text.c_str() + cursor.pos;
    char* end = nullptr;
    const double value = std::strtod(start, &end);
    if (end == start) fail("number");
    cursor.pos = static_cast<std::size_t>(end - text.c_str());
    skipSpace(cursor);
    requireEnd(cursor, "number");
    return value;
}

bool jsonBool(const std::string& text) {
    Cursor cursor{text};
    skipSpace(cursor);
    bool value = false;
    if (text.compare(cursor.pos, 4, "true") == 0) {
        cursor.pos += 4;
        value = true;
    } else if (text.compare(cursor.pos, 5, "false") == 0) {
        cursor.pos += 5;
    } else {
        fail("boolean");
    }
    skipSpace(cursor);
    requireEnd(cursor, "boolean");
    return value;
}

}  // namespace detail
}  // namespace cp3::runtime::network
