// =============================================================================
// src/cli/src/detail/core/runtime_snapshot.hpp - runtime source mirror
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// `cp3c build/run --show-runtime` copies the runtime and stdlib source trees
// into `<buildDir>/_runtime/` as a SHOW-ONLY inspection aid, laid out exactly
// like the repository. The mirror is created only when it does not already
// exist; an existing mirror is never re-copied, merged or rewritten, and the
// build never compiles from it. A README.md placed in the mirror explains
// that editing the files there changes nothing. `cp3c clean` removes the
// mirror with the rest of .cp3/build.
// =============================================================================
#pragma once

#include <filesystem>

#include "detail/cache/cache.hpp"

namespace cp3::cli {

// Materializes `<buildDir>/_runtime/` for `cp3c build/run --show-runtime`.
// Creates the mirror (repository-relative layout) and its README.md when the
// directory does not exist yet; otherwise returns the existing mirror without
// touching it (the README.md is ensured in that case too). Returns an empty
// path after reporting a failure. `verbose` lists every copied file.
std::filesystem::path copyRuntimeSnapshot(const BuildLayout& layout,
                                          bool verbose);

}  // namespace cp3::cli
