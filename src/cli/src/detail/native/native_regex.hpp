// =============================================================================
// src/cli/src/detail/native_regex.hpp - core regex literal backend row (D48).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Regex literals are core language: they never appear in a program's import
// graph, so this row cannot be keyed off `ModuleDependency` like the
// std/... rows (native_packages.hpp). It is selected from the generated
// module's own content instead: build.cpp computes `usesRegex` (the emitted
// C++ includes cp3/core/regex.hpp) and `selectCoreNativePackages` appends the
// row exactly when a program uses a regex literal or regex value. A program
// without regexes keeps compiling the same runtime TUs as before and links no
// PCRE2 archive (D68 "no cost when unused").
//
// Backend: PCRE2 10.49 (vendor/third_party/pcre2), pinned static 8-bit build -
// DECISIONS D70 + vendor/third_party/MANIFEST.md "Verified consumer flags".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

// Verified against vendor/third_party/MANIFEST.md "Verified consumer flags":
//   compile: -DPCRE2_STATIC=1 -DPCRE2_CODE_UNIT_WIDTH=8 -I vendor/third_party/pcre2/include
//   link:    -L <archive dir> -lpcre2-8
// The archive directory is per-target: windows-x64 keeps the original
// vendor/third_party/pcre2/lib root, linux-x64 uses vendor/third_party/pcre2/lib/linux-x64.
// Keeping the root out of the platform table stops a Linux build from ever
// picking up the Windows COFF archive. The runtime include root
// (src/runtime/include, which owns cp3/core/regex.hpp) is always in
// compilerIncludeRoots(); the row only supplies the vendored PCRE2 header tree
// and the two defines pcre2.h requires at its own include site.
inline const NativePackage kRegexCoreNativePackage{
    "core/regex",
    {"vendor/third_party/pcre2/include"},
    {"-DPCRE2_STATIC=1", "-DPCRE2_CODE_UNIT_WIDTH=8"},
    {"src/runtime/src/core/regex.cpp"},
    {},
    {},
    {},
    {{"windows-x64", {"vendor/third_party/pcre2/lib"}, {"-lpcre2-8"}},
     {"linux-x64", {"vendor/third_party/pcre2/lib/linux-x64"}, {"-lpcre2-8"}}},
};

}  // namespace cp3::cli::detail
