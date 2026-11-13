// =============================================================================
// src/codegen/src/detail/generator/fragment_cache.hpp - PRIVATE per-unit
// codegen fragment cache (the v0.2.5 incremental reuse mechanism).
//
// OWNERSHIP: codegen module (src/codegen/**). PRIVATE header.
//
// The CLI (incremental profile only) pre-validates and rewrites
// <projectRoot>/.cp3/cache/incremental-program-cache so every record it
// contains corresponds to one checked unit whose key currently matches
// (source bytes, interface fingerprint of its local/package dependencies,
// program-wide class/enum shape, toolchain + profile). This file consumes
// that cache at assembly time: for each unit it re-verifies the recorded
// source hash and the isEntry/isPackage flags; a verified record reproduces
// the unit's text sections byte-identically, so the emit walk is skipped.
// Every miss falls back to the ordinary emit path. Any corrupt record is a
// miss. The file is rewritten by the assembly with the union of reused and
// freshly emitted records (temp + rename).
//
// The format is mirrored from detail/toolchain/incremental.hpp in the CLI
// module (there is no cross-module sharing; both sides document the wire
// format identically):
//
//   magic line: "cp3-incremental-cache/3"
//   one line per record:
//     <len>:<sourceKey> <len>:<sourceHash> <len>:<ihFingerprint>
//     <len>:<shapeHash> <len>:<configHash> <len>:<interfaceHash>
//     <len>:<sourceKey> <len>:<namesp> <len>:<globals> <len>:<decls>
//     <len>:<defs> <len>:<mainBody>
//     <len>:<needs-bits> <len>:<native-headers> <len>:<flags>
// The multi-line value fields (globals, decls, defs, mainBody, native
// headers) are hex-encoded so every record fits on one physical line.
//
// needsMask bit layout (one bit per ProgramNeeds bool flag):
//    0 consoleStderr   1 consoleAbi     2 eventLoop    3 arith      4 math
//    5 number          6 str            7 path         8 os         9 json
//   10 jsonFile       11 process       12 env        13 crypto    14 date
//   15 url            16 time          17 hash       18 text      19 readline
//   20 network        21 webserverSettings  22 typedArray 23 collections
//   24 buffer         25 fs            26 io         27 tuple     28 eventEmitter
//   29 bitwise        30 unionValue    31 objectValue 32 regex      33 parallel
//   34 worker
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/meta/module.hpp"

namespace cp3::detail {

// One stored unit fragment, exactly as the CLI wrote it.
struct FragmentRecord {
    std::string unitKey;
    std::string sourceHash;
    std::string ihFingerprint;
    std::string shapeHash;
    std::string configHash;
    std::string interfaceHash;
    std::string fragmentSourceKey;
    std::string namesp;
    std::string globals;
    std::string decls;
    std::string defs;
    std::string mainBody;
    std::uint64_t needsMask = 0;
    std::vector<std::string> nativeHeaders;
    bool isEntry = false;
    bool isPackage = false;
    bool topLevelAwait = false;
};

// The loaded cache for one assembly. Empty when the feature is off.
struct FragmentCache {
    std::unordered_map<std::string, FragmentRecord> records;
};

// fnv1a64 of the unit's source text (0 for a missing source).
std::uint64_t fragmentSourceHashOf(const CompilationUnit& unit);

// Reads the cache named by CP3_INCREMENTAL_FRAGMENTS. Returns nullptr (the
// feature is off, zero cost) when the variable is unset or the file is
// unreadable/invalid (a miss everywhere, never a stale hit).
FragmentCache* loadFragmentCache();

// Rewrites the cache file atomically (temp + rename) with the given records.
// Best effort; failure leaves the previous file intact.
bool writeFragmentCache(const std::filesystem::path& cachePath,
                        const std::vector<FragmentRecord>& records);

}  // namespace cp3::detail
