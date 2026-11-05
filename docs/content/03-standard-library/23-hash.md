---
title: Checksums
chapter: Chapter 3 · Standard library
---
# Checksums

`std/hash` is a default namespace for fast non-cryptographic checksums:

```ts
import hash from "std/hash";
```

Checksums are **not security**. They are easy to forge and must never be used for passwords, signatures, tokens, or integrity against an adversary. They are useful for cache keys, change detection, and corruption checks against accidents. Use [Cryptography](10-crypto.md) (`std/crypto`: SHA-256/512, AES-256-GCM) for anything adversarial.

## Exact signatures and results

| Method | Signature (exact types / widths) | Result |
| --- | --- | --- |
| `crc32` | `crc32(input: string \| Buffer): u32` | IEEE 802.3 CRC-32. In a TypeScript project: `number`. In a Rust/C++ project: `u32` (unsigned 32-bit). |
| `crc32c` | `crc32c(input: string \| Buffer): u32` | Castagnoli CRC-32C. TypeScript: `number`. Rust/C++: `u32`. |
| `fnv1a32` | `fnv1a32(input: string \| Buffer): u32` | FNV-1a 32-bit. TypeScript: `number`. Rust/C++: `u32`. |
| `fnv1a64` | `fnv1a64(input: string \| Buffer): u64` | FNV-1a 64-bit. TypeScript: `number` (values above 2^53 lose precision). Rust/C++: `u64` (unsigned 64-bit). |

A TypeScript project keeps `number` for every return; a Rust/C++ project gets the exact native width (`u32`/`u64`), the same rule `std/fs` uses for `FileStats.size`. The module is a closed namespace: a misspelled or unsupported member is rejected by the compiler. Hash methods accept either text or Buffer; the Buffer bytes are hashed as bytes, not as a printed representation. `u32` values never exceed 2^32 and are exactly representable in a TypeScript `number`; `u64` values are compared for equality in TypeScript (compare two results, do not expect exact integer text) and read exactly in Rust/C++ as `u64`.

`xxhash64` is **BLOCKED** and not implemented: no XXHash archive is vendored, so calling `hash.xxhash64` is a compiler error like any other unknown member.

## Semantics

All four functions are deterministic and stateless: the same bytes give the same value in every process. `crc32("")` and `crc32c("")` are `0`. The standard check values for `"123456789"` are `crc32: 3421780262` (`0xCBF43926`) and `crc32c: 3808858755` (`0xE3069283`). `fnv1a64("abc")` is `16654208175385433931`. `fnv1a32("")` is `2166136261` (the 32-bit FNV offset basis). A CP3 string is UTF-8, so non-ASCII text is hashed in its UTF-8 encoding. A Buffer input is processed without converting the bytes to text. Header-only and dependency-free: no vendored archive is linked; the CRC tables are hand-written from the IEEE 802.3 (`0xEDB88320`) and Castagnoli (`0x82F63B78`) polynomials, and FNV-1a is implemented inline. Importing `std/hash` therefore takes no native crypto dependency; this is intentionally different from `std/crypto` (libsodium).

## Limits

- Checksums, not security: checksums are easy to forge and must never be used for passwords, signatures, tokens, or integrity against an adversary. They are suitable only for cache keys, change detection, and corruption checks against accidents. Use [Cryptography](10-crypto.md) (`std/crypto`: SHA-256/512, AES-256-GCM) for anything adversarial.
- No `xxhash64`: blocked and not implemented; use `fnv1a64` or `std/crypto` digests instead.
- No hex/base64 rendering of the checksum itself: format the numeric result at the call site if you need text.
- No streaming or incremental hashing: each call hashes one complete `string | Buffer` value.
- No keyed or seeded hashing: results are fixed for fixed bytes in every process (contrast `Map`/`Set` bucket hashing, which uses a per-process random seed for HashDoS defense and never appears here).

Width note (restated): `crc32`, `crc32c`, and `fnv1a32` never exceed 2^32, so they are exactly representable in a TypeScript `number`. `fnv1a64` does not fit in a `number` exactly: in a TypeScript project values above 2^53 are rounded, so compare two `fnv1a64` results for equality in that vocabulary, and read the exact 64-bit value in a Rust/C++ project (`u64`).

## Dependency and platform notes

Header-only and dependency-free: no vendored archive is linked. The CRC tables are hand-written from the IEEE 802.3 (`0xEDB88320`) and Castagnoli (`0x82F63B78`) polynomials, and FNV-1a is implemented inline under `cp3/std/hash.hpp`. A program that imports `std/hash` therefore takes no native crypto dependency; this is intentionally different from `std/crypto` (libsodium).

## Relevant conformance cases

The cases under `tests/cases/stdlib/02/hash` cover the standard check values, empty inputs, Buffer/string equivalence, determinism, the exact 64-bit value in Rust-widths, and the blocked `xxhash64`. Use these as the behavior contract when changing the implementation or documentation.

## Safe usage outline

```ts
import hash from "std/hash";

const cacheKey = hash.crc32(configText);
const stillSame: boolean = hash.fnv1a64(payload) === hash.fnv1a64(payload);
```

This sketch shows key-building and a change check. Both are safe uses: neither value is treated as proof of origin. For proof of origin, use `std/crypto` authenticated encryption or digests.
