// =============================================================================
// src/cli/src/detail/core/runtime_snapshot.hpp - runtime source snapshot
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// `cp3c build/run --show-runtime` writes a readable copy of the runtime
// headers and the core runtime translation units this build selected. The
// normal build keeps using the shipped include trees and sources; the snapshot
// is an inspection aid, not a second build input.
// =============================================================================
#pragma once

#include <filesystem>

#include "detail/cache/cache.hpp"

namespace cp3::cli {

// Copies the runtime support used by `layout` into
// `<buildDir>/core/cp3-runtime/`, mirroring the `cp3/` include tree without its
// prefix. Returns the snapshot root, or an empty path after reporting a
// failure. `verbose` also lists every copied file.
std::filesystem::path copyRuntimeSnapshot(const BuildLayout& layout,
                                          bool verbose);

}  // namespace cp3::cli
