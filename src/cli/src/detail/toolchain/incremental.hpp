// =============================================================================
// src/cli/src/detail/toolchain/incremental.hpp - per-module incremental cache DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header (detail/**).
//
// The v0.2.5 incremental frontend: for a local-module program the codegen
// fragments of every unit are cached in `<projectRoot>/.cp3/cache/
// incremental-program-cache`, keyed per unit by
//
//   sourceHash      fnv1a64 of the unit's source bytes
//   ihFingerprint   fnv1a64 of the joined interface hashes of its resolved
//                   local/package dependencies, in declaration order
//   classMethodHash fnv1a64 of the multiset of class method names in the
//                   whole program (the same proxy the codegen virtual-set
//                   pass needs to stay meaningful under reuse)
//   configHash      toolchain (compilerKey) + profile
//
// A cache entry that matches reproduces the unit byte-identically without
// re-sema or re-codegen: the fragments carry the unit's namespace, globals,
// declaration and definition sections and its module initializer. Any change
// to the unit, its imports' interfaces, any class member anywhere in the
// program, the toolchain or the profile retires that entry. A corrupt cache
// file is a miss everywhere (never a stale hit).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "detail/toolchain/pipeline.hpp"

namespace cp3::cli {

struct ProgramUnitFragment {
    std::string namesp;    // e.g. "cp3_mod_counter"
    std::string globals;   // guarded global `$o{...}` value structs
    std::string decls;     // forward declarations + module-scope bindings
    std::string defs;      // function definitions
    std::string mainBody;  // top-level statements (the module initializer)
    std::string sourceKey; // the unit's logical module name (manifest identity)
    bool isEntry = false;
    bool isPackage = false;
    bool topLevelAwait = false;
};

struct IncrementalUnitEntry {
    std::string unitKey;        // module path-ish logical name
    std::string sourceHash;     // hex fnv1a64 of the unit's source bytes
    std::string ihFingerprint;  // hex fnv1a64 of dep interface hashes
    std::string classMethodHash;
    std::string configHash;     // compilerKey + profile
    std::string interfaceHash;  // hex fnv1a64 of the unit's own surface
    ProgramUnitFragment fragment;
};

// The shared cache file for one project (lives in the .cp3/cache dir).
constexpr const char* kIncrementalCacheFile = "incremental-program-cache";
// The cache format version. Any change to this scheme rewrites the file.
constexpr const char* kIncrementalCacheFormat = "cp3-incremental-cache/1";

// Reads the cache. Returns false on any I/O/parse error (all units are
// then treated as fresh); empty cache files are a valid empty hit-set.
bool incrementalCacheLoad(const fs::path& cachePath,
                          std::vector<IncrementalUnitEntry>& out);

// Writes the cache atomically (write-then-rename is not needed on the one
// .cp3/build dir; a torn file just retires every entry next run). Best
// effort: a file that cannot be written is not an error.
bool incrementalCacheStore(const fs::path& cachePath,
                           const std::vector<IncrementalUnitEntry>& entries);

// A unit's cache key and the fingerprint of its dependency interfaces.
std::string incrementalUnitKey(const CompilationUnit& unit);
std::string unitSourceHash(const CompilationUnit& unit);
std::string classMethodHash(const ModuleGraph& graph);
std::string configHashOf(const std::string& compilerKey,
                         const std::string& profile);

// fnv1a64 joined over the hex interface hashes of this unit's resolved
// local/package dependencies, in graph dependency declaration order.
std::string ihFingerprintOf(
    const ModuleGraph& graph, const CompilationUnit& unit,
    const std::unordered_map<std::uint32_t, std::uint64_t>& ihOf);

}  // namespace cp3::cli
