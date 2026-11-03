---
title: Cryptography
chapter: Chapter 3 · Standard library
---
# Cryptography

`std/crypto` is a default namespace for secure random data, hashes, and authenticated encryption:

```ts
import crypto from "std/crypto";
```

Cryptographic APIs are sensitive to key handling, nonce reuse, and protocol design. Use the authenticated encryption interface as documented; do not invent a format by concatenating raw cryptographic outputs.

## UUIDs and random bytes

- `randomUUID()` creates a random UUID.
- `randomBytes(size)` returns cryptographically generated random bytes.

Use cryptographic randomness for secret material and unpredictable tokens. `std/math.random()` is a pseudo-random numeric helper and is not appropriate for secrets.

## Hashes

`sha256(data)` and `sha512(data)` compute the corresponding digest. A hash is one-way and is not encryption. General-purpose hashes are also not password-hashing functions: use a password-specific derivation scheme when storing passwords.

## AES-256 authenticated encryption

`AES256(key, data)` encrypts with AES-256-GCM and returns base64 text containing the nonce, ciphertext, and authentication tag. The runtime manages the nonce for this operation. `AES256Decrypt(key, encrypted)` decrypts and verifies the authentication tag. If verification fails, it throws a catchable error rather than returning unauthenticated plaintext.

Keep encryption keys secret. The combined output carries the metadata required by decryption; keep the key separate from that value. The API accepts string keys and string payloads, so encode binary data deliberately before encryption.

## Operational guidance

- Do not log keys, raw secret bytes, or decrypted sensitive data.
- Do not hard-code production keys in source code.
- Use a secure secret store or deployment-provided secret injection.
- Treat a decryption/authentication error as invalid data.
- Prefer established protocols and libraries over custom cryptographic constructions.

The module surface currently consists of `randomUUID`, `randomBytes`, `sha256`, `sha512`, `AES256`, and `AES256Decrypt`.

## Exact signatures and results

| Method | Signature | Result |
| --- | --- | --- |
| `randomUUID` | `randomUUID(): string` | RFC 4122 version-4 UUID text (36 characters with hyphens) |
| `randomBytes` | `randomBytes(length: number): Buffer` | Cryptographically random bytes |
| `sha256` | `sha256(data: string | Buffer): string` | Lowercase hexadecimal SHA-256 digest (64 characters) |
| `sha512` | `sha512(data: string | Buffer): string` | Lowercase hexadecimal SHA-512 digest (128 characters) |
| `AES256` | `AES256(key: string, data: string): string` | Base64 authenticated-encryption envelope |
| `AES256Decrypt` | `AES256Decrypt(key: string, encrypted: string): string` | Authenticated plaintext string |

The module is a closed namespace: a misspelled or unsupported member is rejected by the compiler. `randomBytes` returns the nominal `Buffer` type from `std/buffer`, so import Buffer if you annotate it directly. Hash methods accept either text or Buffer; the Buffer bytes are hashed as bytes, not as a printed representation.

## Random byte size and failure behavior

`randomBytes` requires a finite, non-negative integer length no greater than 2,147,483,647. A negative, fractional, infinite, NaN, or too-large value throws a catchable CP3 error at runtime. Zero is valid and returns an empty Buffer. The tests cover negative and fractional values, zero, length, and non-equality of independent random results.

A randomly generated value is not a substitute for a lifecycle policy. Keep secret bytes out of logs, avoid accidental string conversion, and limit how long they remain reachable. CP3 Buffer is immutable through its documented surface; if an API requires mutable key memory or explicit zeroization, this module does not document that capability.

## Hash behavior

SHA-256 and SHA-512 operate on the exact input bytes. A CP3 string is UTF-8, so non-ASCII text is hashed in its UTF-8 encoding. A Buffer input is processed without converting the bytes to text. The returned digest is lowercase hex, with fixed output lengths independent of input length.

Hashes are deterministic and one-way; encryption is reversible with a key. Do not store passwords as a raw SHA-256/SHA-512 digest. This module does not expose a password-specific key derivation API. For repeated short identical SHA-256 inputs the runtime has a small per-thread cache; this is an implementation detail and must not be relied on for semantics or timing.

## AES-256-GCM envelope

`AES256` creates a fresh random nonce for each call and encrypts the UTF-8 bytes of the `data` string using AES-256-GCM. The runtime derives a 32-byte cipher key by hashing the supplied key string with BLAKE2b-256. It returns base64 of the binary envelope:

```text
12-byte nonce || ciphertext || 16-byte authentication tag
```

`AES256Decrypt` decodes this envelope, extracts its nonce, and verifies the authentication tag before returning plaintext. It throws a catchable error for malformed base64, an envelope too short to contain nonce and tag, a wrong key, or tampered ciphertext. Empty plaintext is supported. The randomized nonce means encrypting the same key and data twice produces different ciphertext strings; both decrypt to the same plaintext.

The key derivation here is a fixed hash of the key string, not a password-hardening function with a salt and work factor. Do not treat a human password as a strong encryption key merely because this API accepts a string. Supply high-entropy secret material from an appropriate secret-management process. The API does not accept associated data, binary Buffer plaintext, or caller-supplied nonces.

## Dependency and platform notes

The implementation is backed by the compiler's bundled libsodium dependency. An application importing `std/crypto` therefore participates in the native crypto dependency/build path. This is different from `std/math.random()`, which is not cryptographically secure.

## Relevant conformance cases

The cases under `tests/cases/stdlib/01/crypto` cover signatures, empty and non-empty hashes, UTF-8 hashing, Buffer hashing, UUID shape, random byte sizes and failures, AES empty and multiline round trips, nonce randomization, malformed envelopes, wrong keys, and tampering. Use these as the behavior contract when changing the implementation or documentation.

## Safe usage outline

```ts
import crypto from "std/crypto";

const token = crypto.randomBytes(32);
const digest: string = crypto.sha256(token);
```

This sketch demonstrates random bytes and byte-oriented hashing. To encrypt, obtain high-entropy key material through the deployment's secret-management process, avoid printing it, and define how ciphertext is versioned and stored. Do not copy a key literal into source. See [Buffers](11-buffer.md) for byte/text encodings and [Environment](02-env.md) for configuration.
