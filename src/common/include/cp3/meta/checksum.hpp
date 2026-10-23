// =============================================================================
// cp3/checksum.hpp - the SHA-256 content fingerprint.
//
// OWNERSHIP: common module. The algorithm is implemented from FIPS 180-4 in
// plain C++20 (no third-party code, no platform crypto APIs), so the same
// bytes are produced on every platform.
//
// It lives in common because two modules need the same fingerprint: the
// package manager (`.cp3/lock.json` integrity entries) and the native
// binding reader (the `contentHash` a `native.json` records for its
// `index.cp3`). The CLI's private `detail/checksum.hpp` re-exports it, so the
// package manager's call sites are unchanged.
// =============================================================================
#pragma once

#include <string>
#include <string_view>

namespace cp3 {

// Lowercase hex SHA-256 of `data` (64 characters).
std::string sha256Hex(std::string_view data);

}  // namespace cp3
