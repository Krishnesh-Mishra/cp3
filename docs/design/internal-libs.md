# Ledger F — internal-only libraries

Solo inventory (no downloads, no builds, no `src/**` CMake edits). Sources:
`vendor/third_party/MANIFEST.md`, `vendor/third_party/build.ps1`, `CMakeLists.txt`,
`src/CMakeLists.txt`, `src/runtime/CMakeLists.txt`, and the `src/std/**`
headers. MANIFEST.md got a "Ledger F" section with the rows below; `build.ps1`
got a header note only.

## Inventory

| Library | Present in third_party | Evidence | Current stand-in in tree |
| --- | --- | --- | --- |
| libsodium (seed / CSPRNG) | PRESENT | `vendor/third_party/libsodium/` + MANIFEST pin 1.0.22; `src/std/crypto/runtime.cpp` uses `sodium_init`/`randombytes_buf` | — (done) |
| ankerl::unordered_dense | MISSING | no dir, no MANIFEST row, no CMake ref | `std::unordered_map` in `collections.hpp` (Map/Set), `std/io/runtime.cpp` |
| fast_float | MISSING | no dir/row | `std::strtod` in `json_parse.hpp`, typed-arrays, `std/network/runtime.cpp` |
| simdutf | MISSING | no dir/row; `std/text` header comment says "no simdutf dependency" | hand-validated UTF-8 decode; O(n) code-point scan in `string.hpp` `charAt` |
| miniz | MISSING | no dir/row; no `src/std/compress/` exists | nothing — uWS deflate off (`UWS_NO_ZLIB`, `DISABLED`) |
| moodycamel concurrentqueue | MISSING | no dir/row | no Channel family; `std/log` is synchronous `std::cout` |
| Boost.Lockfree | MISSING | no dir/row | same as above (alternative to moodycamel — vendor one first) |
| fmt | MISSING | no dir/row; `log.hpp` says "fmt is NOT bundled" | hand-written `renderValue` in `log.hpp` |
| XXHash (xxhash64) | MISSING | no dir/row; `hash.hpp`/`hash/sema.cpp` say BLOCKED | FNV-1a + hand-written CRC tables only |
| wyhash / XXH3 (bucket hash) | MISSING | no dir/row | `KeyHash` -> `std::hash` in `collections.hpp` |
| CRC32C vendored | NOT NEEDED | no dir/row, intentionally | correct table-driven `crc32c` in `hash.hpp`; vendor `google/crc32c` only for HW accel |

## MANIFEST changes

Added "Ledger F — internal-only libraries (PLANNED, not yet vendored)" section
to `vendor/third_party/MANIFEST.md`: one row per lib above (candidate upstream URL,
kind, hot path, status), plus wiring-note blocks (CMake include dirs for
header-only libs; `-I`/`-L`/`-l` per-triple rows for compiled libs). Rows are
explicitly **not pins** — no SHA/tag/`VERSION.txt` claimed. `vendor/third_party/build.ps1`
got a matching header comment; `$pins` unchanged so a re-run is unaffected.

## Wiring lines for the lead (land in src/** after vendoring)

Header-only — add to `cp3_runtime` PRIVATE includes in `src/runtime/CMakeLists.txt`:

```cmake
"${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/unordered_dense/include"
"${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/fast_float/include"
"${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/concurrentqueue/include"  # or boost/include
"${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/wyhash/include"           # if chosen
```

Compiled — add rows to each family's `native.hpp` (pattern: `std/crypto`,
`std/network`) with per-triple `lib/<triple>` dirs:

```
fmt:     -I vendor/third_party/fmt/include     -L vendor/third_party/fmt/lib/<triple>     -lfmt
simdutf: -I vendor/third_party/simdutf/include -L vendor/third_party/simdutf/lib/<triple> -lsimdutf
miniz:   -I vendor/third_party/miniz/include   -L vendor/third_party/miniz/lib/<triple>   -lminiz
xxhash:  -I vendor/third_party/xxhash/include  -L vendor/third_party/xxhash/lib/<triple>  -lxxhash
```

Order of first consumers: unordered_dense -> `collections.hpp` Map/Set index;
fast_float -> `json_parse.hpp` (then env/url); simdutf -> `std/text` + `charAt`;
fmt -> `log.hpp`/console; XXHash -> unblock `std/hash` xxhash64 + bucket hash;
miniz -> new `src/std/compress/**`; moodycamel/Boost -> new Channel + async
log sink. libsodium: no change.

## Limitations

- Nothing vendored: no downloads/builds were run, so no SHA, bytes, or smoke
  evidence exists for any Ledger F lib. Candidate URLs/tags above are from
  inventory knowledge and must be pinned + smoke-tested by the lead.
- Kind split matters: unordered_dense, fast_float, moodycamel, Boost.Lockfree,
  wyhash are header-only; simdutf, miniz, xxhash, crc32c, and fmt (unless
  `FMT_HEADER_ONLY`) need compiled per-triple static archives like
  libsodium/libuv — more build work, Windows/Linux x64/arm64 matrix.
- Choose one queue (moodycamel vs Boost.Lockfree) and one bucket hash
  (wyhash vs XXH3) first — shipping both doubles audit surface.
- wyhash licensing/versioning is less standard than XXHash (single-file
  releases, no stable ABI); XXH3 via XXHash may be the safer bucket-hash pick.
- simdutf needs a runtime SIMD-baseline decision (generic vs arch-specific
  build); miniz has no consumer until a `std/compress` family is specified.
- fmt header-only mode avoids an archive but inflates every TU; compiled
  `-lfmt` is the leaner default if `std/log`/console pay for it.
