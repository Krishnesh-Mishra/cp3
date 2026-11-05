# `std/hash` xxhash64 — vendoring prep (ROADMAP C)

Solo prep, code only. No `cmake`/build/test ran, no `third_party/` tree touched,
no shared files edited. After this change `hash.xxhash64` is still BLOCKED
(CP3305) — the doc below is the unblock plan.

## Decision (lead)

Vendor **XXHash** (Cyan4973/XXHash, BSD-2-Clause) once, compiled static:

- `XXH64` → user-facing `hash.xxhash64` (deterministic checksum, seed 0).
- `XXH3` (`XXH3_64bits_withSeed`) → internal Map/Set bucket hash, seeded per
  process (replaces the hand-written wyhash final4 in
  `src/runtime/include/cp3/core/bucket_hash.hpp`). Never user-facing.

Rationale for splitting the two names over the one archive: `XXH64` is a frozen
checksum (stable across XXHash minor versions — safe for persisted cache keys),
whereas `XXH3` is the faster bucket hash but its algorithm was not frozen at
0.8.0; exposing it by name would promise stability we do not have. One archive
serves both at zero marginal cost; two archives (wyhash + xxhash) would double
the audit surface.

## Header-only vs compiled split

**Compiled static wins for XXHash.** XXHash also ships header-only forms
(`XXH_INLINE_ALL` in `xxhash.h`, `xxh3.h` secret variants), but they were
rejected here:

| Option | Why not / why yes |
|---|---|
| Header-only (`XXH_INLINE_ALL`) | Duplicates upstream's 10k-line implementation into every consumer TU (every generated program importing `std/hash`, plus the runtime if XXH3 is wired there). SIMD dispatch (`XXH3_initCustomSecret` variants) gets recompiled per TU, provenance becomes "copied text", and pinning is a file checksum instead of an archive. Convenient for single-use, wrong for a shared std/runtime dependency. |
| **Compiled static (`libxxhash.a` per triple)** — chosen | One object per triple (`windows-x64` / `linux-x64` / `linux-arm64`), built once by the vendor recipe, linked like libsodium/libuv/PCRE2 (`-L …/lib/<triple> -lxxhash`). `hash.hpp` stays a thin wrapper; if the bucket switch lands, `bucket_hash.hpp` calls `XXH3_64bits_withSeed` with no new archive — the marginal cost of the second consumer is zero. |

The wyhash header-inline backend (`bucket_hash.hpp`) is **not** deleted in the
prep step. It stays in place until the archive is pinned and smoke-tested; the
swap to `XXH3_64bits_withSeed` is a separate, atomic lead step (seed math must
stay internal — see Limitations).

## MANIFEST row format (proposed, not landed)

When the archive is delivered, add XXHash following the existing three-table
shape in `third_party/MANIFEST.md` (do not land these rows until bytes exist):

**Pins** (fill `tag`/`commit` at vend time; candidate: upstream release
`v0.8.3`, the first 0.8.x with CMake install support used by libsodium recipes
— lead to confirm):

```
| XXHash | https://github.com/Cyan4973/xxHash.git | v0.8.3 | <full SHA> |
```

**Delivered artifacts** (per triple, mirroring libsodium/libuv):

```
| xxhash/lib/libxxhash.a | <bytes> | <SHA-256> |
| xxhash/lib/windows-x64/libxxhash.a | <bytes> | <SHA-256> |
| xxhash/lib/linux-x64/libxxhash.a | <bytes> | <SHA-256> |
| xxhash/lib/linux-arm64/libxxhash.a | <bytes> | <SHA-256> |
```

**Licenses:**

```
| xxhash/LICENSE | BSD-2-Clause |
```

Plus `third_party/xxhash/VERSION.txt` in the existing one-line format:
`"<repo URL> <tag> <full SHA> <date built>"`.

**Vendor tree** (populate at vend time only):

```
third_party/xxhash/
  VERSION.txt
  LICENSE
  include/  { xxhash.h, xxh3.h, xxhash_v32.h, xxhsum.h }
  lib/libxxhash.a
  lib/{windows-x64,linux-x64,linux-arm64}/libxxhash.a
```

`XXH_STATIC_LINKING_ONLY` / `XXH_PRIVATE_API` must **not** be defined by
consumers (they would break the stable ABI); `XXH_INLINE_ALL` must not be
defined either (it is incompatible with linking `libxxhash.a` for `XXH64`).

## Wiring for lead (not landed)

1. **Build recipe** — extend `third_party/build.ps1` following the libuv/PCRE2
   CMake+Ninja pattern: `BUILD_SHARED_LIBS=OFF`, `XXHASH_BUILD_XXH32=ON`,
   `XXHASH_BUILD_XXH64=ON`, `XXHASH_BUILD_XXH3=ON`, `XXHASH_BUILD_TESTS=OFF`,
   `XXHASH_BUILD_BENCH=OFF`, Release, install prefix. Record the exact commands
   in `MANIFEST.md` "Build commands" section, with the same CMake-cache
   verification note as libuv/oneTBB.
2. **`std/hash` runtime** — `src/std/hash/include/cp3/std/hash.hpp`: add

   ```cpp
   #include <xxhash.h>
   inline std::uint64_t xxhash64(std::string_view data) {
       return XXH64(data.data(), data.size(), 0ULL /* seed */);
   }
   ```

   No seed in the public API; the wrapper must stay `inline` in the header so
   the existing header-only family shape is unchanged.
3. **`std/hash` native row** — after vendoring, `src/std/hash/native.hpp`
   becomes the `std/crypto` pattern:

   ```
   compile: -I third_party/xxhash/include
   link:    -L third_party/xxhash/lib/<triple> -lxxhash
   ```

   per-triple rows for `windows-x64`, `linux-x64`, `linux-arm64`; the compile
   dir merges with the existing `src/std/hash/include` +
   `src/std/buffer/include` trees. Dedupe `-lxxhash` if a program imports both
   `std/hash` and the bucket-hash consumer.
4. **Sema / codegen** — once vendored, add to `kHashRows` in
   `src/std/hash/sema.cpp`:

   ```cpp
   {"xxhash64", stringOrBufferToNumber,
    "xxhash64(input: string | Buffer): u64",
    "<XXH64 non-crypto wording; seed 0, deterministic>"},
   ```

   with the return width via
   `stdIntegerType(StdIntegerKind::Checksum64, options_.typeSystem)`
   (the same swap pattern already applied to `fnv1a64` in sema.cpp), and add
   `"xxhash64"` to `kHashMethods` in `src/std/hash/codegen.cpp`. Width handling
   (TypeScript `static_cast<double>` widening) is already correct for any
   64-bit return — no codegen change beyond the method name.
5. **Bucket hash (optional, separate step)** — replace the body of
   `bucketHashBytes` in `src/runtime/include/cp3/core/bucket_hash.hpp` with
   `XXH3_64bits_withSeed(data, len, seed ^ domainTag)` and add
   `third_party/xxhash/include` to `cp3_runtime`'s PRIVATE includes in
   `src/runtime/CMakeLists.txt` plus the corresponding archive link. Keep
   `bucketSeed()` semantics identical (libsodium seed, domain tags) — only the
   mix function changes. Land only after `std/hash` is already linking
   `libxxhash.a`, so the archive is proven on one consumer first.
6. **Tests / docs** — replace `tests/cases/stdlib/02/hash/hash_xxhash64_blocked`
   with a value + determinism + Buffer/string-equivalence case at vend time,
   and update `docs/content/03-standard-library/23-hash.md` (remove the BLOCKED
   line, add the `xxhash64` row). Do not ship the wiring without these.

## Verification vectors

Before landing 2–5, smoke-test the vendored archive against upstream vectors
(`XXH64("Hello World!", 12, 0) == 0x45AB6734B21E6968`, empty input
`XXH64("", 0, 0) == 0xEF46DB3751D8E999`, fixed known-answer `sanityBuffer`
from `xxhash.h`). A mismatch means the build flags (SIMD baseline, endianness)
are wrong — checksum semantics must be bit-identical across triples.

## Limitations

- **No pin, no bytes, no smoke yet.** Tag/SHA above are placeholders from the
  Ledger F inventory (`internal-libs.md`); the lead must pin, build the
  per-triple matrix, and record bytes + SHA-256 before any `src/**` change.
- **Blocked until vendored.** Reverting to a hand-rolled `xxhash64` under that
  name is not allowed — `hash.hpp`/`sema.cpp` say BLOCKED precisely to prevent
  a non-conformant replica; only the vendored object may implement the name.
- **XXH3 is not a checksum.** `XXH3_64bits_withSeed` must never be exposed as a
  `std/hash` name or serialized (bucket values stay per-process); it is not a
  MAC and does not defend HashDoS on its own — the per-process libsodium seed
  does.
- **XXH64 seed exposure.** The public `xxhash64` wrapper hard-codes seed 0;
  do not add an optional seed parameter without re-reviewing HashDoS and
  cross-run stability.
- **wyhash remains the fallback.** If the archive build or hash-placement
  review stalls, `bucket_hash.hpp` keeps working without `libxxhash.a`;
  bucket code must compile both before and after the switch (no hard
  dependency on XXHash at configure time).
- **TypeScript precision.** 64-bit values above 2^53 lose precision via
  `static_cast<double>` (codegen.cpp) — documented, unchanged by vendoring.
