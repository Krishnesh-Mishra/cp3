// =============================================================================
// src/cli/src/detail/source_closure.hpp - the source-package closure DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/pkg/manifest/source_closure.cpp.
//
// The `.cp3pak` of a `"type": "package"` project carries the entry and every
// module it reaches through relative imports (an import whose source is
// relative - `./x`, `../x`, or any `.cp3` specifier, which the module loader
// classifies as Local), resolved against the package root. Nothing else
// ships: std modules are not copied, and a reachable module whose file does
// not live under the package root is a hard error ("escapes the package
// root"), because the archive cannot carry a path outside itself.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "detail/archive/tar.hpp"

namespace cp3 {
class ModuleGraph;
}

namespace cp3::cli {

// Collects the entry's relative-import closure as archive members.
//
// `graph` is the module graph of the entry (every transitively reachable
// unit it holds is walked). `packageRoot` anchors the member paths, which
// are '/'-separated and relative to it, preserving the authored layout.
// Members are sorted by name, cycle-safe and deduplicated (the walk is a
// set-based closure over Local edges). Member data is the raw file bytes.
//
// Fails (returns false, `err` set) when a reachable unit's file does not
// live under `packageRoot` - the package cannot ship outside itself.
bool collectRelativeClosure(const ModuleGraph& graph,
                            const std::filesystem::path& packageRoot,
                            std::vector<TarEntry>& out, std::string& err);

}  // namespace cp3::cli
