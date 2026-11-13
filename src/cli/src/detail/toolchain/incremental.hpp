// =============================================================================
// src/cli/src/detail/toolchain/incremental.hpp - per-module incremental reuse
// DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header (detail/**).
//
// The v0.2.5 incremental reuse: for a local-module program the codegen
// fragments of every unit are cached in `<projectRoot>/.cp3/cache/
// incremental-program-cache`, keyed per unit by
//
//   sourceHash    fnv1a64 of the unit's source bytes
//   ihFingerprint fnv1a64 of the joined interface hashes of its resolved
//                 local/package dependencies, in declaration order
//   shapeHash     fnv1a64 of EVERY class/interface/alias/enum canonical
//                 shape in the whole program (the only airtight backstop
//                 for vtable slot layout: any member add/remove/rename/
//                 retype/reorder in any unit retires every cached fragment)
//   configHash    toolchain (compilerKey) + profile
//
// A cache entry that matches reproduces the unit byte-identically without
// re-codegen: the fragments carry the unit's globals, declarations,
// definitions and module initializer plus the ProgramNeeds bits and the
// native header list the shared header aggregates. A corrupt or partial
// cache file is a miss everywhere (never a stale hit).
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

#include "detail/toolchain/pipeline.hpp"

namespace cp3::cli {

struct ProgramUnitFragment {
    std::string namesp;    // e.g. "cp3_mod_counter"
    std::string globals;   // guarded global `$o{...}` value structs
    std::string decls;     // forward declarations + module-scope bindings
    std::string defs;      // function definitions
    std::string mainBody;  // top-level statements (the module initializer)
    std::string sourceKey; // the unit's logical module name (manifest identity)
    std::uint64_t needsMask = 0;  // one bit per ProgramNeeds bool flag
    std::vector<std::string> nativeHeaders;  // `import native` headers, unique
    bool isEntry = false;
    bool isPackage = false;
    bool topLevelAwait = false;
};

struct IncrementalUnitEntry {
    std::string unitKey;        // module path-ish logical name
    std::string sourceHash;     // hex fnv1a64 of the unit's source bytes
    std::string ihFingerprint;  // hex fnv1a64 of dep interface hashes
    std::string shapeHash;      // hex fnv1a64 of program-wide shapes
    std::string configHash;     // compilerKey + profile
    std::string interfaceHash;  // hex fnv1a64 of the unit's own surface
    ProgramUnitFragment fragment;
};

// The shared cache file for one project (lives in the .cp3/cache dir).
constexpr const char* kIncrementalCacheFile = "incremental-program-cache";
// The cache format version. Any change to this scheme rewrites the file.
constexpr const char* kIncrementalCacheFormat = "cp3-incremental-cache/3";

// Reads the cache. Returns false on any I/O/parse error (all units are then
// treated as fresh); empty cache files are a valid empty hit-set.
bool incrementalCacheLoad(const fs::path& cachePath,
                          std::vector<IncrementalUnitEntry>& out);

// Writes the cache atomically (temp + rename in the cache directory). Best
// effort: a file that cannot be written is not an error. A torn file simply
// retires every entry next run.
bool incrementalCacheStore(const fs::path& cachePath,
                           const std::vector<IncrementalUnitEntry>& entries);

// A unit's cache key and the fingerprint of its dependency interfaces.
std::string incrementalUnitKey(const CompilationUnit& unit);
std::string unitSourceHash(const CompilationUnit& unit);
std::string configHashOf(const std::string& compilerKey,
                         const std::string& profile);
std::string programShapeHash(const ModuleGraph& graph);

// fnv1a64 joined over the hex interface hashes of this unit's resolved
// local/package dependencies, in graph dependency declaration order.
std::string ihFingerprintOf(
    const ModuleGraph& graph, const CompilationUnit& unit,
    const std::unordered_map<std::uint32_t, std::uint64_t>& ihOf);

// fnv1a64 render of a value, matching the record format's hex fields.
std::string incrementalHex64(std::uint64_t value);

fs::path incrementalCachePathFor(const fs::path& entryFile);

// Computes the reuse decision for every unit and PRUNES the cache to the
// valid records (everything else is dropped). Entries whose shape/interface
// fingerprints are missing (fresh records written by codegen, which has no
// interface tables) are stamped with the current values. Returns the number
// of kept entries, 0 when the cache was unusable, or -1 when the rewrite
// failed (the next run regenerates).
int prepareIncrementalReuse(const fs::path& cachePath,
                            const ModuleGraph& graph,
                            const std::string& compilerKey,
                            const std::string& profile,
                            const std::unordered_map<std::uint32_t, std::uint64_t>& ihOf);

}  // namespace cp3::cli
