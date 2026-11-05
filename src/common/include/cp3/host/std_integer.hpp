// =============================================================================
// cp3/host/std_integer.hpp - the one semantic-width table for std integers.
//
// Every integer-valued standard-library result (and the matching parameters)
// has exactly one `StdIntegerKind`. The width is a property of the kind, never
// of the module: HTTP status codes and ports are `u16`, process ids and exit
// codes `i32`, timestamps `i64`, byte sizes `u64`, and collection
// lengths/indexes/counts `usize` (canonical `u64`, displayed as `usize` in a
// Rust project and `size_t` in a C++ one).
//
// A TypeScript project keeps `number` for every kind, so existing code is
// unchanged. Rust/C++ projects get the exact native width. Modules must not
// spell these types themselves: call `stdIntegerType` for the semantic type
// and `stdIntegerSpelling` for generated documentation, so typing and docs
// can never drift.
// =============================================================================
#pragma once

#include <string>

#include "cp3/core/types.hpp"
#include "cp3/host/options.hpp"

namespace cp3 {

enum class StdIntegerKind {
    Status,       // HTTP status code, WebSocket close code
    Port,         // listen/bind port
    Pid,          // process or child-process id
    ExitCode,     // process exit status
    Timestamp,    // epoch milliseconds
    FileSize,     // byte counts that can exceed 2^32
    Length,       // collection/string/buffer length
    Index,        // collection/string index (non-negative)
    Count,        // element/thread count
    Checksum32,   // 32-bit checksum (CRC-32, FNV-1a 32)
    Checksum64,   // 64-bit checksum (FNV-1a 64)
    ByteValue,    // a single byte value (0-255), e.g. `str[i]`
    SignedIndex,  // a signed index: -1 is meaningful (not found, from end)
};

// The canonical native id behind a kind: "u8", "u16", "u32", "i32", "i64" or
// "u64". (The Rust `usize`/C++ `size_t` spellings resolve to the canonical
// "u64"; the Rust `isize`/C++ `ptrdiff_t` spellings resolve to "i64".)
const char* stdIntegerNativeId(StdIntegerKind kind) noexcept;

// The semantic type of one kind in a project vocabulary: `number` for a
// TypeScript project, the native width above for Rust/C++ projects.
Type stdIntegerType(StdIntegerKind kind, TypeSystem ts);

// The spelling generated documentation shows: "number", "u16"/"uint16_t",
// "i64"/"int64_t", "usize"/"size_t", ...
const char* stdIntegerSpelling(StdIntegerKind kind, TypeSystem ts) noexcept;

}  // namespace cp3
