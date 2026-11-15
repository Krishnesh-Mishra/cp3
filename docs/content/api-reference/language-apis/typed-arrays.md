---
title: Typed arrays
---
# Typed arrays

Typed arrays, `ArrayBuffer`, `SharedArrayBuffer`, and `DataView` are language builtins — no import. They model fixed-width binary storage and are not interchangeable with growable `T[]` or the `Buffer` type from `std/buffer`. See the [typed-arrays guide](../../standard-library/16-typed-arrays.md) for the full model.

## The families

| Class | Element width | Stored representation |
| --- | ---: | --- |
| `Int8Array` | 1 byte | signed 8-bit integer |
| `Uint8Array` | 1 byte | unsigned 8-bit integer |
| `Uint8ClampedArray` | 1 byte | unsigned 8-bit integer, clamped writes |
| `Int16Array` | 2 bytes | signed 16-bit integer |
| `Uint16Array` | 2 bytes | unsigned 16-bit integer |
| `Int32Array` | 4 bytes | signed 32-bit integer |
| `Uint32Array` | 4 bytes | unsigned 32-bit integer |
| `Float32Array` | 4 bytes | IEEE single precision |
| `Float64Array` | 8 bytes | IEEE double precision |
| `BigInt64Array` | 8 bytes | signed 64-bit storage |
| `BigUint64Array` | 8 bytes | unsigned 64-bit storage |

The currently exposed indexed value type is `number`. The BigInt-named arrays do not expose JavaScript `bigint`: stored 64-bit values convert through `double`, so integers above the exact binary64 range lose precision on read. Do not use them for exact large-integer arithmetic through indexed reads.

## Construction

Typed-array constructors accept one of three forms (checked by the compiler). Multi-byte elements require an aligned byte offset; invalid alignment and out-of-range view lengths are diagnosed by the constructor path (a runtime `RangeError`).

| Form | Example | |
| --- | --- | --- |
| Length | `new Float64Array(3)` | Allocates new backing storage; elements are zero-initialized. |
| Input array | `new Uint8Array([65, 66, 67])` | Allocates storage and writes the converted values. |
| Buffer + offset/length | `new Uint32Array(buffer, byteOffset?, length?)` | Creates a view over an existing `ArrayBuffer`. `byteOffset` must be aligned to the element width; `length` must fit the remaining bytes. |

`ArrayBuffer` allocates a fixed byte length: `new ArrayBuffer(byteLength)`. `SharedArrayBuffer` is the shared-storage family — sharing storage does not make accesses atomic or synchronized; coordinate explicitly. `DataView` is covered below.

## Writing and indexed access

Writes convert values to the element representation:

- Integer types truncate fractional values and wrap to their fixed-width range (`257` → `1` in a `Uint8Array`).
- Non-finite numbers convert to zero for integer storage.
- `Uint8ClampedArray` clamps to `[0, 255]` and rounds ties to even (`257` → `255`, `-1` → `0`).
- `Float32Array` rounds to single precision; `Float64Array` retains double.

| Operation | Result | Notes |
| --- | --- | --- |
| in-range read | `number` | |
| out-of-range read (negative, fractional, or ≥ `length`) | `undefined` | e.g. `const missing: number \| undefined = bytes[5]` |
| out-of-range write | ignored | |
| `subarray(begin?, end?)` | view over the same bytes | Writes through either view are shared. |
| `slice(begin?, end?)` | new independent copy | Own buffer. |
| `set(source, offset?)` | `undefined` | Copies values in; destination does not resize; a source that does not fit is a runtime `RangeError`. |
| `fill(value, start?, end?)` | the view | Writes the converted value to the range. |
| `copyWithin(target, start?, end?)` | the view | Moves elements within the view. |
| `reverse()` | the view | Reverses in place. |
| `indexOf(value, fromIndex?)` | `number` | Index of first match or `-1`. |
| `includes(value, fromIndex?)` | `boolean` | |
| `join(separator?)` | `string` | |
| `length`, `byteLength`, `byteOffset`, `buffer`, `BYTES_PER_ELEMENT` | | Describe the view. `buffer` is the backing `ArrayBuffer`; `length`/`byteLength`/`byteOffset` are counts, `BYTES_PER_ELEMENT` is a byte width. |

Out-of-range views and an invalid `set` offset fail through the checked error path (a runtime `RangeError` is reported and the process exits; this path is not catchable).

## ArrayBuffer and SharedArrayBuffer

| Member | Signature | Notes |
| --- | --- | --- |
| `new ArrayBuffer(byteLength)` | `ArrayBuffer` | Owns a fixed-length byte sequence; slice it to make a separate buffer. |
| `byteLength` | count | Byte length. |
| `slice` | `slice(begin?, end?): ArrayBuffer` | New, separate buffer copied from the selected range. |

`SharedArrayBuffer` supports the same constructor form. Neither the buffer nor the typed views synchronize concurrent access — if multiple workers or parallel tasks can touch the same bytes, coordinate explicitly.

## DataView

`DataView` interprets bytes in an `ArrayBuffer` at explicit byte offsets. Accesses are assembled from bytes, so unaligned offsets are supported. The default byte order for multi-byte accesses is **big-endian**; pass `true` as the last argument to select little-endian. The one-byte forms have no byte order.

| Member | Signature | Notes |
| --- | --- | --- |
| `new DataView(buffer, byteOffset?, byteLength?)` | `DataView` | Covers the whole buffer or a selected byte range. |
| `buffer`, `byteOffset`, `byteLength` | | `byteOffset`/`byteLength` describe the view; accesses are relative to the view, not the buffer start. |
| `getInt8` / `setInt8` | `(offset: number)` / `(offset: number, value: number)` | One byte; range check is view-relative and includes the full width. |
| `getUint8` / `setUint8` | same shape | One byte. |
| `getInt16` / `setInt16` | `(offset, [littleEndian?])` / `(offset, value, [littleEndian?])` | Two bytes; big-endian default. |
| `getUint16` / `setUint16` | same shape | Two bytes. |
| `getInt32` / `setInt32` | same shape | Four bytes. |
| `getUint32` / `setUint32` | same shape | Four bytes. |
| `getFloat32` / `setFloat32` | same shape | Four bytes. |
| `getFloat64` / `setFloat64` | same shape | Eight bytes. |

An access that starts inside the view but extends past its end is out of range; so is an access with a negative offset. Both fail through the checked error path (a runtime `RangeError`; the process exits; not catchable). Do not rely on host endianness — pass the flag explicitly for each multi-byte operation. See the [typed-arrays guide](../../standard-library/16-typed-arrays.md) for the binary-layout guidance.

## Related pages

- [Typed arrays guide](../../standard-library/16-typed-arrays.md)
- [Array](./array.md) — the growable `T[]` surface
- [std/buffer](../../standard-library/11-buffer.md) — byte buffers with text encodings
