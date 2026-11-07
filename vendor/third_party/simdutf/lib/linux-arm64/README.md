# simdutf linux-arm64 — archive pending

`libsimdutf.a` for `linux-arm64` has **not been built yet** (no Linux
toolchain on the authoring host; see
`docs/design/linux-packaging-simdutf.md`).

Produce it with (from the repo root), on a native arm64 Linux host:

```sh
vendor/third_party/simdutf/build-linux-simdutf.sh --arch arm64
```

or from Linux x64 with an `aarch64-linux-gnu` cross toolchain
(`aarch64-linux-gnu-g++`, `aarch64-linux-gnu-ar` — override `CXX`/`AR`/
`RANLIB` in the environment if they live elsewhere):

```sh
vendor/third_party/simdutf/build-linux-simdutf.sh --arch arm64
```

which compiles the pinned upstream single translation unit
(`src/simdutf.cpp`, simdutf v6.1.0, commit
`7852150e454ebacbf7e65cbf2c5e3feac5e660ec`) with
`g++ -O3 -std=c++17 -I include -I src` and writes:

```
vendor/third_party/simdutf/lib/linux-arm64/libsimdutf.a
vendor/third_party/simdutf/lib/linux-arm64/VERSION.txt
```

Until the archive lands, consumers of `std/text` on `linux-arm64` fail
fast at native-package resolution ("no vendor archives for target")
rather than linking the Windows COFF archive. Header-only consumers
(`cp3/std/text.hpp` without the native row) keep the hand-written
decoder via `__has_include(<simdutf.h>)`.

This placeholder is deleted by the build script on success.
