---
title: Hash
---
# Hash

`std/hash` provides fast **non-cryptographic** checksums. Import the default namespace:

```ts
import hash from "std/hash";
```

Checksums are easy to forge: never use them for passwords, signatures, tokens, or integrity against an adversary — use [Cryptography](./crypto.md) for that. They are useful for cache keys, change detection and corruption accident checks.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `crc32` | `crc32(input: string \| Buffer): u32` | IEEE 802.3 CRC-32; `number` in a TypeScript project |
| `crc32c` | `crc32c(input: string \| Buffer): u32` | Castagnoli CRC-32C; `number` in TypeScript |
| `fnv1a32` | `fnv1a32(input: string \| Buffer): u32` | FNV-1a 32-bit; `number` in TypeScript |
| `fnv1a64` | `fnv1a64(input: string \| Buffer): u64` | FNV-1a 64-bit; `number` in TypeScript loses precision above 2^53 |
| `xxhash64` | `xxhash64(input: string \| Buffer): u64` | XXH64, seed fixed to `0` — deterministic across runs, safe for persisted cache keys; `number` in TypeScript |

In a Rust/C++ project the returns are exact native widths (`u32`/`u64`); in a TypeScript project, compare `u64` values for equality rather than expecting exact integer text. All five functions are deterministic and stateless — the same bytes give the same value in every process. Buffer inputs are hashed as bytes; CP3 strings are UTF-8.

Check values: `crc32("")`/`crc32c("")` are `0`; `crc32("123456789")` is `3421780262` (`0xCBF43926`); `crc32c("123456789")` is `3808858755` (`0xE3069283`); `fnv1a32("")` is `2166136261`; `fnv1a64("abc")` is `16654208175385433931`; `xxhash64("")` is `0xEF46DB3751D8E999`.

No seeded/keyed hashing, no streaming, no hex rendering of the result — format at the call site. `xxhash64` links the vendored XXH64 archive selected when a program imports `std/hash` (windows-x64/arm64, linux-x64/arm64); the rest are dependency-free. `std/hash` carries no native crypto dependency.

Behavior details: [guide](../../standard-library/23-hash.md)
