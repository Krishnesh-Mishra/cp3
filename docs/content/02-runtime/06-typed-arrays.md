---
title: Typed arrays and DataView
chapter: Chapter 2 · Runtime and memory
---
# Typed arrays and DataView

CP3 includes fixed-width typed arrays, `ArrayBuffer`, `SharedArrayBuffer`, and `DataView` as language-level families. These are not imported from a `std/typed-arrays` module.

## Typed-array classes

The runtime implements signed and unsigned 8-, 16-, and 32-bit integer views, `Uint8ClampedArray`, `Float32Array`, and `Float64Array`. A typed view has a fixed element width and reads or writes values using that element’s conversion rules.

Construct a view from an element count or a supported backing buffer form. Byte offsets must meet the element alignment requirement. Lengths and offsets are checked against the underlying buffer; invalid construction follows the typed-array range-error path.

## `ArrayBuffer` and `SharedArrayBuffer`

Both expose fixed-size byte storage in the runtime. A typed view can refer to a byte offset and element count over that storage. `SharedArrayBuffer` is a distinct class type, but its existence does not make arbitrary concurrent writes safe; use the concurrency model and native synchronization rules.

## `DataView`

`DataView` provides byte-offset reads and writes for multiple numeric widths. Multibyte operations use byte-wise access in the runtime, so unaligned offsets are legal where the view bounds permit them. The default byte order is big-endian; pass the supported `littleEndian` flag explicitly when little-endian data is required.

```ts
const view = new DataView(buffer);
const count = view.getUint32(0, true); // little-endian
```

Offsets are relative to the view, not the full buffer. A read is valid only if its entire width falls within the view. An invalid access retains the checked runtime behavior.

## Current loop optimization status

The compiler contains analysis for narrow zero-based loops that reduce `DataView.getUint8` or `getFloat64` with a constant bound and unit increment. It also checks that a view is backed by a fresh, fixed-size typed-array allocation whose uses do not escape or alias unexpectedly.

The loop emitter currently has code to hoist a raw `rawData` pointer and check the proven range before the loop. However, the current per-read lowering is not connected to the recorded fast-read fact (`fastDataViewReads_` has no consumer in the code generator). Therefore this scaffold does **not** establish that helper calls disappear or that the loop vectorizes. Treat the optimization as incomplete until generated C++ and a backend vectorization report show the direct hot-loop load.

The separate typed-array fill analysis only handles tightly recognized fill loops and still preserves a range check. Neither analysis is a general SIMD guarantee. See [optimization pipeline](../04-compiler-cli/03-optimization-pipeline.md) for how to verify the actual emitted code.

## Typed-array index behavior

Typed-array indexed reads return `undefined` for a negative, fractional, or out-of-range index. Indexed writes outside the valid element range are ignored. In-range writes convert the source `number` to the destination element representation. For example, an integer array wraps or narrows according to its element type; `Uint8ClampedArray` clamps to its supported byte range and has dedicated conversion cases.

The index and value are checked as separate questions. A program may write an in-range value that needs numeric conversion; a non-numeric assignment is rejected by the checker where it can determine that statically. The tests under `tests/cases/typed-arrays/03/typed/index` pin out-of-range reads and writes, fractional reads, and write conversions.

## Construction and view bounds

Typed arrays can be created with an element count, from an input array, or as views over an `ArrayBuffer`/`SharedArrayBuffer` using the supported overloads. When a view is built from a buffer, offsets are measured in bytes and lengths in elements. The byte range must fit the backing storage, and offsets must satisfy alignment for the element width. Remainders and overflows that cannot describe a whole view are rejected.

A typed view retains access to its backing storage. Two views over overlapping byte ranges can observe writes through either view. `subarray` creates a view sharing the existing storage; `slice` creates copied storage. These methods therefore differ in aliasing as well as allocation cost.

## Numeric element conversion

Integer typed arrays use fixed-width element conversion when values are written or supplied in constructor input. Signed and unsigned arrays have distinct interpretation. `Uint8ClampedArray` follows clamping behavior rather than integer wraparound. `Float32Array` rounds to 32-bit floating precision; `Float64Array` stores 64-bit floating values.

Do not use typed arrays as arbitrary-precision integer containers. For exact 64-bit integer arithmetic use the native scalar type support when appropriate, or the specific typed-array family CP3 provides. A floating-point `number` can lose integer precision before it reaches a typed-array conversion.

## Shared storage and concurrency

`SharedArrayBuffer` gives multiple views access to shared bytes in supported worker/concurrency paths. Sharing storage does not make a compound read-modify-write operation atomic. Use the atomic operations and worker/parallel rules that CP3 actually supports; do not infer thread safety from the class name.

When reasoning about a typed view, track both the view object and its backing buffer. Another alias can write the same bytes even if the current variable is never assigned again. This aliasing matters to correctness proofs and determines whether a compiler can safely hoist or vectorize reads.

## DataView bounds and endianness

A DataView uses byte offsets and supports mixed access widths. The complete access width must fit within the view. Multibyte accesses are byte-safe and can use unaligned offsets, unlike a typed-array view whose start offset must meet its element alignment. The default byte order is big-endian; pass `true` for little-endian when the data format requires it.

Out-of-range DataView reads and writes take the checked runtime error path. This differs from typed-array indexed reads, which yield `undefined`, and typed-array out-of-range writes, which are ignored. Keep this distinction explicit when porting JavaScript code.

## Backing pointer lifetime

A raw pointer to backing storage is valid only while the buffer remains alive and its storage does not move. A typed-array view over fixed-size ArrayBuffer storage does not resize that backing allocation through its normal API, but aliases can still mutate its bytes. Hoisting a pointer is only safe if the compiler proves lifetime, bounds, stride, and no invalidating mutation for the whole loop.

The current DataView analysis records narrow candidates, but the per-read emitter does not consume the recorded facts. Consequently, the generated hot loop may still call a checked helper and must be inspected directly. Do not report SIMD from a generic optimizer remark or from the analysis pass existing in source.

## Related test families

- `tests/cases/typed-arrays/03/typed/index` covers indexed reads, writes, and coercion.
- `tests/cases/typed-arrays/03/subarray` checks shared backing storage.
- `tests/cases/typed-arrays/03/slice` checks copied storage.
- `tests/cases/typed-arrays/03/view` checks alignment, byte offsets, lengths, and shared buffers.
- DataView cases cover widths, byte order, and invalid offsets.

Use both run-mode tests and generated C++ inspection for performance changes. Correct output alone does not show that checks or helper calls were removed.
