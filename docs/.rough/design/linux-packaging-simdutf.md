# Linux packaging — simdutf

Status: **recipe + wiring ready, archives pending.** The Linux static
archives (`linux-x64`, `linux-arm64`) have not been built (no Linux
g++ toolchain on the authoring host; no build was run for this
change). What lands now is the reproducible recipe, the script that
runs it on a Linux host, the placeholder layout, and the exact
MANIFEST / `native.hpp` rows to apply once the archives exist.

> **Update 2026-11-07:** the `linux-x64` archive now exists.
> `libsimdutf.a` was built in WSL2 Ubuntu (783928 bytes, SHA-256
> `27B5E746...042FE11`; full value in MANIFEST.md), the
> `src/std/text/native.hpp` `linux-x64` row is wired, and the package
> script stages the linux-x64 archive. `linux-arm64` remains pending and
> keeps its placeholder README with no platform row.

Scope of this change (only these paths were touched):

- `vendor/third_party/simdutf/build-linux-simdutf.sh` (new) — Linux build
- `vendor/third_party/simdutf/lib/linux-x64/README.md` (new) — placeholder
- `vendor/third_party/simdutf/lib/linux-arm64/README.md` (new) — placeholder
- `vendor/third_party/build.ps1` — new §11 (dirs + recipe pointer; see below)
- `docs/.rough/design/linux-packaging-simdutf.md` (new) — this document

Explicitly **not** touched (out of scope, wiring listed verbatim
below): `vendor/third_party/MANIFEST.md`, `src/std/text/native.hpp`,
`vendor/third_party/build-linux.sh`.

## Pin

| library | repo URL | tag | commit (full SHA) |
| --- | --- | --- | --- |
| simdutf | https://github.com/simdutf/simdutf.git | v6.1.0 | 7852150e454ebacbf7e65cbf2c5e3feac5e660ec |

Same pin as `vendor/third_party/simdutf/VERSION.txt`, the `$pins` table in
`vendor/third_party/build.ps1`, and `vendor/third_party/MANIFEST.md` Ledger F.
License: Apache-2.0 (files already vendored:
`vendor/third_party/simdutf/LICENSE-APACHE`, `LICENSE-MIT`).

## Layout

```
vendor/third_party/simdutf/
  include/                       public headers (shared by all triples, already vendored)
  lib/windows-x64/libsimdutf.a   existing (COFF, MinGW — do not link on Linux)
  lib/linux-x64/libsimdutf.a     <-- produced by build-linux-simdutf.sh --arch x64
  lib/linux-arm64/libsimdutf.a   <-- produced by build-linux-simdutf.sh --arch arm64
  VERSION.txt                    top-level pin (already vendored)
  build-linux-simdutf.sh         recipe
```

Only the archive + a per-dir `VERSION.txt` land per Linux triple;
headers stay single-sourced under `include/` (no per-arch header
copies). The Windows `simdutf.o` sitting next to the archive is a
leftover of the original build; the Linux recipe does **not** copy
the `.o` into the deliverable dir.

## Build recipe (Linux)

simdutf v6.1.0 ships an amalgamated single translation unit,
`src/simdutf.cpp`. No CMake, no per-arch file list.

```sh
# from the repo root, on a Linux x86_64 host (or native arm64 for arm64):
vendor/third_party/simdutf/build-linux-simdutf.sh --arch x64     # or --arch arm64

# equivalent manual form (what the script runs, after checkout at the pin):
g++ -O3 -std=c++17 -I <src>/include -I <src>/src -c <src>/src/simdutf.cpp -o simdutf.o
ar rcs libsimdutf.a simdutf.o
ranlib libsimdutf.a
cp libsimdutf.a vendor/third_party/simdutf/lib/linux-x64/   # (or linux-arm64/)
```

Details that matter:

- **Include order `-I include -I src`.** The public headers
  (`simdutf.h`, `simdutf/*.h`) come first; `-I src` is needed for the
  internal headers (`simdutf/internal/isadetection.h`) the TU pulls in.
- **No `-march=` / `-mtune=native`.** simdutf does runtime CPU
  detection and picks the SIMD kernel; a host-tuned baseline would
  produce an archive that SIGILLs on older machines. Same flag set
  for both arches keeps the two archives reproducible from one recipe.
- **arm64 cross from x64** needs `aarch64-linux-gnu-g++` /
  `aarch64-linux-gnu-ar` / `aarch64-linux-gnu-ranlib` (override
  `CXX`/`AR`/`RANLIB`). The script refuses `/mnt/c/...` Windows
  tools imported by WSL, mirroring `vendor/third_party/build-linux.sh`.
- **No extra link flags** for consumers beyond `-lsimdutf`: no
  `-pthread`, no `-ldl` (only `validate_utf8` is wired — see
  Limitations). Consumer line is unchanged apart from the `<triple>`
  segment: `-I vendor/third_party/simdutf/include -L vendor/third_party/simdutf/lib/<triple> -lsimdutf`.
- The script deletes the per-dir `README.md` placeholder on success
  and writes `VERSION.txt` in the same `key=value` shape as the other
  Linux vendor dirs: `simdutf=<SHA> arch=<x64|arm64> built=<UTC ISO>`.

## MANIFEST rows (apply to `vendor/third_party/MANIFEST.md`, not done here)

Once the two archives exist, fill in bytes + SHA-256 (never guess)
and apply these rows:

1. Pins table — add after the PCRE2 row:

```markdown
| simdutf | https://github.com/simdutf/simdutf.git | v6.1.0 | 7852150e454ebacbf7e65cbf2c5e3feac5e660ec |
```

2. Delivered artifacts table — add:

```markdown
| simdutf/lib/windows-x64/libsimdutf.a | TBD | TBD |
| simdutf/lib/linux-x64/libsimdutf.a | TBD | TBD |
| simdutf/lib/linux-arm64/libsimdutf.a | TBD | TBD |
```

3. Licenses table — add (files already present):

```markdown
| simdutf/LICENSE-APACHE | Apache-2.0 |
| simdutf/LICENSE-MIT | MIT |
```

4. Verified consumer flags table — add:

```markdown
| std/text (simdutf) | -I vendor/third_party/simdutf/include | -L vendor/third_party/simdutf/lib/<triple> -lsimdutf |
```

5. Ledger F — update the simdutf row status from
   `VENDORED (windows-x64 .a)` to
   `VENDORED (windows-x64 + linux-x64 + linux-arm64 .a)`.

## native.hpp wiring (apply to `src/std/text/native.hpp`, not done here)

Current platform map (line 44):

```cpp
{{{"windows-x64", {"vendor/third_party/simdutf/lib/windows-x64"}, {"-lsimdutf"}}}},
```

Target platform map — add the two Linux rows next to the Windows one:

```cpp
{{{"windows-x64", {"vendor/third_party/simdutf/lib/windows-x64"}, {"-lsimdutf"}}},
 {"linux-x64",   {"vendor/third_party/simdutf/lib/linux-x64"},   {"-lsimdutf"}},
 {"linux-arm64", {"vendor/third_party/simdutf/lib/linux-arm64"}, {"-lsimdutf"}}}},
```

Also update the `// LIMITATIONS` comment block (lines 47–59): the
"only `windows-x64` ships an archive today" paragraph no longer
holds; replace it with the new constraint set (see Limitations
below). Nothing else in `native.hpp` changes: include dirs already
carry `vendor/third_party/simdutf/include`, and per-platform `libDirs` /
`linkFlags` is exactly where new triples belong (the crypto/fs
pattern). Until the archives land, `applyNativePackages` keeps
failing Linux `std/text` programs fast with "no vendor archives for
target" — the intended behaviour, not a silent COFF mislink.

## build.ps1 (§11, landed)

`vendor/third_party/build.ps1` gains a new trailing section that creates
`vendor/third_party/simdutf/lib/linux-x64` and `.../linux-arm64` and warns
when `libsimdutf.a` is absent for a triple. It deliberately does
**not** cross-compile ELF archives from the Windows host — the
authoritative producer is `vendor/third_party/simdutf/build-linux-simdutf.sh`
run on a Linux host (or from the Linux step of the pipeline). The
existing `$pins` row for simdutf (v6.1.0 / `7852150e…`) already
exists and was left as-is.

## Limitations

1. **Archives not built in this change.** No cmake/build/tests were
   run. `lib/linux-x64/` and `lib/linux-arm64/` contain only
   `README.md` placeholders. Linux `std/text` native programs keep
   failing fast until a Linux host runs the recipe and the
   `native.hpp` rows above land.
2. **No smoke evidence yet.** When built, verify with a program that
   includes `<simdutf.h>`, calls `simdutf::validate_utf8` on valid +
   invalid UTF-8, and links with ONLY
   `vendor/third_party/simdutf/include` + the per-arch archive (dynamic,
   `-static-libgcc -static-libstdc++`, and `-static` variants, same
   matrix as `vendor/third_party/build.ps1` §10), then record bytes +
   SHA-256 into the MANIFEST rows above.
3. **Baseline, not tuned.** `-O3` only; no `-march=native`. If
   `std/text` throughput ever matters, the knob is a `-march=`
   variant is a deliberate, separately-named triple — not a different source file.
4. **Only `validate_utf8` is wired** (`TextDecoder.decode`);
   UTF-16/UTF-32/convert APIs are intentionally untouched
   (`src/std/text/sema.cpp`). Header-only consumers keep the
   hand-written decoder via `__has_include(<simdutf.h>)`.
5. **No `-pthread`/`-ldl` rows** — simdutf needs neither for
   validation. Do not add them when wiring the Linux rows.
6. **NOT covered here:** `vendor/third_party/build-linux.sh` (still builds
   only libsodium/libuv/uSockets) — wiring simdutf into it is a
   one-line addition later, or it stays as the standalone
   `build-linux-simdutf.sh`. `oneTBB`/`PCRE2` Linux archives are
   separate gaps, untouched.
