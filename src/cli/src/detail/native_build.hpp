// =============================================================================
// src/cli/src/detail/native_build.hpp - the native binding build integration.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// The declarations live in src/cli/src/commands/build_native.cpp (STYLE.md §3:
// headers declare, `.cpp` defines).
//
// A native binding records what its C++ library needs: the include roots, the
// preprocessor defines, the library directories, the libraries to link and the
// runtime DLLs the executable must find next to it. This integration adds
// those to the build, for the incremental profile and for `--release` alike.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/native_bindings.hpp"

#include "detail/cache.hpp"

namespace cp3::cli::detail {

// The flags a native backend contributes to one build (DECISIONS D68).
struct NativeBuildFlags {
    std::vector<std::string> compile;
    std::vector<std::string> link;
};

// Adds every binding's include roots, defines, library directories and link
// names to the build, and copies the recorded runtime DLLs next to the
// executable (unless package.json's "build" section sets
// "bundleNativeDlls" to false). `msvc` selects the MSVC spelling of the link
// flags (/LIBPATH: and <name>.lib instead of -L and -l). Returns false after
// reporting a problem.
bool applyNativeBindings(const std::vector<NativeBinding>& bindings,
                         BuildLayout& layout, NativeBuildFlags& flags,
                         bool msvc);

}  // namespace cp3::cli::detail
