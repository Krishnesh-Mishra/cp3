// =============================================================================
// src/cli/src/detail/toolchain/probe_cache.hpp - cached compiler probe DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header (detail/**).
//
// `cp3c run`/`dev` re-validated the compiler on every invocation (a test
// compile + test exe run for the saved/exact toolchain), which cost ~1s of
// the per-edit loop. The probe outcome depends only on the compiler binary
// itself, so the banner/version and the validation result are cached in
// `<config dir>/toolchain-probe-cache` (the same directory as
// `toolchain.conf`), keyed by the absolute program path plus its size and
// last-write-time. Swapping, patching or rebuilding the compiler changes the
// identity and forces a fresh probe automatically; the cache is never consulted
// for correctness of the produced binary and is never a reason to fail a
// build - any cache error means "miss" and the live probe runs as before.
// =============================================================================
#pragma once

#include <string>

namespace cp3::cli {

struct ProbeCacheEntry {
    std::string banner;   // first line of `<program> --version`
    std::string version;  // normalizeCompilerVersion(banner)
};

// True when a valid banner/version entry exists for this exact compiler binary
// (absolute path, size and last-write-time must all match). On any I/O or
// parse error the function returns false and `out` is left untouched.
bool probeCacheLookup(const std::string& program, ProbeCacheEntry& out);

// Upserts the entry. Best effort: a cache that cannot be written is not an
// error. Returns false only to signal the caller that nothing was persisted.
bool probeCacheStore(const std::string& program, const ProbeCacheEntry& entry);

// Cached result of the Windows target-flag probe (test-compiling a reference
// program to pick --target/--sysroot), keyed by the same binary identity.
// `flags` are joined by \x1f. `sysroot` records the resolved --sysroot (empty
// when the driver's own defaults were verified instead) so a swapped MinGW
// tree can be detected cheaply before reuse.
struct ProbeFlagsCacheEntry {
    std::string flags;   // \x1f-joined flag strings
    std::string sysroot; // the resolved --sysroot path, or empty
};

bool probeFlagsLookup(const std::string& program, ProbeFlagsCacheEntry& out);
bool probeFlagsStore(const std::string& program, const ProbeFlagsCacheEntry& entry);

}  // namespace cp3::cli
