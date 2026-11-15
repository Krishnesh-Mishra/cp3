---
title: Cryptography
---
# Cryptography

`std/crypto` provides secure random data, hashes, HMAC, BLAKE2b, UUID helpers and AES-256-GCM authenticated encryption. Import the default namespace:

```ts
import crypto from "std/crypto";
```

Backed by the compiler's bundled libsodium (vendored for windows-x64, windows-arm64, linux-x64, linux-arm64); importing the module pulls in the native crypto dependency. `std/math.random()` is not a substitute for any of this. Hash methods accept `string | Buffer`; a Buffer is hashed as bytes, not as printed text. The namespace is closed — unsupported members are compile-time errors.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `randomUUID` | `randomUUID(): string` | RFC 4122 v4 UUID, 36 chars with hyphens |
| `randomBytes` | `randomBytes(length: number): Buffer` | Cryptographic bytes; length must be a finite non-negative integer ≤ 2,147,483,647; `0` returns an empty Buffer; other values throw |
| `getRandomValues` | `getRandomValues(length: number): Buffer` | Alias of `randomBytes` (same rules) |
| `sha256` | `sha256(data: string \| Buffer): string` | Lowercase hex, 64 chars |
| `sha512` | `sha512(data: string \| Buffer): string` | Lowercase hex, 128 chars |
| `sha256Raw` | `sha256Raw(data: string \| Buffer): Buffer` | Raw 32 digest bytes, for byte consumers |
| `hmacSha256` | `hmacSha256(key: string \| Buffer, data: string \| Buffer): string` | Lowercase hex HMAC-SHA-256; key may be any length |
| `hmacSha512` | `hmacSha512(key: string \| Buffer, data: string \| Buffer): string` | Lowercase hex HMAC-SHA-512 |
| `blake2b` | `blake2b(data: string \| Buffer): string` | Lowercase hex, unkeyed BLAKE2b, 32-byte output |
| `AES256` | `AES256(key: string, data: string): string` | Base64 envelope (12-byte nonce ‖ ciphertext ‖ 16-byte tag); fresh random nonce per call; key string is hashed with BLAKE2b-256 |
| `AES256Decrypt` | `AES256Decrypt(key: string, encrypted: string): string` | Decrypts and verifies the tag; throws a catchable error on wrong key, tampering, malformed base64, or too-short envelope; wrong key never returns plaintext |
| `uuidParse` | `uuidParse(uuid: string): Buffer` | 16 raw bytes of a canonical UUID; throws on malformed input |
| `uuidValidate` | `uuidValidate(uuid: string): boolean` | True for canonical `8-4-4-4-12` hex (case-insensitive); version/variant nibbles not constrained |
| `uuidVersion` | `uuidVersion(uuid: string): number` | Version nibble `0`–`15` of a canonical UUID; `0` when not canonical |

## Notes

- Hashes are one-way, not encryption; not password-hashing — this module exposes no password derivation.
- `AES256`'s key derivation is a fixed hash of the key string, not a salted KDF; supply high-entropy key material, not a password. Empty plaintext is supported; encrypting the same key+data twice yields different ciphertext, both decrypting to the same plaintext. The API takes no associated data, no Buffer plaintext, and no caller-supplied nonces.
- Compare HMACs in full (or not at all); a truncated comparison is not a constant-time check. Treat a decryption/authentication error as invalid data. The runtime keeps a small per-thread cache for repeated short SHA-256 inputs — an implementation detail, not something to rely on for semantics or timing.

Behavior details: [guide](../../standard-library/10-crypto.md)
