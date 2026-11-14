---
title: Typed arrays, buffers, and DataView
chapter: Chapter 3 · Standard library
---
# Typed arrays, buffers, and DataView

Typed arrays, `ArrayBuffer`, `SharedArrayBuffer`, and `DataView` are built-in language families. They do not require a `std/typed-arrays` import. These types model fixed-width binary storage; they are not interchangeable with growable CP3 arrays or the `Buffer` type from `std/buffer`.

## Pick the right representation

| Representation | Storage and use |
| --- | --- |
| `T[]` | Growable sequence of CP3 values with one checked element type. |
| `Uint8Array` or another typed array | Fixed-width elements in a shared byte buffer. |
| `ArrayBuffer` | Owned byte storage, with no element interpretation by itself. |
| `DataView` | Byte-offset reads/writes for mixed widths and explicit endianness. |
| `Buffer` | CP3 byte-buffer API with text encoding conversion. |

Use a typed array when the data has one uniform numeric representation. Use `DataView` when a binary record mixes widths, uses explicit byte order, or has fields at byte offsets. Use `Buffer` when the API needs CP3's byte-buffer methods and string encodings.

## Supported typed-array classes

The runtime defines these element types:

| Class | Element width | Stored representation |
| --- | ---: | --- |
| `Int8Array` | 1 byte | signed 8-bit integer |
| `Uint8Array` | 1 byte | unsigned 8-bit integer |
| `Uint8ClampedArray` | 1 byte | unsigned 8-bit integer with clamped writes |
| `Int16Array` | 2 bytes | signed 16-bit integer |
| `Uint16Array` | 2 bytes | unsigned 16-bit integer |
| `Int32Array` | 4 bytes | signed 32-bit integer |
| `Uint32Array` | 4 bytes | unsigned 32-bit integer |
| `Float32Array` | 4 bytes | IEEE single precision |
| `Float64Array` | 8 bytes | IEEE double precision |
| `BigInt64Array` | 8 bytes | signed 64-bit storage |
| `BigUint64Array` | 8 bytes | unsigned 64-bit storage |

The currently exposed indexed value type is `number`. In particular, the BigInt-named arrays do not expose JavaScript `bigint` values: the runtime converts their stored 64-bit values through `double`, so integers above the exact range of binary64 can lose precision when read. The conformance tests explicitly pin rounding beyond `2^53`. Do not use these classes for exact large-integer arithmetic through CP3 indexed reads.

## Construction

A typed array can be created with a length, an array of initial values, or another supported typed-array/buffer form:

```ts
const samples = new Float64Array(3);
samples[0] = 1.25;

const codes = new Uint8Array([65, 66, 67]);
```

The constructor form determines whether it allocates new backing storage or creates a view over an existing `ArrayBuffer`. The accepted overloads are checked by the compiler. A typed array has a fixed element count; writing elements does not grow it.

`ArrayBuffer` allocates a byte length. A typed view can specify an offset and, where supported, a view length. Multi-byte typed elements require an aligned byte offset; invalid alignment and out-of-range view lengths are diagnosed at runtime by the constructor path.

## Numeric conversion on writes

Writes convert values to the typed element representation. Integer types truncate fractional values and wrap to their fixed-width range. Non-finite numeric values convert to zero for integer array storage. `Uint8ClampedArray` clamps values to `[0, 255]` and rounds ties to even.

Floating-point arrays convert to their element precision. `Float32Array` rounds to single precision; `Float64Array` retains double precision. These conversions occur on writes and are part of the typed-array behavior, not an optional compiler optimization.

For example, a value written to an unsigned 8-bit array wraps modulo 256, while the same value written to a clamped array is clamped:

```ts
const wrapped = new Uint8Array([257, -1]);
const clamped = new Uint8ClampedArray([257, -1]);
```

The test cases cover wrap, clamp, NaN handling, and typed writes. Use the smallest example matching the target class before depending on an edge case.

## Indexed access and out-of-range behavior

Typed-array indexing is zero-based. An in-range read returns a number; a read that is negative, fractional, or out of bounds yields `undefined` according to the tested behavior. Out-of-range writes are ignored. The compiler can narrow some reads when a literal index or loop range is proven in bounds, but normal source semantics remain checked.

```ts
const bytes = new Uint8Array(2);
const missing: number | undefined = bytes[5];
bytes[5] = 99; // ignored by the typed-array write path
```

`length`, `byteLength`, `byteOffset`, `buffer`, and `BYTES_PER_ELEMENT` describe the view. `buffer` refers to the backing buffer. A subarray shares storage; a slice creates a copied view with its own buffer according to the current runtime behavior.

## Typed-array methods

The checked surface includes `subarray`, `slice`, `copyWithin`, `reverse`, `indexOf`, `includes`, `join`, `set`, and `fill`.

- `subarray(begin?, end?)` makes another view into the same bytes. Writes are visible through either view.
- `slice(begin?, end?)` copies the selected elements into independent storage.
- `copyWithin(target, start?, end?)` moves elements within the existing view and returns that view.
- `reverse()` reverses elements in place.
- `indexOf(value, fromIndex?)` returns the matching index or `-1`.
- `includes(value, fromIndex?)` reports whether the value occurs.
- `join(separator?)` renders a string from the elements.
- `set(source, offset?)` copies values into an existing view; it does not resize the destination.
- `fill(value, start?, end?)` writes a converted value to the selected range and returns the receiver.

Mutating methods modify the shared backing bytes. A `const` view binding does not make those bytes immutable. If two views overlap, write order matters.

## ArrayBuffer and SharedArrayBuffer

`ArrayBuffer` owns a fixed-length byte sequence. It can be sliced to make a separate buffer, then viewed as a typed array or DataView. Views retain access to the same storage while they exist.

`SharedArrayBuffer` is the shared-storage family supported by the runtime. Sharing storage does not make every access atomic or synchronized. If multiple workers or parallel tasks can touch the same bytes, coordinate access using the supported concurrency mechanisms. See [Thread safety](../02-runtime/10-thread-safety.md).

## DataView construction

`DataView` interprets bytes in an `ArrayBuffer`. It can cover the whole buffer or a selected byte range:

```ts
const storage = new ArrayBuffer(16);
const view = new DataView(storage, 4, 8);
```

The `byteOffset` and `byteLength` properties describe the view. A DataView access offset is relative to that view, not the start of the entire buffer. Each access checks that its full requested width fits within the view.

## DataView reads and writes

The runtime implements signed and unsigned integer reads and writes, plus floating-point 32-bit and 64-bit forms. The available forms include `getInt8`, `getUint8`, `getInt16`, `getUint16`, `getInt32`, `getUint32`, `getFloat32`, `getFloat64` and corresponding setters where defined by the runtime.

For multi-byte access, the optional boolean selects little-endian when `true`. The default is big-endian. The one-byte forms have no meaningful byte order. DataView supports unaligned byte offsets because accesses are assembled from bytes rather than assuming the address meets a native type's alignment.

```ts
const view = new DataView(new ArrayBuffer(8));
view.setUint32(1, 0x12345678, true);
const value = view.getUint32(1, true);
```

The bounds check is view-relative and includes the entire element width. An access that starts inside the view but extends past its end is still out of range.

## Binary layout and endianness

Do not rely on host endianness when a file or network protocol specifies byte order. Pass the endianness argument explicitly for each multi-byte DataView operation. Typed arrays use the runtime's native typed storage interpretation; DataView is the appropriate API for an explicitly serialized byte order.

When defining a file format, document offsets, widths, signedness, and endianness next to the code. For example, an integer field at byte 4 with four bytes should be read using the matching 32-bit method and the protocol's stated endianness.

## Compiler optimizations and guarantees

The compiler has narrow range-analysis facts for some typed-array indices. A fact can remove an unnecessary union with `undefined` in the type system for a proven case. This is distinct from general raw-pointer lowering.

The current DataView analysis records some possible loop facts, but the facts are not consumed by the per-read emitter. Therefore this compiler does not currently guarantee that a DataView helper call disappears or that a numeric loop is vectorized. Check the generated C++ for the exact loop and require a backend vectorization remark that points to that loop before claiming SIMD.

If the compiler cannot prove an index safe, the checked path preserves out-of-range behavior. Do not modify benchmark input or disable checks to manufacture a faster result. See [Optimization pipeline](../04-compiler-cli/03-optimization-pipeline.md) and [Inspecting generated C++](../04-compiler-cli/10-generated-cpp-inspection.md).

## Test families to consult

The typed-array tests cover construction, element conversion, typed writes, buffer sharing, byte offsets, copies versus shared subarrays, bounds, and DataView byte order and range checks. Important cases include:

- typed read out of range returns `undefined`;
- typed write out of range is ignored;
- fractional and negative indices are rejected or handled as pinned by the case;
- a subarray shares bytes with the original view;
- a slice has independent storage;
- a DataView validates the complete read/write width;
- a multi-byte DataView can use an unaligned offset;
- a multi-byte typed-array view enforces element alignment;
- 64-bit BigInt-named storage read as `number` may round.

These are semantic constraints for optimization: removing a check is correct only if the compiler proves the corresponding access valid.

## Related pages

- [Byte buffers](./11-buffer.md)
- [Arrays](../01-language/04-values-and-objects/04-arrays.md)
- [Typed arrays in the runtime](../02-runtime/06-typed-arrays.md)
- [Thread safety](../02-runtime/10-thread-safety.md)
- [Performance measurements](../00-welcome/02-performance.md)
