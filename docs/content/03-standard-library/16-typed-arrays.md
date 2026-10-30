---
title: Typed arrays, buffers, and DataView
chapter: Chapter 3 · Standard library
---
# Typed arrays, buffers, and DataView

Typed arrays are built-in language families. They provide fixed-width numeric views over binary storage and do not use a `std/typed-arrays` import.

## Available element types

CP3 supports `Int8Array`, `Uint8Array`, `Uint8ClampedArray`, `Int16Array`, `Uint16Array`, `Int32Array`, `Uint32Array`, `Float32Array`, and `Float64Array`. Integer arrays convert values to their fixed-width representation; floating-point arrays use the corresponding precision.

Typed arrays have a fixed element count after construction. Ordinary arrays grow dynamically and store general CP3 values; choose the representation based on whether the data is numeric and fixed-width.

## ArrayBuffer and views

`ArrayBuffer` owns raw bytes. Typed arrays and `DataView` expose interpretations of those bytes. A view may begin at a byte offset and has a bounded length. The view's element type controls width and conversion.

```ts
const storage = new ArrayBuffer(32);
const bytes = new Uint8Array(storage);
```

Out-of-range operations follow their checked runtime semantics. Do not use an unchecked C++ pointer assumption in user code; the compiler may only remove checks when its proof is valid.

## DataView

`DataView` reads and writes integers and floating-point values at byte offsets. Unlike typed arrays, a DataView can mix element widths in one buffer. Multi-byte access accepts the supported endianness parameter; the default is big-endian and `true` requests little-endian.

```ts
const view = new DataView(storage);
view.setUint32(0, 0x12345678, true);
const value = view.getUint32(0, true);
```

The view checks that the requested byte range fits. Unaligned byte offsets are supported by the runtime's byte-safe load/store path.

## SharedArrayBuffer

`SharedArrayBuffer` provides storage that can be shared across supported worker/concurrency paths. Shared memory introduces synchronization requirements. A raw read/write sequence is not automatically atomic; use the language's supported atomic operations and coordination mechanisms where required.

## Performance and compiler guarantees

The compiler contains analysis scaffolding for a narrow class of fixed-size DataView loops. It records facts about candidate views and loops, but the recorded `fastDataViewReads_` facts are not currently consumed by the per-read code generator. As a result, the presence of analysis code does not show that hot-loop helper calls were removed or that the backend vectorized the loop.

Treat vectorization as proven only after inspecting generated C++ for direct contiguous loads and checking a GCC/Clang vectorization report for the benchmark source itself. A report from a C++ standard-library header is not evidence that the CP3 loop was vectorized. Bounds checks remain on paths where the compiler cannot prove safety.

## Choosing a representation

| Need | Use |
| --- | --- |
| Growable values of one CP3 type | Ordinary array such as `number[]` |
| Dense fixed-width numeric data | Matching typed array |
| Byte-oriented storage and mixed numeric widths | `ArrayBuffer` plus `DataView` |
| Text or binary APIs that use Buffer | `Buffer` from `std/buffer` |

See [Byte buffers](#03-standard-library/11-buffer) for Buffer construction and text encodings.
