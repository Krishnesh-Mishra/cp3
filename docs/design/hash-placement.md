# Hash placement (Roadmap E)

Internal bucket hash for `Map`/`Set`/`LRUCache` is **wyhash (final4) with a
per-process random seed from libsodium**. HashDoS defense only — the hash is
a non-crypto bucket hash, never a security primitive, never exposed.

## Placement rule

| Surface | Allowed | Forbidden |
| --- | --- | --- |
| `std/crypto` | libsodium-safe only: SHA-256/512, AES-256-GCM, ChaCha20-Poly1305, `randomBytes`/`randomUUID` (libsodium 1.0.22, `third_party/libsodium`, D70) | `wyhash`, `xxhash`/`xxh3` names of any form under `cp3::runtime::crypto::` / `hash.wyhash` / `crypto.xxhash` |
| `std/hash` | Non-crypto checksums only: `crc32`, `crc32c`, `fnv1a32`, `fnv1a64` (header-only, `src/std/hash/include/cp3/std/hash.hpp`) | `wyhash`, `xxhash`, `xxh3`, `siphash` user-facing names; no seeded behavior |
| Internal buckets | wyhash (final4) via `cp3::runtime::detail::bucketHash*` with per-process seed (`src/runtime/include/cp3/core/bucket_hash.hpp`) | Any exposure: seed, raw bucket value, or algorithm name to cp3 user code, serialized output, logs, or cross-process comparison |

Rules enforced by this placement:

* `std/crypto` only libsodium-safe primitives. libsodium `crypto_generichash`
  (BLAKE2b) is libsodium-internal and is **not** re-exported as a bucket hash.
* `std/hash` only non-crypto checksums. Its docs already state "checksums,
  not security — use `std/crypto` for that".
* `wyhash`/`xxhash` never under crypto names, never under `std/hash` names.
  xxhash64 stays BLOCKED as a user name (no vendored xxhash row exists in
  `third_party/MANIFEST.md`); a future switch from wyhash to xxh3 requires
  vendoring xxhash and stays internal.

## What shipped

* `src/runtime/include/cp3/core/bucket_hash.hpp` — `cp3::runtime::detail::
  bucketSeed()`, `bucketHashBytes()`, `bucketHashString()`, and the wyhash
  final4 implementation (header-inline, no new archive dependency).
* `src/runtime/src/core/bucket_hash.cpp` — the per-process seed, generated
  once (magic static, thread-safe) via `sodium_init()` + `randombytes_buf()`
  (32 bytes folded to 64 bits, `sodium_memzero` + avalanche mix). If
  `sodium_init()` fails, degrades to `std::random_device` — still random,
  never a fixed seed. Never returns the seed to user code.
* `src/std/collections/include/cp3/std/collections.hpp` — `KeyHash<K>` now
  hashes via `detail::bucketHash*` instead of `std::hash`, with domain tags
  per family (string/number/bool/other) and SameValueZero canonicalization
  (NaN → one bucket, −0 → +0). `SetData` gained the `index_` map so `add`/
  `has` are O(1) (previously an O(n) linear scan). `MapData` and `LRUData`
  inherit the new `KeyHash` unchanged. No public name changed.

## Seed mechanism

```
first Map/Set/LRU use → bucketSeed() → cachedSeed()
  → sodium_init() → randombytes_buf(32) → fold → 64-bit seed (never exposed)
  → wyhash(final4, data, seed ^ domainTag) per bucket probe
```

* One seed per process, shared by all maps/sets. Collisions cannot be
  precomputed across runs (HashDoS defense); within a run, two maps share
  the same seed — callers must not persist bucket values or compare them
  across processes.
* Domain tags separate families so a string key and a number key cannot be
  cross-predicted into one bucket layout.
* The seed is not derivable from any user-visible value: `bucketSeed()` is
  `detail`-namespaced, no `std/hash`/`std/crypto` entry point returns it, and
  bucket values are `size_t` bucket indices, not digest formats.

## Wiring for lead (not done here — shared files untouched)

`bucket_hash.cpp` references `sodium_init`/`randombytes_buf`, so any
generated program that uses `Map`/`Set`/`LRUCache` must link libsodium.

1. **Runtime build** — `src/runtime/CMakeLists.txt`, add
   `src/core/bucket_hash.cpp` to `cp3_runtime_sources` (after
   `src/core/runtime.cpp`). Include path for `<sodium.h>` is already wired
   (`third_party/libsodium/include` is a PRIVATE include of `cp3_runtime`).
2. **Generated-program link** — mirror the `std/crypto` row in
   `src/cli/src/detail/native_packages.hpp` for `std/collections`: add
   `third_party/libsodium/include` to compile dirs and `-lsodium`
   (plus per-platform `lib` dirs: `third_party/libsodium/lib/{windows-x64,
   linux-x64,linux-arm64}`) whenever the program imports `std/collections`.
   Merge with the existing `std/crypto` entry when a program imports both
   (do not duplicate `-lsodium`).
3. **Collections native row** — same compile/link addition in the
   `kCollectionsNativePackage` row creator (the family that owns
   `src/std/collections/native.hpp`) so `collections`-only programs link
   sodium even without `std/crypto`.

Until 1–3 land, `Map`/`Set`-using programs that do not link libsodium will
fail the final link with `undefined reference to sodium_init /
randombytes_buf` — visible and intentional, not a silent fallback.

## Limitations

* wyhash is not a MAC: within one process, an in-process observer can read
  bucket indices and open-code the constants (they ship in the binary).
  Against a remote attacker who cannot recallly observe timing, per-process
  randomization is sufficient; against an observer with bucket-index
  visibility, it is not. Do not use buckets for anything security-relevant.
* `Set::remove` is O(n) (vector erase + tail re-index); `add`/`has` are
  O(1). `Map::values`/`entries`/`forEach` remain O(n) by design (insertion
  order D44).
* A single shared seed means a collision found in one map degrades every
  map in the process. Per-map salts are out of scope.
* MSVC uses `_umul128` (x64); the portable limb fallback covers ARM64 MSVC /
  other hosts. Tested-by-construction; no build/test run was performed in
  this change (code only, per ledger instructions).
