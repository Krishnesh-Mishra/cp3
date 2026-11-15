---
title: Typed arrays
---
# Typed arrays

There is **no `std/typed-arrays` module** — typed arrays, `ArrayBuffer`, `SharedArrayBuffer` and `DataView` are built-in language families, available with no import, mirroring the language-level surface documented in the [typed arrays reference](../language-apis/typed-arrays.md) and the [guide](../../standard-library/16-typed-arrays.md). This page summarizes the surface for discovery; the linked pages are authoritative.

| Family | Element width | Stored representation |
| --- | ---: | --- |
| `Int8Array` | 1 | signed 8-bit integer |
| `Uint8Array` | 1 | unsigned 8-bit integer |
| `Uint8ClampedArray` | 1 | 8-bit, clamped writes (ties to even) |
| `Int16Array` / `Uint16Array` | 2 | 16-bit signed / unsigned |
| `Int32Array` / `Uint32Array` | 4 | 32-bit signed / unsigned |
| `Float32Array` / `Float64Array` | 4 / 8 | IEEE single / double |
| `BigInt64Array` / `BigUint64Array` | 8 | 64-bit signed / unsigned via `double` — integers above the exact binary64 range lose precision on reads |

Key facts (see the typed-arrays page for the full contract):

- Constructor: `new Float64Array(length)`, `new Uint8Array([values])`, or a view over an `ArrayBuffer`/`SharedArrayBuffer` with optional offset and length; multi-byte views require aligned offsets.
- Writes convert to the element representation (integer arrays wrap modulo 2^width; `Uint8ClampedArray` clamps; non-finite numeric values write as zero).
- Indexing is zero-based; an out-of-range read yields `undefined`, an out-of-range write is ignored. `length`, `byteLength`, `byteOffset`, `buffer`, `BYTES_PER_ELEMENT` describe the view.
- The checked method surface: `subarray` (aliased view), `slice` (copy), `copyWithin`, `reverse`, `indexOf`, `includes`, `join`, `set`, `fill`.
- `ArrayBuffer` owns fixed-length bytes and can be sliced; views retain access while they exist. `SharedArrayBuffer` is supported but unsynchronized — coordinate access explicitly.
- `DataView` reads/writes at byte offsets with explicit endianness (default big-endian; unaligned offsets allowed): `getInt8/16/32`, `getUint8/16/32`, `getFloat32/64` and the corresponding setters, with view-relative bounds checks.
- The currently exposed indexed value type is `number`.

Use `std/buffer` when the API needs CP3's byte-buffer encodings (`Buffer.from`/`toString`) — see [Buffer](./buffer.md). `SharedArrayBuffer` is not a synchronization primitive; concurrency helpers are separate language-level features.

Behavior details: [guide](../../standard-library/16-typed-arrays.md)
