// =============================================================================
// src/lsp/src/detail/framing.hpp - LSP Content-Length framing on stdio.
//
// OWNERSHIP: LSP workstream (src/lsp/**). PRIVATE header.
//
// LSP 3.17 over stdio: ASCII headers ("Content-Length: N\r\n\r\n") followed by
// N UTF-8 bytes of JSON. Nothing else may ever be written to stdout, so the
// writer is the ONLY place frames are produced and it flushes every frame.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace cp3::lsp {

// Upper bound for one message body. A larger Content-Length is a framing
// error: the stream position cannot be trusted afterwards, so the server
// stops instead of buffering unbounded client input.
inline constexpr std::size_t kMaxFrameBytes = 64u * 1024u * 1024u;

// The `err` value readFrame sets for a clean end of input (EOF at a frame
// boundary); callers exit quietly in that case.
inline constexpr const char* kEofReason = "EOF";

// Reads one frame body, blocking until it arrives. Returns false at EOF, on a
// malformed header block, or when Content-Length is missing/oversized; `err`
// then carries a short diagnostic (kEofReason for a clean EOF).
bool readFrame(std::FILE* in, std::string& body, std::string& err);

// Writes one frame and flushes. False when the stream is gone (client died):
// the caller stops rather than failing repeatedly.
bool writeFrame(std::FILE* out, std::string_view body);

// Puts stdin/stdout into binary mode. On Windows the default text mode would
// translate \n to \r\n inside bodies and corrupt every frame; on POSIX this is
// a no-op.
void setBinaryStdio();

// True when more bytes are already buffered on `in` (no blocking). False when
// the platform or the stream type cannot answer; callers then skip the
// coalescing window and run the check immediately, which is always correct.
bool stdinHasData();

}  // namespace cp3::lsp
