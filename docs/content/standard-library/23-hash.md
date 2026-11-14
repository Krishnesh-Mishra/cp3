---
title: Checksums
---
# Checksums

`std/hash` is a default namespace for fast non-cryptographic checksums:

```ts
import hash from "std/hash";
```

Checksums are **not security**. They are easy to forge and must never be used for passwords, signatures, tokens, or integrity against an adversary. They are useful for cache keys, change detection, and corruption checks against accidents. Use [Cryptography](./10-crypto.md) (`std/crypto`: SHA-256/512, AES-256-GCM) for anything adversarial.

## Exact signatures and results

| Method | Signature (exact types / widths) | Result |
| --- | --- | --- |
| `crc32` | `crc32(input: string \| Buffer): u32` | IEEE 802.3 CRC-32. In a TypeScript project: `number`. In a Rust/C++ project: `u32`. |
| `crc32c` | `crc32c(input: string \| Buffer): u32` | Castagnoli CRC-32C. TypeScript: `number`. Rust/C++: `u32`. |
| `fnv1a32` | `fnv1a32(input: string \| Buffer): u32` | FNV-1a 32-bit. TypeScript: `number`. Rust/C++: `u32`. |
| `fnv1a64` | `fnv1a64(input: string \| Buffer): u64` | FNV-1a 64-bit. TypeScript: `number` (values above 2^53 lose precision). Rust/C++: `u64`. |
| `xxhash64` | `xxhash64(input: string \| Buffer): u64` | XXH64 (vendored xxHash) with the seed fixed to 0. TypeScript: `number` (values above 2^53 lose precision). Rust/C++: `u64`. |

A TypeScript project keeps `number` for every return; a Rust/C++ project gets the exact native width (`u32`/`u64`). Hash methods accept either text or a Buffer; the Buffer bytes are hashed as bytes, not as a printed representation. `u32` values never exceed 2^32 and are exactly representable in a TypeScript `number`; `u64` values should be compared for equality in TypeScript (do not expect exact integer text) and read exactly in Rust/C++ as `u64`.

`xxhash64`'s seed is deliberately not a parameter, so the call is deterministic across runs: it is safe for persisted cache keys.

## Semantics

All five functions are deterministic and stateless: the same bytes give the same value in every process. `crc32("")` and `crc32c("")` are `0`. The standard check values for `"123456789"` are `crc32: 3421780262` (`0xCBF43926`) and `crc32c: 3808858755` (`0xE3069283`). `fnv1a64("abc")` is `16654208175385433931`. `fnv1a32("")` is `2166136261` (the 32-bit FNV offset basis). `xxhash64("")` is `0xEF46DB3751D8E999` (`17241709254077376921`). A CP3 string is UTF-8, so non-ASCII text is hashed in its UTF-8 encoding. A Buffer input is processed without converting the bytes to text.

## Limits

- Checksums, not security: never use for passwords, signatures, tokens, or integrity against an adversary. Use [Cryptography](./10-crypto.md) for anything adversarial.
- No seed parameter on `xxhash64`: results are fixed for fixed bytes in every process; use `std/crypto` digests if you need keyed hashing.
- No hex/base64 rendering of the checksum itself: format the numeric result at the call site if you need text.
- No streaming or incremental hashing: each call hashes one complete `string | Buffer` value.
- No keyed or seeded hashing: results are fixed for fixed bytes in every process; use `std/crypto` digests if you need keyed hashing.

## Requirements and platforms

`crc32`, `crc32c`, `fnv1a32` and `fnv1a64` are dependency-free. `xxhash64` links the vendored XXHash static archive, which is selected when a program imports `std/hash`; the archives are vendored for windows-x64, windows-arm64, linux-x64 and linux-arm64. Importing `std/hash` takes no native crypto dependency (it is intentionally different from `std/crypto`, which uses libsodium).

```ts
import hash from "std/hash";

const cacheKey = hash.crc32(configText);
const stillSame: boolean = hash.fnv1a64(payload) === hash.fnv1a64(payload);
```

Both are safe uses: neither value is treated as proof of origin. For proof of origin, use `std/crypto` authenticated encryption or digests.

## Related pages

- [Cryptography](./10-crypto.md) — SHA-256/512 and AES-256-GCM for anything adversarial
- [Buffers](./11-buffer.md) — byte-oriented inputs