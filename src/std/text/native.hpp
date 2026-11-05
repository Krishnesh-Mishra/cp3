// =============================================================================
// src/std/text/native.hpp - `std/text` native backend row (D72).
//
// OWNERSHIP: the std/text family (src/std/text/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. The runtime is header-only (no runtime TU); the compiled dependency
// is simdutf for `TextDecoder.decode` validation: the vendored headers
// (third_party/simdutf/include) supply `simdutf::validate_utf8` and the
// vendored static archive `third_party/simdutf/lib/windows-x64/libsimdutf.a`
// is linked for `windows-x64` (third_party/MANIFEST.md "Verified consumer
// flags": `-I third_party/simdutf/include -L third_party/simdutf/lib/<triple>
// -lsimdutf`). A program that imports "std/text" gets the include root and
// the archive; other programs link nothing extra. No extra compile flags are
// needed (simdutf v6.1.0 requires only C++11); no `-pthread`/`-ldl` row is
// added because only the `windows-x64` archive is vendored — see LIMITATIONS
// below. The header (cp3/std/text.hpp) keeps its hand-written decoder as the
// fallback, so a build without `simdutf.h` on the include path still links
// and behaves identically (slower).
//
// The header (cp3/std/text.hpp) spells the `Uint8Array` value type from
// cp3/typed_array.hpp, so the typed-arrays include tree rides along - the
// same bundling std/fs uses for its std/buffer `Buffer` chunk
// (src/std/fs/native.hpp).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kTextNativePackage{
    "std/text",
    {"src/std/text/include", "src/std/typed-arrays/include",
     "third_party/simdutf/include"},
    {},
    {},
    // libDirs / linkFlags are supplied per-platform below (the crypto/fs
    // pattern): top level stays empty so a non-Windows target never tries
    // to link the windows-x64 COFF archive.
    {},
    {},
    // Parallel to runtimeSources (none): no runtime TU, so no markers.
    {},
    // Linux audit: docs/design/linux-crosscompat.md — no linux-x64 /
    // linux-arm64 libsimdutf.a vendored yet, so no linux platform row is
    // added here (a linux target fails loudly, never mislinks COFF).
    {{"windows-x64", {"third_party/simdutf/lib/windows-x64"}, {"-lsimdutf"}}},
};

// LIMITATIONS (simdutf wiring): only `windows-x64` ships an archive today
// (third_party/simdutf/lib/windows-x64/libsimdutf.a). `applyNativePackages`
// rejects a selected row with no matching platform row, so a program that
// imports "std/text" for a `linux-x64` / `linux-arm64` target fails with
// "native backend for 'std/text' has no vendor archives for target ..." until
// `linux-x64` / `linux-arm64` archives are vendored and added here next to
// the `windows-x64` row. That is an explicit build error, never a silent
// mislink or a quiet slow-path fallback. Header-only consumers (a TU that
// includes cp3/std/text.hpp without this row) degrade to the hand-written
// decoder via `__has_include(<simdutf.h>)` instead. Only UTF-8 validation is
// wired (validate_utf8); UTF-16/UTF-32/convert APIs are intentionally
// untouched — TextEncoder copies cp3 strings verbatim and TextDecoder is
// UTF-8-only by design (src/std/text/sema.cpp).

}  // namespace cp3::cli::detail
