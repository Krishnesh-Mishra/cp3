# Ledger F vendored-libs smoke verification — design

Solo, code-only design (no builds, no tests run, no commit). It adds smoke
programs for the five libraries vendored 2026-11-05 under "Ledger F"
(`third_party/MANIFEST.md`): **XXHash, simdutf, unordered_dense, fast_float,
moodycamel (ConcurrentQueue)**. The smokes follow the existing uWS/libuv
smoke pattern in `third_party/build.ps1` §10 verbatim: compile from a clean
directory using **only** `third_party` includes/archives, link, run to exit
code 0, in **3 link variants**, evidence to
`build/scratch/vend/smoke/SMOKE-RUNS.txt`.

## Files (only files created)

| file | kind |
| --- | --- |
| `third_party/smokes/xxhash_smoke.cpp` | smoke source |
| `third_party/smokes/simdutf_smoke.cpp` | smoke source |
| `third_party/smokes/unordered_dense_smoke.cpp` | smoke source |
| `third_party/smokes/fast_float_smoke.cpp` | smoke source |
| `third_party/smokes/concurrentqueue_smoke.cpp` | smoke source |
| `third_party/smokes/run_smokes.ps1` | smoke runner (3-variant matrix + `SMOKE-RUNS.txt` evidence) |
| `docs/design/vendor-smoke.md` | this document |

No other file is touched: `third_party/build.ps1`, `third_party/MANIFEST.md`,
`src/**` CMake, and the existing scratch smoke evidence are unchanged.
Wiring the new matrix into `build.ps1` is a lead step (see §Wiring).

## Smoke list

Common contract (all five): `-std=c++20 -O2 -Wall -Wextra`, no input, no
files, deterministic verdict, print `PASS`/`FAIL` per check, exit `0` iff
all checks pass, `1` otherwise (an exception escaping `main` also fails the
run, as with the uWS/libuv smokes).

| smoke | library (pin, `VERSION.txt`) | kind | assertions |
| --- | --- | --- | --- |
| `xxhash_smoke.cpp` | XXHash v0.8.3 `e626a72b…f363` | compiled, `libxxhash.a` | `XXH64("",0) == 0xEF46DB3751D8E999`; `XXH64("Hello World!",0) == 0x45AB6734B21E6968`; repeat-call determinism; seed changes digest; streaming `XXH64_update`/`XXH64_digest` equals the one-shot value |
| `simdutf_smoke.cpp` | simdutf v6.1.0 `7852150e…ec5e660ec` | compiled, `libsimdutf.a` | `simdutf::get_active_implementation()->validate_utf8` true for `""`, ASCII, U+00E9, U+20AC, U+1F600, mixed ASCII+multibyte; false for lone `0xFF`, bad continuation `C3 28`, truncated `E2 82`, bare continuation `80`, overlong `C0 AF`; `validate_utf8_with_errors` agrees (`SUCCESS`+`count==len` on valid, non-`SUCCESS` on invalid) |
| `unordered_dense_smoke.cpp` | ankerl unordered_dense v4.3.1 `7c58a8b7…ba9d5` | header-only | `ankerl::unordered_dense::map` empty/find-miss, insert, hit, overwrite keeps size, erase `1`/`0`, gone-after-erase, iteration visits exactly the remaining entries (order **not** asserted); `set` dedupe/`contains`/erase; 10 000 inserts all findable after growth |
| `fast_float_smoke.cpp` | fast_float master `6a3fabc3…fdf8fb9` | header-only | `fast_float::from_chars<double>` full-consumption parses `42`, `3.14159`, `-0.5`, `1.5e10`, `-2.5E-3`, `007.25`, `1e+5` to the identical double as the C++ literal; `float` overload parses `0.1`; invalid input → `ec != errc()` and `ptr == first`; `"123abc"` → value `123`, `ptr == first+3`; empty input rejected |
| `concurrentqueue_smoke.cpp` | moodycamel concurrentqueue v1.0.4 `6dd38b8a…a6ff5d` | header-only | empty-queue `try_dequeue` fails; 0…999 enqueue/dequeue round-trip yields exactly that multiset (sorted compare); queue empty after drain; 4 producers × 250 / 4 consumers via `std::thread` consume all 1000 items with sum `0+…+999` (ordering across producers **not** asserted) |

Notes on expected values: the two XXH64 digests are the canonical upstream
sanity vectors (given in the task brief); the simdutf verdicts are fixed by
the UTF-8 specification; the fast_float expectations are the C++ literals
themselves, which a correctly-rounded parser must reproduce bit-for-bit.

## Build instructions (match `build.ps1` §10)

Toolchain identical to `build.ps1`: MinGW-W64 x86_64 ucrt posix-seh GCC
16.x, `g++` on PATH. Each smoke is compiled in a clean output directory
(`build/scratch/vend/smoke/`, wiped per run) referencing **only**
`third_party` include/archive paths, then run. Three link variants per
program, in this order (`build.ps1` lines 200–204):

1. `dynamic` — plain link;
2. `static-lib` — add `-static-libgcc -static-libstdc++`;
3. `static` — add `-static -static-libgcc -static-libstdc++`.

The runner, `third_party/smokes/run_smokes.ps1`, implements exactly that
and appends every command line, toolchain version, and exit code to
`build/scratch/vend/smoke/SMOKE-RUNS.txt`. Dynamic-variant commands (the
other two only add the flags above):

```powershell
g++ -std=c++20 -O2 -Wall -Wextra -I third_party/xxhash/include `
    third_party/smokes/xxhash_smoke.cpp -L third_party/xxhash/lib/windows-x64 -lxxhash `
    -o build/scratch/vend/smoke/xxhash_dynamic.exe

g++ -std=c++20 -O2 -Wall -Wextra -I third_party/simdutf/include `
    third_party/smokes/simdutf_smoke.cpp -L third_party/simdutf/lib/windows-x64 -lsimdutf `
    -o build/scratch/vend/smoke/simdutf_dynamic.exe

g++ -std=c++20 -O2 -Wall -Wextra -I third_party/unordered_dense/include `
    third_party/smokes/unordered_dense_smoke.cpp -o build/scratch/vend/smoke/unordered_dense_dynamic.exe

g++ -std=c++20 -O2 -Wall -Wextra -I third_party/fast_float/include `
    third_party/smokes/fast_float_smoke.cpp -o build/scratch/vend/smoke/fast_float_dynamic.exe

g++ -std=c++20 -O2 -Wall -Wextra -pthread -I third_party/moodycamel/include `
    third_party/smokes/concurrentqueue_smoke.cpp -o build/scratch/vend/smoke/concurrentqueue_dynamic.exe
```

Header-only libraries have no archive, so for them the three variants vary
only libstdc++/libc linkage — which is still the point of the §10 matrix
(the static variants are what ships). `-pthread` on the concurrentqueue
smoke is both a compile and link flag. The runner accepts `-OutDir` to
redirect the clean work dir + evidence file (default
`build\scratch\vend\smoke`, matching `build.ps1`).

## Wiring (lead step, not done here)

1. Merge, don't replace: the scratch `build/scratch/vend/scripts/run_smokes.ps1`
   that `build.ps1` calls today is the uWS/libuv (etc.) matrix. Append the
   five `Invoke-Smoke` blocks from `third_party/smokes/run_smokes.ps1` to it,
   or keep this file as `run_smokes_ledgerf.ps1` and call both from
   `build.ps1` §10. Overwriting the existing script would silently drop the
   older smokes' evidence.
2. Same `OutDir` caveat the other way: this runner wipes its output dir, and
   `build.ps1` §10's evidence (`SMOKE-RUNS.txt`) lives in that dir. If run
   standalone, pass e.g. `-OutDir build\scratch\vend\smoke-ledgerf` and merge
   the log, or merge the two scripts in step 1 so the wipe is shared.
3. `build.ps1` also needs Ledger F build/vendor steps before the smokes can
   pass in a full rebuild: currently `$pins` (lines 101–105) clone the five
   sources but nothing builds xxhash/simdutf archives or copies
   unordered_dense/fast_float/moodycamel headers into `third_party/` —
   those trees already exist in the repo from the 2026-11-05 vendoring, but
   `build.ps1`'s vendor-copy step would not reproduce them today.

## Limitations

- **Not run.** Code-only task: no `cmake`/build/test was executed, so these
  sources are verified by reading the vendored headers only (API names:
  `XXH64`/`XXH64_state_t` from `xxhash.h`; `simdutf::get_active_implementation()`,
  `validate_utf8_with_errors`, `simdutf::error_code::SUCCESS` from
  `simdutf/implementation.h` + `simdutf/error.h`; `ankerl::unordered_dense::map/set`;
  `fast_float::from_chars`; `moodycamel::ConcurrentQueue::enqueue/try_dequeue`).
  First run by the lead is the real check; likely polish points are
  `-Wall -Wextra` noise and the fully-static concurrentqueue link.
- **Windows x64 only.** `libxxhash.a`/`libsimdutf.a` exist only under
  `lib/windows-x64/`; there are no `linux-x64`/`linux-arm64` archives yet,
  and `third_party/build-linux.sh` builds only libsodium/libuv/uSockets.
  A Linux smoke matrix needs those archives first (same single-TU pattern).
- **Stray objects.** `third_party/xxhash/lib/windows-x64/xxhash.o` and
  `third_party/simdutf/lib/windows-x64/simdutf.o` sit next to the `.a`
  files; the smokes link the archives only. The `.o` files are dead weight
  (or a future single-TU fallback) and should be confirmed as such.
- **Name drift vs `internal-libs.md`.** That design guessed a
  `third_party/concurrentqueue/include` path; the actual vendored tree is
  `third_party/moodycamel/include`. The smoke (and any CMake wiring) must
  use `moodycamel`.
- **Coverage is deliberately thin** (smoke ≠ test suite): XXH64 only — no
  XXH32/XXH3 (the chosen bucket hash), no multi-block streaming; simdutf
  UTF-8 validation only — no UTF-16/32 transcode, base64, or forcing the
  non-SIMD fallback implementation (dispatch is host-CPU dependent);
  fast_float no subnormals/inf/nan spellings/long-digit round-trips, and
  its pin is a `master` SHA (reproducible by SHA only — consider a tag);
  unordered_dense no PMR/segmented/`map_view`/custom-hash path;
  concurrentqueue totals-only under `std::thread` (no TSan/ASan variant,
  busy-spin consumer loop, ordering never asserted).
- **No sanitizer/UBSan/ASan variant** in the matrix, matching §10's
  dynamic/static-lib/static trio. Adding `-fsanitize=address,undefined`
  (dynamic variant only) is a cheap future extension for the header-only
  smokes.
