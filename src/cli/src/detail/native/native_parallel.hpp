// =============================================================================
// src/cli/src/detail/native_parallel.hpp - core parallel.* backend row (spec 13).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// parallel.* is core language: it never appears in a program's import graph,
// so this row cannot be keyed off `ModuleDependency` like the std/... rows
// (native_packages.hpp). build.cpp computes `usesParallel` (the generated C++
// includes cp3/parallel/parallel.hpp, i.e. the program names the parallel
// family) and `selectCoreNativePackages` appends the row exactly when a
// program uses it. A program without parallel.* compiles the same runtime
// TUs as before and links no TBB archive.
//
// Backend: oneTBB v2023.1.0 (vendor/third_party/oneTBB), pinned static
// libtbb.a - vendor/third_party/MANIFEST.md "Verified consumer flags" and the
// "oneTBB v2023.1.0" build recipe. The parallel.* engine
// (src/runtime/src/parallel/parallel.cpp) schedules its chunks through TBB;
// that TU is selected by content in src/cli/src/toolchain/runtime/util.cpp
// (kRuntimeUnits: "parallel.cpp") and needs the oneTBB include root at
// compile time - this row supplies it, plus the -ltbb link line.
//
// Scope notes:
//   * The Worker/channel machinery (cp3/parallel/worker*.hpp) is NOT backed by
//     TBB: message passing keeps its FIFO drain on the event loop. A program
//     that only uses `new Worker(...)` names worker.hpp, never
//     cp3/parallel/parallel.hpp, so it links nothing new.
//   * `tbb::scalable_allocator` is opt-in upstream and is NOT enabled
//     anywhere: the runtime does not use scalable allocation, so -ltbbmalloc
//     is deliberately absent.
//   * No runtime sources of its own: parallel.cpp is a content-selected
//     runtime TU; listing it here too would compile it twice.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

// Verified against vendor/third_party/MANIFEST.md "Verified consumer flags":
//   compile: -std=c++20 -I vendor/third_party/oneTBB/include
//   link:    -L vendor/third_party/oneTBB/lib -ltbb
// The archive directory is per-target: windows-x64 keeps the original
// vendor/third_party/oneTBB/lib root, linux-x64 uses
// vendor/third_party/oneTBB/lib/linux-x64 (the linux archive is staged by the
// maintainer; until it lands the link fails closed with a clear diagnostic).
// Keeping the root out of the platform table stops a Linux build from ever
// picking up the Windows COFF archive. No darwin / arm64 rows exist yet: the
// driver reports "no vendor archives for target" there, same as any native
// row without a matching platform entry.
inline const NativePackage kParallelCoreNativePackage{
    "core/parallel",
    {"vendor/third_party/oneTBB/include"},
    {},
    {},
    {},
    {},
    {},
    {{"windows-x64", {"vendor/third_party/oneTBB/lib"}, {"-ltbb"}},
     {"linux-x64", {"vendor/third_party/oneTBB/lib/linux-x64"},
                   {"-ltbb", "-pthread"}}},
};

}  // namespace cp3::cli::detail
