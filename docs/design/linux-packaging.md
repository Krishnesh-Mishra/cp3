# Linux xxHash packaging (solo, code only)

Solo prep on a non-Linux (Windows) host. **Nothing was built, run, or
tested**: no cmake/gcc/ar/smoke ran, no archive bytes exist for Linux,
no `src/**` file was edited, nothing was committed or cloned. This
doc plus the files below are the complete deliverable; the archives,
their bytes/SHA-256, and the `native.hpp` wiring are follow-up steps
that must happen on (or cross-compiling for) Linux.

> **Update 2026-11-07:** the follow-up landed. `libxxhash.a` for
> `linux-x64` was built in WSL2 Ubuntu (67096 bytes, SHA-256
> `3DA25C3E...5CB15B83`; full value in MANIFEST.md), the placeholder
> README now records the real archive, and the `linux-x64` rows are wired
> in `src/std/hash/native.hpp` and `src/std/collections/native.hpp`.
> `linux-arm64` still has no archive and no row.

## Files created / edited in this change

| file | action |
| --- | --- |
| `third_party/xxhash/build-linux.sh` | created — the build recipe (below) |
| `third_party/xxhash/lib/linux-x64/README.md` | created — placeholder (no `libxxhash.a` yet) |
| `third_party/xxhash/lib/linux-arm64/README.md` | created — placeholder (no `libxxhash.a` yet) |
| `third_party/build.ps1` | edited — header + commented Linux-xxHash section only (no logic change; see §5) |
| `docs/design/linux-packaging.md` | created — this file |

Not touched (out of scope, listed as wiring instead): `third_party/MANIFEST.md`
(§3 gives the exact rows to add), `third_party/build-linux.sh` (§5),
`src/std/hash/native.hpp` and `src/std/collections/native.hpp` (§4).

## Pin (already vendored, unchanged)

From `third_party/xxhash/VERSION.txt` (Ledger F, MANIFEST.md §"Ledger F"):

```
https://github.com/Cyan4973/xxHash.git v0.8.3 e626a72bc2321cd320e953a0ccf1584cad60f363 2026-11-05
```

| library | repo URL | tag | commit (full SHA) |
| --- | --- | --- | --- |
| XXHash (XXH32/64/3) | https://github.com/Cyan4973/xxHash.git | v0.8.3 | e626a72bc2321cd320e953a0ccf1584cad60f363 |

License: BSD-2-Clause (`third_party/xxhash/LICENSE`, already present).
Already delivered: `include/{xxhash.h,xxh3.h,xxhash.c,xxh_x86dispatch.h,xxh_x86dispatch.c}`
and `lib/windows-x64/{libxxhash.a,xxhash.o}`. The vendored
`include/xxhash.c` is the single TU (`XXH_IMPLEMENTATION` +
`#include "xxhash.h"`), so the Linux recipe below compiles the vendored
source directly — no clone is needed, and the source is byte-identical
to the one the windows-x64 archive was built from.

## 1. Build recipe (Linux gcc -O3, single-TU)

Script: `third_party/xxhash/build-linux.sh` (run from the repo root;
`--arch` defaults to the host `uname -m`, `x86_64→x64`, `aarch64→arm64`).

### linux-x64 (native x64 Linux host)

```sh
third_party/xxhash/build-linux.sh --arch x64
# i.e. CC=/usr/bin/gcc AR=/usr/bin/ar RANLIB=/usr/bin/ranlib:
gcc -O3 -std=c99 -Wall -c third_party/xxhash/include/xxhash.c -o xxhash.o
ar rcs libxxhash.a xxhash.o
ranlib libxxhash.a
cp libxxhash.a third_party/xxhash/lib/linux-x64/libxxhash.a
cp third_party/xxhash/VERSION.txt third_party/xxhash/lib/linux-x64/VERSION.txt
```

### linux-arm64 (native aarch64 host, or x64 + cross toolchain)

Prerequisite on x64: an `aarch64-linux-gnu` cross toolchain
(e.g. `apt install gcc-aarch64-linux-gnu`), mirroring
`third_party/build-linux.sh`'s arm64 handling.

```sh
third_party/xxhash/build-linux.sh --arch arm64
# i.e. CC=/usr/bin/aarch64-linux-gnu-gcc AR=/usr/bin/aarch64-linux-gnu-ar
#      RANLIB=/usr/bin/aarch64-linux-gnu-ranlib:
aarch64-linux-gnu-gcc -O3 -std=c99 -Wall -c third_party/xxhash/include/xxhash.c -o xxhash.o
aarch64-linux-gnu-ar rcs libxxhash.a xxhash.o
aarch64-linux-gnu-ranlib libxxhash.a
cp libxxhash.a third_party/xxhash/lib/linux-arm64/libxxhash.a
cp third_party/xxhash/VERSION.txt third_party/xxhash/lib/linux-arm64/VERSION.txt
```

Flag notes (all deliberate, matching the libsodium/libuv Linux pattern
in `third_party/build-linux.sh`):

- `-O3` as specified; `-std=c99 -Wall` (xxHash is C99; this is the
  upstream-supported dialect for `xxhash.c`).
- **No `-march=native`/`-mcpu=native`**: bakes the build host's CPU into
  the archive and breaks cross builds. The generic x86-64 / arm64
  baselines are correct; `xxhash.h` selects XXH3 SIMD variants from the
  target baseline, so no `-mavx*`/`-msse*` flags are passed.
- **No `-fPIC`**: same as the libsodium/libuv Linux archives (static,
  linked into executables). If cp3 ever links these into a shared
  object, rebuild with `-fPIC` and re-record the MANIFEST rows.
- The script rejects Windows-mounted (`/mnt/c/...`) `CC`/`AR`/`RANLIB`
  inherited from WSL, exactly like `third_party/build-linux.sh`
  (§"WSL can import Windows environment variables"), and overrides them
  with the per-arch defaults.
- Rebuilds are not byte-identical (`ar` records object timestamps), so
  the MANIFEST SHA-256 identifies the delivered bytes, not a rebuild.

### Vendor tree after building

```
third_party/xxhash/
  VERSION.txt                                  # existing pin line
  LICENSE                                      # existing (BSD-2-Clause)
  build-linux.sh                               # recipe (this change)
  include/{xxhash.h,xxh3.h,xxhash.c,xxh_x86dispatch.h,xxh_x86dispatch.c}  # existing
  lib/windows-x64/{libxxhash.a,xxhash.o}       # existing
  lib/linux-x64/{libxxhash.a,VERSION.txt}      # NEW (placeholder README.md until built)
  lib/linux-arm64/{libxxhash.a,VERSION.txt}    # NEW (placeholder README.md until built)
```

## 2. MANIFEST rows (proposed — do not land until bytes exist)

Scope note: `third_party/MANIFEST.md` was outside this change's file
scope, so the rows below are **proposed text for the lead to merge**,
following the exact three-table shape used by libsodium (Pins /
Delivered artifacts / Licenses). Fill `<bytes>`/`<SHA-256>` from the
built archives (`wc -c` / `sha256sum`) — they are placeholders, not
measurements.

**Append to the Pins table** (after the PCRE2 row):

```
| XXHash (XXH32/64/3) | https://github.com/Cyan4973/xxHash.git | v0.8.3 | e626a72bc2321cd320e953a0ccf1584cad60f363 |
```

**Append to the Delivered artifacts table** (after the uWebSockets
linux-arm64 row, mirroring the libsodium archive pattern
`lib/linux-<arch>/<lib>.a`):

```
| xxhash/lib/windows-x64/libxxhash.a | <bytes — already delivered, fill from existing file> | <SHA-256> |
| xxhash/lib/linux-x64/libxxhash.a | <bytes> | <SHA-256> |
| xxhash/lib/linux-arm64/libxxhash.a | <bytes> | <SHA-256> |
```

(The windows-x64 row is a gap in the current table — it should be
filled from the already-delivered archive while adding the Linux rows.
There is no flat `xxhash/lib/libxxhash.a` and none is proposed: unlike
libsodium, xxhash has no legacy flat path, and the rows in
`src/std/hash/native.hpp` already use `lib/<triple>` only.)

**Append to the Licenses table:**

```
| xxhash/LICENSE | BSD-2-Clause |
```

**Add a "Build commands" subsection** (after the PCRE2 subsection),
with the §1 commands verbatim. Also update the Ledger F status line
that currently says "linux-x64/arm64 archives follow the same
single-TU pattern (documented in build.ps1 pins)" to point at
`third_party/xxhash/build-linux.sh` + this doc once the archives land.
Ledger F's compiled-static wiring note already names the consumer
flags; no change needed there beyond the rows above.

## 3. Verified consumer flags (same as windows, per triple)

```
compile: -I third_party/xxhash/include
link:    -L third_party/xxhash/lib/<triple> -lxxhash     # <triple> ∈ {windows-x64, linux-x64, linux-arm64}
```

Consumers must not define `XXH_STATIC_LINKING_ONLY`, `XXH_PRIVATE_API`,
or `XXH_INLINE_ALL` (the last is incompatible with linking
`libxxhash.a`; see `docs/design/xxhash64.md`).

## 4. native.hpp wiring lines (NOT applied — out of scope)

Do not land these until the corresponding `libxxhash.a` exists in the
tree; until then a linux row would point the linker at a directory
containing only a README. The windows-x64-only state documented in
both headers (and their "do not point linux rows at the windows
archive" warnings) stays correct until then.

### src/std/hash/native.hpp (currently windows-x64 only)

Replace the platform list and the header comment:

```cpp
// was:
    {{"windows-x64", {"third_party/xxhash/lib/windows-x64"}, {"-lxxhash"}}},
// becomes:
    {{"windows-x64", {"third_party/xxhash/lib/windows-x64"}, {"-lxxhash"}},
     {"linux-x64", {"third_party/xxhash/lib/linux-x64"}, {"-lxxhash"}},
     {"linux-arm64", {"third_party/xxhash/lib/linux-arm64"}, {"-lxxhash"}}},
```

and replace the comment paragraph "Only the windows-x64 archive is
vendored so far … Do not add linux platform rows pointing at the
windows archive." with: "All three archives are vendored
(windows-x64 COFF, linux-x64 / linux-arm64 ELF; see
third_party/MANIFEST.md and docs/design/linux-packaging.md). Do not
add platform rows pointing at another triple's archive."

### src/std/collections/native.hpp (linux rows currently libsodium-only)

Replace the two linux rows:

```cpp
// was:
     {"linux-x64", {"third_party/libsodium/lib/linux-x64"}, {"-lsodium"}},
     {"linux-arm64", {"third_party/libsodium/lib/linux-arm64"}, {"-lsodium"}}},
// becomes:
     {"linux-x64", {"third_party/libsodium/lib/linux-x64",
                    "third_party/xxhash/lib/linux-x64"}, {"-lsodium", "-lxxhash"}},
     {"linux-arm64", {"third_party/libsodium/lib/linux-arm64",
                      "third_party/xxhash/lib/linux-arm64"}, {"-lsodium", "-lxxhash"}}},
```

and update the comment ("The linux-x64 / linux-arm64 rows stay
libsodium-only until their xxhash archives are vendored …") to state
that the linux rows now carry `-lxxhash` like the windows-x64 row.
This closes the documented failure mode (undefined reference to
`XXH3_64bits_withSeed` in a linux Map/Set program).

## 5. What build.ps1 does and does not do

- `third_party/build.ps1` is the **Windows** rebuild. It already pins
  xxHash (v0.8.3 / `e626a72…`) and builds the windows-x64 archive;
  this change adds only a commented Linux-xxHash section to it
  (recipe pointer + "not built by this script" note) — no PowerShell
  logic changed.
- `third_party/build-linux.sh` (the Linux libsodium/libuv/uSockets
  builder) was **not** extended here — it sits outside this change's
  file scope. The standalone `third_party/xxhash/build-linux.sh`
  covers xxHash alone. Follow-up (lead call): either fold a `[4/4]
  xxHash` step into `third_party/build-linux.sh` reusing its
  `clone_pin`/toolchain guards, or keep the per-vendor script.
  If folded in, the xxHash step needs no clone step (vendored
  `include/xxhash.c` is the source) but should copy `VERSION.txt`
  per arch like the other vendors.

## Limitations

- **No Linux bytes, no verification.** The two README placeholders are
  the only linux artifacts. No `file`/`nm`/`objdump`/`sha256sum` check
  has run; expected object formats (x86-64 ELF / AArch64 ELF) are by
  construction (native/cross gcc + GNU ar), not by inspection.
- **No smoke test.** Before landing §4, link a consumer against each
  new archive only (e.g. `XXH64("Hello World!",12,0) == 0x45AB6734B21E6968`,
  `XXH64("",0,0) == 0xEF46DB3751D8E999`, and `XXH3_64bits_withSeed`
  cross-checked against the windows-x64 build) — bit-identical results
  across triples are the acceptance bar (see
  `docs/design/xxhash64.md` §Verification vectors).
- **XXH3 is not a checksum** and must stay internal to the bucket hash;
  this packaging changes nothing about that split.
- **Single-TU vs CMake.** The recipe compiles the vendored `xxhash.c`
  directly instead of upstream's CMake (`XXHASH_BUILD_XXH32/64/3=ON,
  XXHASH_BUILD_TESTS/BENCH=OFF`, as sketched in `xxhash64.md`). Both
  produce the same single object; the direct form is chosen because it
  needs no clone/configure step and matches the task spec. If the lead
  prefers the CMake form for parity with libuv/PCRE2, the delivered
  archive will differ in metadata only — consumer flags are unchanged.
- **arm64 cross prerequisite** (`aarch64-linux-gnu` toolchain) is not
  installed/verified from this host.
