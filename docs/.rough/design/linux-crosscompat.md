# Linux / Windows cross-compat audit — NativePackage platform rows

Solo audit (code only: no cmake/build/tests run, no commit). Scope of this
audit: every `NativePackage` platform row under `src/std/**/native.hpp`,
checked for `linux-x64` / `linux-arm64` coverage against the archives
actually present under `vendor/third_party/*/lib/<triple>/`.

Method: read every `src/std/**/native.hpp`, listed `vendor/third_party/*/lib/**`
on disk, and cross-checked `vendor/third_party/MANIFEST.md` (Pins / Delivered
artifacts / Ledger F). No archive was built or copied.

> **Update 2026-11-07:** the follow-up this audit called for has landed.
> `vendor/third_party/xxhash/lib/linux-x64/libxxhash.a` and
> `vendor/third_party/simdutf/lib/linux-x64/libsimdutf.a` now exist (ELF x64; byte
> counts and SHA-256 in MANIFEST.md), the `linux-x64` rows are wired in
> `std/hash`, `std/text` and `std/collections`, and PCRE2 gained a
> `linux-x64` archive and row alongside the existing Windows one. The
> tables and verdicts below describe the audit's original state; only the
> `linux-arm64` xxhash/simdutf archives remain missing (their rows stay
> absent so those targets fail loudly).

## Missing-triples table (per library)

| Library (pin) | windows-x64 archive | linux-x64 archive | linux-arm64 archive | Missing triples |
| --- | --- | --- | --- | --- |
| libsodium 1.0.22 | `vendor/third_party/libsodium/lib/windows-x64/libsodium.a` | `.../lib/linux-x64/libsodium.a` | `.../lib/linux-arm64/libsodium.a` | — (none) |
| libuv v1.53.0 | `vendor/third_party/libuv/lib/windows-x64/libuv.a` | `.../lib/linux-x64/libuv.a` | `.../lib/linux-arm64/libuv.a` | — (none) |
| uSockets (uWebSockets v20.80.0) | `vendor/third_party/uWebSockets/lib/windows-x64/libuSockets.a` | `.../lib/linux-x64/libuSockets.a` | `.../lib/linux-arm64/libuSockets.a` | — (none for the linked lib; see uWebSockets note) |
| uWebSockets (header-only) | `libuWebSockets.a` present (optional, never pulled) | **absent** | **absent** | linux-x64, linux-arm64 — **not missing in practice**: header-only, `-luWebSockets` is optional per MANIFEST; no NativePackage row links it on any triple |
| XXHash v0.8.3 | `vendor/third_party/xxhash/lib/windows-x64/libxxhash.a` | **absent** (no `lib/linux-*` dir) | **absent** | **linux-x64, linux-arm64** |
| simdutf v6.1.0 | `vendor/third_party/simdutf/lib/windows-x64/libsimdutf.a` | **absent** (no `lib/linux-*` dir) | **absent** | **linux-x64, linux-arm64** |

Un-tripled legacy copies (`vendor/third_party/libsodium/lib/libsodium.a`,
`vendor/third_party/libuv/lib/libuv.a`, `vendor/third_party/uWebSockets/lib/lib*.a`) are
the original Windows builds kept for compatibility (MANIFEST "Delivered
artifacts"); they are not Linux archives and must not be pointed at by a
linux row.

## Per-NativePackage verdict

| NativePackage (file) | Lib(s) | windows-x64 row | linux-x64 row | linux-arm64 row | Verdict |
| --- | --- | --- | --- | --- | --- |
| `std/crypto` (`src/std/crypto/native.hpp`) | libsodium | yes | yes | yes | ✅ Covered — no change (not in this audit's editable set) |
| `std/network` (`src/std/network/codegen/native.hpp`) | uSockets + libuv | yes | yes | yes | ✅ Covered — `-luSockets -luv -pthread -ldl -lrt` on both linux rows; no change (not editable here) |
| `std/readline` (`src/std/readline/native.hpp`) | libuv | yes | yes | yes | ✅ Covered — no change (not editable here) |
| `std/io`, `std/fs`, `std/os`, `std/process` (libuv rows, same pattern) | libuv | yes | yes | yes | ✅ Covered — audited for completeness; no change (not editable here) |
| `std/collections` (`src/std/collections/native.hpp`) | libsodium + **xxhash** | yes (both) | **libsodium only** | **libsodium only** | ⚠️ **Partial**: linux rows exist but lack the xxhash component — a linux Map/Set program fails final link with undefined `XXH3_64bits_withSeed` |
| `std/hash` (`src/std/hash/native.hpp`) | **xxhash** | yes | **missing** | **missing** | ❌ **Missing both linux triples**: linux target fails loudly via `NativePackage::platforms` ("has no vendor archives") |
| `std/text` (`src/std/text/native.hpp`) | **simdutf** | yes | **missing** | **missing** | ❌ **Missing both linux triples**: same loud failure; header-only consumers fall back to the hand-written decoder via `__has_include(<simdutf.h>)` |
| All other `std/*` rows (buffer, console, date, env, json, log, math, path, string, time, typed-arrays, url, network/express/*, …) | none / header-only | n/a (`platforms` empty) | n/a | n/a | ✅ Triple-agnostic — no archive, nothing missing |

## What was (and was not) changed in `native.hpp`

No linux platform row was added for xxhash or simdutf, because **no linux
archive exists to point it at**. Adding a row such as
`{"linux-x64", {"vendor/third_party/xxhash/lib/linux-x64"}, {"-lxxhash"}}` today
would point `-L` at a non-existent directory and turn the current explicit
"no vendor archives for target" error into a confusing `-lxxhash not found`
link error; pointing at the windows-x64 COFF archive would be worse
(silent wrong-object risk) and is explicitly forbidden by the existing row
comments. The same applies to simdutf.

The three editable files (`src/std/collections/native.hpp`,
`src/std/hash/native.hpp`, `src/std/text/native.hpp`) therefore keep their
existing platform rows unchanged in semantics; each gained only a one-line
pointer in the platform-row comment above the row to this audit doc
(`docs/.rough/design/linux-crosscompat.md`), so the next person to vendor a linux
archive knows exactly which rows to extend. `std/collections` linux rows
already exist for libsodium and must gain the xxhash lib dir + `-lxxhash`
**only once** `libxxhash.a` lands for that triple (see below).

## Placeholder READMEs (specified, not created)

The task asks for a placeholder README where an archive is missing. This
solo audit's file scope is limited to this doc plus the three `native.hpp`
platform rows, so the READMEs below are **specified here ready-to-drop,
not created** under `vendor/third_party/` — creating them (plus the empty
`lib/linux-*` dirs) is a one-step follow-up for whoever vendors the
archives. Each README should sit next to the future archive:

- `vendor/third_party/xxhash/lib/linux-x64/README.md` and
  `vendor/third_party/xxhash/lib/linux-arm64/README.md`
- `vendor/third_party/simdutf/lib/linux-x64/README.md` and
  `vendor/third_party/simdutf/lib/linux-arm64/README.md`

Ready-to-drop content (xxhash; simdutf analogous with its names/sources):

```markdown
# Placeholder — no linux-<arch> archive vendored yet

Expected: `libxxhash.a` (XXHash v0.8.3, pinned in
`vendor/third_party/xxhash/VERSION.txt`), built from the vendored single TU
`vendor/third_party/xxhash/include/xxhash.c` (`XXH_IMPLEMENTATION`) with a native
<arch> `gcc -O3 -std=c99` + `ar`/`ranlib` — see
`vendor/third_party/xxhash/build-linux.sh` (code only; not yet run on Linux).

Do NOT point a NativePackage linux row at this dir until `libxxhash.a`
exists here; do NOT reuse `lib/windows-x64/libxxhash.a` (COFF).
Once built: record bytes + SHA-256 in `vendor/third_party/MANIFEST.md`, then add
the linux row to `src/std/hash/native.hpp` / `src/std/collections/native.hpp`.
Audit: `docs/.rough/design/linux-crosscompat.md`.
```

For simdutf the expected artefact is `libsimdutf.a` (v6.1.0,
`vendor/third_party/simdutf/VERSION.txt`); note simdutf is **not** single-TU
(`lib/windows-x64/` carries `simdutf.o` built from the full upstream
CMake build — there is no vendored `.c` amalgamation under
`vendor/third_party/simdutf/include/`), so its linux build needs the upstream
CMake recipe (or a system `libsimdutf-dev` cross-check) rather than the
xxhash one-liner, plus a SIMD-baseline decision (generic vs arch-specific;
see `docs/.rough/design/internal-libs.md` limitations). Same row target:
`src/std/text/native.hpp`, plus `-pthread`/`-ldl` only if the linux build
actually needs them (verify from real link errors, per MANIFEST practice).

## Wiring lines to land once archives exist

```cpp
// std/hash + std/collections (xxhash), per triple <triple> = linux-x64 | linux-arm64:
{"linux-x64",   {"vendor/third_party/xxhash/lib/linux-x64"},   {"-lxxhash"}},   // + libsodium dir/-lsodium in collections
{"linux-arm64", {"vendor/third_party/xxhash/lib/linux-arm64"}, {"-lxxhash"}},
// std/text (simdutf):
{"linux-x64",   {"vendor/third_party/simdutf/lib/linux-x64"},   {"-lsimdutf"}},
{"linux-arm64", {"vendor/third_party/simdutf/lib/linux-arm64"}, {"-lsimdutf"}},
```

## Limitations

- **No builds/tests were run** (per task scope): archive presence is by
  file listing + MANIFEST SHA table only; the linux libsodium/libuv/uSockets
  archives were not re-verified (no `file`/`ar` inspection, no smoke link)
  in this audit.
- `vendor/third_party/xxhash/build-linux.sh` exists in the working tree (untracked,
  another wave's work) and documents the xxhash linux recipe; it has not
  been run, so no `lib/linux-*` dir/archive exists yet. No equivalent
  script was found for simdutf.
- uWebSockets `libuWebSockets.a` is absent for linux triples; this is
  documented as non-blocking (header-only, optional link) but a future
  consumer that does link `-luWebSockets` would need a linux build of that
  stub TU (MANIFEST documents the recipe).
- Header-only rows (`platforms` empty) were assumed triple-agnostic from
  their row shape; their runtime TUs were not audited for
  Windows-only APIs in this pass.
- Working tree already contained concurrent uncommitted changes to the
  three editable `native.hpp` files (xxhash/simdutf vendoring wave); this
  audit edited only the platform-row comments on top of that state and
  left all other hunks untouched.
