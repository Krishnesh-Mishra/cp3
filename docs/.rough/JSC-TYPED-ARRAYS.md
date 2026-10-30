# JS-compat typed arrays - spec amendment proposal (JS0)

Status: **proposal for user review. No spec or source change is made by this
file.** It feeds the queued implementation wave ("JS-compat typed-array
surface", `M2-PLAN.md`). Home: `docs/rough` (rough work); the resulting
amendments land in `docs/language-spec/`, corpus cases in `tests/cases/`.

Basis:

- User directive 2026-10-18: JS-dev-friendly creation patterns plus the full
  family - `Array(1000).fill("your string")`, `new Float32Array(1000)`,
  `new Float32Array([1.1, 2.2, 3.3, 4.4])`, and the classes `Array`,
  `Int8Array`, `Uint8Array`, `Uint8ClampedArray`, `Int16Array`, `Uint16Array`,
  `Int32Array`, `Uint32Array`, `Float32Array`, `Float64Array`,
  `BigInt64Array`, `BigUint64Array`, `ArrayBuffer`, `SharedArrayBuffer`,
  `DataView`.
- Lead decision already recorded (`M2-PLAN.md`, commit `203b57c`): backing is
  **fixed-size** - an owned heap block + stored length for runtime sizes, the
  native fixed-array mapping when sizes are compile-time known - **never a
  resizing vector**; typed arrays have no delete and no resize; `const` keeps
  its JS meaning (no rebinding, contents mutable).
- House style (D66.4): no em or en dashes in repo text.

Every typed-array/AB class is a **core-language builtin, no import** (same
bucket as `Array` methods, `parseInt`, `setTimeout`: `docs/standard-library/03`).
`Array` itself (the regular resizing array) already exists; this proposal adds
`fill`/`Array.of`/`Array.from` to it and pins `Array(n)`.

## 1. Surface, per class

### 1.1 Family and element mapping

Element storage is the native scalar type; the JS-visible element value is
always `number` (or `undefined`, see 1.4). This is the class-name-to-native
mapping the wave implements.

| JS class | native element | bytes | write conversion | read result |
|---|---|---|---|---|
| `Int8Array` | i8 | 1 | ToIntegerOrInfinity, wrap mod 2^8 | sign-extended `number` |
| `Uint8Array` | u8 | 1 | same, unsigned | zero-extended `number` |
| `Uint8ClampedArray` | u8 | 1 | ToUint8Clamp: NaN -> 0; <= 0 -> 0; >= 255 -> 255; else round half to even | `number` |
| `Int16Array` | i16 | 2 | wrap mod 2^16 | `number` |
| `Uint16Array` | u16 | 2 | same, unsigned | `number` |
| `Int32Array` | i32 | 4 | wrap mod 2^32 | `number` |
| `Uint32Array` | u32 | 4 | same, unsigned | `number` |
| `Float32Array` | f32 | 4 | to IEEE single (round to nearest, ties to even); -0 and NaN preserved | f32 widened to `number` (exact) |
| `Float64Array` | f64 | 8 | as-is | `number` |
| `BigInt64Array` | i64 | 8 | number accepted with the 2^53 caveat (section 5) | `number` (may round) |
| `BigUint64Array` | u64 | 8 | same caveat | `number` (may round) |

Write conversions map 1:1 to C++ `static_cast` plus an explicit modulo (the
mod-and-truncate steps are not implicit C++ conversion behavior and must be
emitted). All classes expose static `BYTES_PER_ELEMENT` (compile-time constant,
1/2/4/8) and `byteLength = length * BYTES_PER_ELEMENT`.

### 1.2 Constructor forms

Both `new C(...)` and `C(...)` (no `new`) spell the same call, for `Array` and
for every class below (JS parity; `new` is preferred in docs). `new C()` is the
empty array.

| form | behavior |
|---|---|
| `C(length)` | `length` must be `number`; ToIndex (truncate toward zero; NaN -> 0); a literal negative/Infinity value is a compile error, a runtime value panics (JS RangeError). Fresh, zero-filled buffer; length = the computed count. |
| `C(array)` | `array` is an array literal or a `number[]` value; element-wise copy with the class write conversion; fresh buffer. |
| `C(typedArray)` | element-wise copy with conversion into a fresh buffer (same class included: real copy, new buffer). |
| `C(buffer)` | view over the whole buffer; `buffer.byteLength` must divide evenly by the element size, else panic. |
| `C(buffer, byteOffset)` | view from `byteOffset`; remainder must divide evenly, else panic. |
| `C(buffer, byteOffset, length)` | explicit view; `byteOffset % size == 0` else panic; `byteOffset + length * size <= buffer.byteLength` else panic. |
| `C(x)` for any other `x` | compile error (no scalar coercion; this is a named divergence from JS, where `C("5")` is length 5). |

The three view forms accept `ArrayBuffer` and `SharedArrayBuffer` (section 5).
View constructors do not copy: `view.buffer` is the passed buffer. When a
violation is statically provable (literal buffer size/offset/length), it is a
compile error; otherwise it panics at runtime.

### 1.3 Properties

| property | type | notes |
|---|---|---|
| `.length` | `number` | element count; read-only (assignment is a compile error; JS silently ignores it). |
| `.byteLength` | `number` | `length * BYTES_PER_ELEMENT`. |
| `.byteOffset` | `number` | bytes from the start of `.buffer`; always a multiple of the element size. |
| `.buffer` | `ArrayBuffer` | the backing buffer; same handle for every view over it. |
| `C.BYTES_PER_ELEMENT` | `number` | static, compile-time constant. |

### 1.4 Indexed read/write

| expression | static type | runtime |
|---|---|---|
| `arr[i]` read, `i: number` | `number \| undefined` (see Q5) | in range: the element value as `number`; otherwise `undefined` (JS parity) |
| `arr[i] = v`, `v: number` | void | in range: store with the class write conversion; otherwise ignored (no-op; JS parity) |
| `arr[i] = v` where `v` is not `number` | compile error | JS would coerce `undefined` to NaN/0; cp3 refuses (D8 spirit) |
| `arr["0"]`, expando property | compile error | cp3 objects are fixed-shape; JS treats these as ordinary properties |

Extra rules: `i` non-integral or NaN reads `undefined` (and a write is ignored).
JS `arr[1.5]` is an ordinary property read, so `undefined` matches; the write
diverges (JS creates an expando property, cp3 ignores it - document it).
Refinement (recommended, implementation may defer): when `i` is a numeric
literal and the length is compile-time known, an in-range literal read types as
`number` with no `undefined` branch (`new Float64Array([2])[0] + 1` compiles).

### 1.5 Methods

Proposed set for this wave (all classes, same semantics per class):

| method | include now | semantics |
|---|---|---|
| `set(source, offset?)` | yes (core) | copy `number[]`/typed-array elements into `this` at element `offset` (default 0), with conversion; statically provable overflow is a compile error, otherwise panic (JS RangeError). |
| `subarray(begin?, end?)` | yes (core) | new view, same buffer; begin/end normalized (negative wraps from the end, clamped); `byteOffset = this.byteOffset + begin*size`. |
| `slice(begin?, end?)` | yes (core) | new array with a **new buffer**; same index normalization; copy. |
| `fill(value, start?, end?)` | yes (core) | in-place fill, conversion applies; returns `this` (the same handle), JS parity. |
| `copyWithin(target, start?, end?)` | yes (cheap) | in-place overlapping copy (memmove); returns `this`. |
| `reverse()` | yes (cheap) | in-place; returns `this`. |
| `indexOf(v, from?)` | yes (cheap) | strict equality; `NaN` is never found (-1), JS parity. |
| `includes(v, from?)` | yes (cheap) | SameValueZero; `NaN` **is** found, JS parity. |
| `join(sep?)` | yes (cheap) | default `","`; element text via the D9 number formatter. |
| `sort`, `map`, `filter`, `forEach`, `reduce`, `find`, `findIndex`, `some`, `every` | defer | need array/closures codegen (wave 3) and, for `sort`, comparator lowering; follow the arrays wave. |
| `lastIndexOf`, `at` | defer | convenience only; not needed for the directive. |
| `keys`/`values`/`entries`, `[Symbol.iterator]`, `for...of` | defer | no iterator protocol is specified yet. |
| statics `C.from` / `C.of` | defer | `Array.from`/`Array.of` (section 3) cover the patterns for now. |

`fill` is also added to the **regular** array method list (section 3). The
cheap set may be dropped by the wave owner if the wave runs long; the core four
are the contract.

## 2. Native storage, types, interop

Runtime shape (new header-only runtime types; keeps the D56 required-TU set
unchanged):

```
ArrayBuffer:      Ref<ArrayBuffer>   { byteLength; owned block }
block:            std::unique_ptr<std::byte[]>   (runtime length)
                  std::array<std::byte, N>       (compile-time-known length)
TypedArray<E>:    Ref<TypedArray<E>> { Ref<ArrayBuffer> buffer;
                                       std::size_t byteOffset, length; }
                  Ref<TypedArray<E, N>> when the length is compile-time known
DataView:         Ref<DataView>      { buffer, byteOffset, byteLength }
```

- Never `std::vector`, never resized or reallocated after construction. Fresh
  buffers are zero-initialized and allocated aligned to at least 8 bytes, so
  every element type through f64/i64 is naturally aligned when `byteOffset` is
  a multiple of its size.
- Compile-time-known lengths (`C(1000)`, `C([1, 2, 3])`) use the
  `TypedArray<E, N>` form: `.length` is `N`, bounds checks constant-fold. This
  is the "native fixed-array mapping" of the lead decision; observable
  semantics are identical to the runtime-length form.
- Element access uses memcpy-based load/store helpers (aliasing- and
  alignment-safe), platform byte order - JS parity.
- Typed arrays, buffers and views are **reference types** in the §10 sense
  (`Ref<T>`, handle-shared, ARC-freed); `const` blocks rebinding only
  (contents mutable), exactly as `const_array_mutable` does for arrays today.

Interop rules (assignment compatibility):

| from -> to | rule |
|---|---|
| `number[]` (regular array) -> typed array | explicit copy: `new C(arr)` or `C(arr)`; never implicit. |
| typed array -> `number[]` | explicit copy: `Array.from(ta)`; never implicit (a view cannot resize, an array can). |
| typed array -> same class | assignable directly (same type; handles share). |
| typed array -> different class | explicit copy: `new C(other)`; no implicit assignability, even widening f32 -> f64. |
| typed array element <-> `number` | reads: `number \| undefined`; writes: `number` with the class conversion (1.1). |
| `ArrayBuffer` <-> view construction | view constructors and `.buffer` (back-reference); no implicit conversion to/from `number[]`. |
| future native-element arrays (`i8[]`, `f32[]`, `r_'i64'[]`, native-type wave) | the JS class names are **not** aliases for native-element arrays; interop is construction-copy with conversion. Direct assignment is deferred to the native-type wave. |

Regular arrays stay `std::vector<E>` (`Array<E> = Ref<std::vector<E>>`,
`value.hpp`); typed arrays are a separate, fixed-size path. No change to
regular-array storage, `push`/`pop`, or `.length` write behavior (the parked
`array_length_write` marker stays with the arrays wave).

## 3. `Array(n)`, `.fill(v)`, `Array.of`, `Array.from`

JS semantics kept: creation forms are separate from the array literal; `fill`
returns the receiver; ranges normalize with negative wrap; `Array.of` and the
multi-argument form build literals.

Deliberate divergences (both static-typing consequences, both to document):

1. **No holes.** `Array(n)` is a dense array of length `n`; every element is
   the element type's default value (`number` -> 0, `string` -> `""`,
   `boolean` -> false). JS `Array(3)` has holes and `a[0]` is `undefined`; in
   cp3 there is no hole state. This resolves the parked marker
   `control-flow/_ambiguous/dense_array_holes` (promote per D58).
   `Array(n)` for an element type with no default (any reference type, e.g.
   `number[][]`) is a compile error unless the call is immediately chained
   `.fill(v)`.
2. **Element-type inference.** `Array(n)` has no element type of its own; it
   takes it from (a) the declared target type, or (b) the immediately chained
   `.fill(v)` argument (widened). `const a = Array(3);` with neither is a
   compile error: annotate (`let a: string[] = Array(1000);`) or chain
   `.fill(...)`. All three directive patterns compile:
   `Array(1000).fill("your string")` -> `string[]`,
   `let a: number[] = Array(1000);` -> 1000 zeros.
3. **`Array(n)` argument rules.** Single `number` argument = length;
   ToIndex = truncate; a literal negative/non-integral value is a compile
   error (JS `Array(2.5)` throws RangeError; `Array(-1)` too), a runtime value
   is checked with the same panic. `Array(a, b, ...)` with 2+ arguments is the
   literal form (JS parity). A single non-number argument (`Array("x")`, JS:
   `["x"]`) is a compile error pointing at `[x]` - no scalar coercion.

`fill(value, start?, end?)` on regular arrays: value must be assignable to the
element type (no coercion; `["a"].fill(0)` is a compile error), ranges
normalize with negative wrap and clamp, returns the receiver. Added to the
array method table in §09.

`Array.of(...)` = the literal form (zero or more args); `Array.from(x)`:
include **only** the copy form where `x` is `number[]` or a typed array
(element-wise copy to a fresh regular array). Other sources (strings, `Map`,
iterables) are a compile error naming deferred iterable support
(`for...of`/`Iterator<T>` is unspecified).

## 4. ArrayBuffer and DataView

**ArrayBuffer**

- `new ArrayBuffer(byteLength)`: ToIndex (truncate; negative/Infinity panic;
  NaN -> 0); zero-filled; `.byteLength`. `ArrayBuffer(0)` is valid.
- `.slice(begin?, end?)`: copy into a new buffer, negative-wrap/clamp
  normalization (JS parity). Cheap; include.
- No `resize`, no `maxByteLength`, no transfer/detach, no `ArrayBuffer.isView`
  in this wave: resizable/transferable buffers contradict the fixed-size
  decision and are deferred (record in the deferred doc).

**DataView**

- `new DataView(buffer, byteOffset?, byteLength?)`: view over a byte range;
  `byteOffset` need **not** be aligned; defaults 0 and
  `buffer.byteLength - byteOffset`; out-of-bounds -> panic (JS RangeError).
- `.buffer`, `.byteOffset`, `.byteLength` as above.
- Getters: `getInt8`, `getUint8`, `getInt16`, `getUint16`, `getInt32`,
  `getUint32`, `getFloat32`, `getFloat64`, `getBigInt64`, `getBigUint64`.
  Setters: the matching `set*` with `(byteOffset, value[, littleEndian])`.
- **Endianness: default big-endian, matching JS.** `littleEndian` is an
  optional trailing `boolean`; omitted or `false` means big-endian, `true`
  means little-endian (the 8-bit accessors take no flag). Correction to the
  wave brief, which said "defaulting little-endian like JS": JS actually
  defaults to **big-endian**; typed arrays themselves use platform byte order
  (little-endian on every target cp3 currently emits for), exactly as in JS.
  See Q3.
- Bounds: reads/writes are checked against the **view's** byteLength, always
  (not a debug/release split); violation -> panic. This is the byte-level API
  and the check is a compare; the debug/release split stays with regular
  array bounds checks.
- Unaligned offsets must work (assemble bytes explicitly; no
  `reinterpret_cast` on arbitrary offsets).

`SharedArrayBuffer` is covered in section 5. `std/buffer`'s `Buffer` is
unchanged: it remains the text/encoding convenience type; ArrayBuffer/typed
arrays are the raw byte substrate. Cross-linking `Buffer` to ArrayBuffer is
deferred.

## 5. BigInt64Array / BigUint64Array / SharedArrayBuffer (open questions)

cp3 has **no BigInt type today** (no `123n` literal, no `typeof "bigint"`, no
bigint arithmetic), and the runtime is a **single-threaded event loop** today
(`worker`/`parallel.*` are later waves; no locks or atomics are ever exposed,
§13).

**BigInt64Array / BigUint64Array options**

| option | consequence |
|---|---|
| A. Include with a documented numeric caveat (recommended) | Classes, views, buffers and coerce-by-number all land now; exact wide values work through DataView bytes; values above +/-2^53 are rounded because the surface is `number`. Writes accept `number` (a divergence from JS `ToBigInt`, which rejects numbers). |
| B. Add a BigInt primitive first | JS-exact element reads/writes, but a full spec wave of its own: literal syntax, `typeof "bigint"`, arithmetic/comparison/printing/JSON/conversion rules, plus codegen and runtime. Too large to ride this wave. |
| C. Defer the two classes | the family has a hole; u64/i64 storage mapping and buffer views would need a different spelling. |

Recommendation: **A**. Corpus cases pin the exact behavior including the
rounding edge (section 6), and the doc states plainly that JS would return
`BigInt` here - a named deviation until BigInt lands. If B is chosen later,
this surface upgrades in place (read/write types become `bigint`); nothing
else in this proposal changes. **Q1.**

**SharedArrayBuffer options**

| option | consequence |
|---|---|
| A. Include as a distinct class with ArrayBuffer-identical semantics today (recommended) | The name exists and is the forward-compatible anchor; a view's `.buffer` is typed `ArrayBuffer`; both buffer classes are accepted by every buffer-taking constructor. Nothing shared is observable while `worker` deep-copies and `parallel.*` does not exist, so semantics today are exactly ArrayBuffer's. |
| B. Defer until real sharing exists | honest, but leaves the directive's class list incomplete and forces JS code that mentions it to change later. |
| C. Implement real shared memory + atomics now | contradicts §13 (no locks/atomics exposed) and the current runtime; not in scope. |

Recommendation: **A**, documented as "identical to ArrayBuffer until
`worker`/`parallel` can share; true shared-memory semantics and any
synchronization surface are deferred and will be its own spec ruling". When
that ruling comes, only `.buffer`'s static type and the cross-thread
permission change. **Q2.**

## 6. Corpus case list (the oracle for the implementation wave)

Layout: regular-array creation cases in `tests/cases/arrays-strings/` (the
existing home of regular arrays; wave owner must be granted that folder per
`tests/cases/README.md`), everything else in a new category
`tests/cases/typed-arrays/`. Harness schema unchanged. Runtime-panic cases
assert the exit code only (the runner cannot match stderr in `run` mode);
message text stays spec prose.

### 6.1 Regular arrays: `Array(n)`, `fill`, `of`, `from` (`arrays-strings/`)

| case | one-line expectation |
|---|---|
| `array_ctor_fill_string` | `Array(3).fill("x")` joins `x,x,x`. |
| `array_ctor_fill_number` | `Array(3).fill(0)` then `a[1] = 9` joins `0,9,0`. |
| `array_ctor_annotated_zero_init` | `let a: number[] = Array(3)` joins `0,0,0`; no holes exist. |
| `array_ctor_annotated_string_default` | `let s: string[] = Array(2)` joins `,` (two empty strings). |
| `array_ctor_zero` | `Array(0).fill(1)` has length 0. |
| `array_ctor_multiarg_literal` | `Array(1, 2, 3)` joins `1,2,3`. |
| `array_ctor_new_spelling` | `new Array(2).fill("a")` joins `a,a`. |
| `array_ctor_single_nonnumber_error` | `Array("x")` is a check error (suggest `[x]`). |
| `array_ctor_negative_error` | `Array(-1)` is a check error. |
| `array_ctor_fractional_error` | `Array(2.5)` is a check error. |
| `array_ctor_uninferable_error` | `const a = Array(3);` is a check error (annotate or `.fill`). |
| `array_ctor_reference_element_error` | `Array(2)` for `number[][]` without `.fill` is a check error. |
| `array_fill_range` | `[1,2,3,4].fill(9, 1, 3)` joins `1,9,9,4`. |
| `array_fill_negative_start` | `.fill(0, -2)` zeroes the last two elements. |
| `array_fill_returns_receiver` | the returned handle aliases the receiver (write through it is visible). |
| `array_fill_wrong_type_error` | `["a"].fill(0)` is a check error (no coercion). |
| `array_of_values` | `Array.of(1, 2, 3)` joins `1,2,3`. |
| `array_from_array_copies` | `Array.from([1,2])` copies; mutating the copy leaves the source. |
| `array_from_typed_copies` | `Array.from(new Int8Array([1,2,3]))` yields a `number[]` `1,2,3`. |
| `array_from_string_error` | `Array.from("abc")` is a check error (iterables deferred). |

### 6.2 Per-class coercion matrix (`typed-arrays/`)

| case | one-line expectation |
|---|---|
| `int8_ctor_array_wraps` | `new Int8Array([300, -1, 128])` joins `44,-1,-128`. |
| `int8_ctor_fractional_truncates` | `[2.9, -2.9]` joins `2,-2`. |
| `int8_ctor_nan_infinity_zero` | `[NaN, Infinity, -Infinity]` joins `0,0,0`. |
| `int8_ctor_negative_zero_reads_zero` | `new Int8Array([-0])[0] === 0`. |
| `uint8_ctor_array_wraps` | `[300, -1, 256]` joins `44,255,0`. |
| `uint8clamped_ctor_clamps` | `[300, -1, 2.5, 3.5]` joins `255,0,2,4` (round half to even). |
| `uint8clamped_ctor_nan_zero` | `[NaN]` joins `0`. |
| `int16_ctor_array_wraps` | `[40000, -40000]` joins `-25536,25536`. |
| `uint16_ctor_array_wraps` | `[-1, 65536]` joins `65535,0`. |
| `int32_ctor_array_wraps` | `[2147483648, -2147483649]` joins `-2147483648,2147483647`. |
| `uint32_ctor_array_wraps` | `[-1, 4294967296]` joins `4294967295,0`. |
| `float32_ctor_rounds` | `[1.1, 1/3]` prints the shortest round-trip decimals of the f32 values (`1.100000023841858`, `0.3333333432674408`). |
| `float32_ctor_specials` | `[Infinity, -Infinity, NaN]` joins `Infinity,-Infinity,NaN`. |
| `float64_ctor_exact` | `[1.1, 2.5]` joins `1.1,2.5`. |
| `float64_ctor_negative_zero` | `1 / new Float64Array([-0])[0] === -Infinity`. |
| `bigint64_ctor_wide_exact` | writing 2^40 and reading back prints `1099511627776`. |
| `bigint64_ctor_beyond_2_53_rounds` | the literal `9007199254740993` prints `9007199254740992` (number-domain caveat). |
| `biguint64_ctor_negative_wraps` | `[-1]` prints `18446744073709552000` (u64 2^64-1 read back through a double; D9 renders the double with ECMAScript Number::toString). |

### 6.3 Constructors, views, properties (`typed-arrays/`)

| case | one-line expectation |
|---|---|
| `ctor_length_zeroes` | `new Int8Array(3)` joins `0,0,0`. |
| `ctor_length_fractional_truncates` | `new Int8Array(2.9)` has length 2. |
| `ctor_length_nan_zero` | `new Int8Array(NaN)` has length 0. |
| `ctor_empty` | `new Int8Array()` has length 0. |
| `ctor_no_new_spelling` | `Float32Array([1])` equals `new Float32Array([1])`. |
| `ctor_string_length_error` | `new Int8Array("5")` is a check error. |
| `ctor_from_float64_truncates` | `new Int8Array(new Float64Array([1.9, -1.9, 300]))` joins `1,-1,44`. |
| `ctor_from_int8_widens` | `new Float64Array(new Int8Array([1, -2]))` joins `1,-2`. |
| `ctor_copy_is_deep` | mutating the source after the copy leaves the copy. |
| `ctor_same_class_copies_buffer`| `new Int8Array(other)` shares no storage (write in one is invisible in the other). |
| `view_from_buffer_defaults` | `new Uint8Array(buf)` has byteOffset 0 and length = byteLength. |
| `view_byte_offset_length` | `new Int32Array(buf16, 8, 1)` has length 1, byteOffset 8, byteLength 4. |
| `view_length_remainder` | `new Int16Array(buf8, 4)` has length 2. |
| `view_shares_buffer` | a write through the view is visible through the source buffer's other views. |
| `view_shares_bytes_across_classes` | u8 view sees the f32 view's bytes in platform order. |
| `view_unaligned_offset_panics` | `new Int32Array(buf, 2)` with a runtime `buf` exits 1. |
| `view_remainder_not_multiple_panics` | `new Int16Array(buf)` with a runtime 3-byte buffer exits 1. |
| `view_length_overflow_panics` | `new Int32Array(buf8, 0, 3)` with a runtime length exits 1. |
| `typed_length_bytelength` | `new Float32Array(4)` has length 4 and byteLength 16. |
| `typed_bytes_per_element` | `Float32Array.BYTES_PER_ELEMENT === 4`, `Int8Array.BYTES_PER_ELEMENT === 1`. |
| `typed_buffer_backreference` | `a.buffer.byteLength === a.byteLength` for an offset-0 view. |
| `typed_byteoffset_after_subarray` | `new Float32Array(4).subarray(2).byteOffset === 8`. |
| `typed_length_write_error` | `a.length = 1` is a check error. |

### 6.4 Indexing (`typed-arrays/`)

| case | one-line expectation |
|---|---|
| `typed_index_write_read` | `u8[1] = 7; u8[1]` prints `7`. |
| `typed_index_oob_read_undefined` | `let v: number \| undefined = a[5]; v === undefined` is true. |
| `typed_index_oob_write_ignored` | `a[9] = 7` leaves all contents unchanged. |
| `typed_index_negative_read_undefined` | `a[-1]` is `undefined` (no wrap-from-end indexing). |
| `typed_index_fractional_read_undefined` | `a[0.5]` is `undefined`. |
| `typed_index_literal_in_range_narrows` | `new Float64Array([2])[0] + 1` prints `3` (literal refinement; move to follow-up if deferred). |
| `typed_index_write_coerces` | `u8[0] = -1; u8[0]` prints `255`. |
| `typed_index_nonnumber_write_error` | `u8[0] = undefined` is a check error. |

### 6.5 Methods (`typed-arrays/`)

| case | one-line expectation |
|---|---|
| `typed_fill_all` | `new Int8Array(3).fill(5)` joins `5,5,5`. |
| `typed_fill_range` | `fill(9, 1, 3)` on `[1,2,3,4]` joins `1,9,9,4`. |
| `typed_fill_negative_start` | `fill(0, -2)` zeroes the last two. |
| `typed_fill_coerces` | `new Uint8Array(1).fill(-1)[0]` prints `255`. |
| `typed_fill_returns_receiver` | the return value aliases the receiver (write visible in the original). |
| `subarray_shares_storage` | writing through `subarray(1)` is visible in the parent. |
| `subarray_negative_begin` | `subarray(-2)` covers the last two elements. |
| `subarray_clamps_end` | `subarray(1, 99)` clamps to length. |
| `slice_copies_new_buffer` | writing through `slice(1)` is invisible in the parent. |
| `slice_negative_begin` | `slice(-2)` copies the last two elements. |
| `set_from_array` | `a.set([1, 2])` writes elements 0-1. |
| `set_with_offset` | `a.set([1], 2)` writes element 2. |
| `set_from_typed_converts` | `new Int8Array(2).set(new Float64Array([1.9, 300]))` joins `1,44`. |
| `set_overflow_panics` | `new Int8Array(2).set(src)` with a runtime source of length 3 exits 1. |
| `copywithin_overlap` | `[1,2,3,4].copyWithin(1, 0, 3)` joins `1,1,2,3`. |
| `reverse_in_place` | `[1,2,3].reverse()` joins `3,2,1` and aliases the receiver. |
| `indexof_found` | `[1,2,3].indexOf(2)` prints `1`. |
| `indexof_nan_absent` | `[NaN].indexOf(NaN)` prints `-1`. |
| `includes_nan_present` | `[NaN].includes(NaN)` prints `true`. |
| `join_default_sep` | `new Int8Array([1,2,3]).join()` prints `1,2,3`. |
| `join_custom_sep` | `join("-")` prints `1-2-3`. |

### 6.6 ArrayBuffer and DataView (`typed-arrays/`)

| case | one-line expectation |
|---|---|
| `arraybuffer_construct_bytelength` | `new ArrayBuffer(8).byteLength === 8`. |
| `arraybuffer_zero_filled` | a u8 view over a fresh buffer is all zeros. |
| `arraybuffer_slice_copies` | `buf.slice(2, 4)` has byteLength 2 and independent bytes. |
| `arraybuffer_slice_negative` | `buf.slice(-2)` copies the last two bytes. |
| `dataview_construct_defaults` | `new DataView(buf8)` has byteOffset 0, byteLength 8. |
| `dataview_subview_offset` | `new DataView(buf8, 2, 4)` has byteOffset 2, byteLength 4. |
| `dataview_get_uint8` | `getUint8(1)` reads the byte written by a u8 view. |
| `dataview_set_int32_default_bigendian` | `setInt32(0, 1)` then a u8 view reads `0,0,0,1`. |
| `dataview_little_endian_flag` | `setInt32(0, 1, true)` then a u8 view reads `1,0,0,0`. |
| `dataview_unaligned_access` | `getFloat64(1)`/`setFloat64(1, x)` work at offset 1. |
| `dataview_float32_roundtrip` | `setFloat32(0, 1.1)` then `getFloat32(0)` prints `1.100000023841858`. |
| `dataview_float64_roundtrip` | `setFloat64(0, 1.1)` then `getFloat64(0)` prints `1.1`. |
| `dataview_bounds_panics` | `getInt32(4)` on a view with byteLength 4 exits 1. |
| `dataview_view_bounds_independent` | offsets are relative to the view, not the buffer. |
| `typed_platform_byte_order` | `new Int32Array([1])` bytes via u8 view print `1,0,0,0` (little-endian targets). |

### 6.7 SharedArrayBuffer, const, interop, printing (`typed-arrays/`)

| case | one-line expectation |
|---|---|
| `sharedarraybuffer_construct` | `new SharedArrayBuffer(8).byteLength === 8`. |
| `sharedarraybuffer_view_works` | `new Uint8Array(new SharedArrayBuffer(4))` is writable and reads back. |
| `typed_const_rebind_error` | `const a = new Int8Array(1); a = new Int8Array(1);` is a check error. |
| `typed_const_contents_mutable` | writing `const a = new Int8Array(1)` through `a[0] = 5` is visible. |
| `typed_not_assignable_to_array_error` | `let a: number[] = new Int8Array(1);` is a check error. |
| `typed_cross_class_assign_error` | `let a: Float64Array = new Float32Array(1);` is a check error. |
| `typed_buffer_argument_mismatch_error` | `new Uint8Array([1])` where a buffer is expected is a check error. |
| `typed_print_format` | `console.log(new Float32Array([1.5, 2.5]))` prints `Float32Array(2) [1.5, 2.5]` (cp3 bracket style, D49 spacing). |
| `typed_print_uint8clamped` | `console.log(new Uint8ClampedArray([255, 0]))` prints `Uint8ClampedArray(2) [255, 0]`. |

## 7. Open questions for the user (recommendations in bold)

1. **BigInt64Array / BigUint64Array**: (a) **include now with the documented
   2^53 numeric caveat**, (b) pause this wave and add a `BigInt` primitive
   first, (c) defer both classes with a clear "not implemented" diagnostic.
   Recommended: (a) - the storage and view surface is needed anyway, and the
   upgrade path (element type becomes BigInt) is additive.
2. **SharedArrayBuffer**: (a) **include now as a distinct class with
   ArrayBuffer-identical semantics, documented as "sharing activates with
   `worker`/`parallel` later"**, (b) defer until real shared memory exists,
   (c) implement shared memory + synchronization now. Recommended: (a).
3. **DataView endianness default**: (a) **JS parity: big-endian when the
   `littleEndian` argument is omitted**, (b) cp3 departure: little-endian
   default (as the brief stated). Recommended: (a) - the class exists for
   JS/network/binary compatibility, and option (b) silently reverses every
   ported read.
4. **`Array(n)` holes**: (a) **dense, element-type zero/default values, no
   hole state** (resolves `dense_array_holes`), (b) `(T | undefined)[]` with
   `undefined` slots (JS-exact state, union cost on every element), (c) keep
   holes only for `Array(n)` results (a third state to carry through the
   vector). Recommended: (a).
5. **Typed-array index read type**: (a) **`number | undefined` (sound,
   JS-exact) with the in-range literal refinement**, (b) `number` (TS default;
   ergonomic, but the mandated `undefined` OOB result becomes unreachable in
   the type system and the read has no value to return). Recommended: (a).

## 8. Amendment map and implementation notes

Spec/docs touched when this is ratified:

| doc | change |
|---|---|
| `docs/language-spec/03-type-system.md` | element mapping table (class -> native scalar); class names are core builtin types; `BYTES_PER_ELEMENT`. |
| `docs/language-spec/09-arrays-strings.md` | `fill` added to the array methods; new subsection: typed arrays, ArrayBuffer/DataView, `Array(n)`/`of`/`from`, indexing rules, divergences (holes, no scalar coercion). |
| `docs/language-spec/10-memory-management.md` | typed arrays/buffers/views named as reference types; fixed-size backing never resizes; contents mutable under `const`. |
| `docs/language-spec/20-standard-library.md` | added to the "built into the language, no import" list. |
| `docs/language-spec/23-open-design-questions.md` | history entries for the resolved items (endianness, holes, SAB, BigInt caveat). |
| `docs/standard-library/04-imported-std-modules.md` | `buffer` section cross-reference (Buffer unchanged; ArrayBuffer/typed arrays are core). |
| `docs/standard-library/05-explicitly-deferred-out-of-scope.md` | deferred: resizable/transferable ArrayBuffer, Atomics/true shared memory, callback and iterator typed-array methods, `Array.from` over iterables. |
| `docs/rough/DECISIONS.md` | one consolidated D-entry (D68): backing, indexing, endianness, SAB, BigInt caveat, `Array(n)` semantics, and the marker promotion below. |
| `docs/rough/M2-PLAN.md` | wave inserted after array codegen (wave 3); landing record on completion. |
| `tests/cases/README.md` | no schema change; new category `typed-arrays/`; ownership grant for the `arrays-strings/` additions. |

Marker rulings this proposal implies (promote per D58):

- `control-flow/_ambiguous/dense_array_holes` -> answered by Q4(a): `Array(n)`
  is dense; no hole state exists. Promote to a real case in `arrays-strings/`
  (`array_ctor_annotated_zero_init` covers it).
- `arrays-strings/_ambiguous/array_print_format` -> array rendering is already
  pinned by D49 (`[1, 2, 3]`); typed-array rendering is pinned here
  (`Float32Array(2) [1.5, 2.5]`).
- `arrays-strings/_ambiguous/slice_negative` and M2-PLAN parked
  `array_index_out_of_bounds` / `array_index_negative` / `array_length_write`:
  this proposal pins typed-array behavior only; the same rulings for
  **regular** arrays stay with the arrays wave (recommended there: the same
  `undefined`/no-op read/write rules, so the two families agree).

Implementation notes for the wave:

- Prerequisite: array-literal + indexing + `.length` codegen (wave 3) or a
  parallel implementation of that path; `new`/call special forms do not need
  the classes wave.
- Sema: extend the builtin registry (`declareBuiltins`, `scopes.cpp` already
  lists `Array` and `BigInt`): 14 class names plus `Array.of`/`Array.from`;
  constructor forms and the index read/write typing live in `member_check` /
  `expr_check`-style special forms, not user classes.
- Codegen/runtime: new header-only `cp3/typed_array.hpp` (ArrayBuffer,
  SharedArrayBuffer, TypedArray, DataView) so the D56 required-TU list stays
  unchanged; method lowering via a table like `kStringMethods`.
- Corpus-first, per project method: spec text + the case list above are the
  oracle; implementation follows, then the `M2-PLAN.md` landing record.
- Perf touchpoints: none for regular arrays (`std::vector` path unchanged);
  typed-array checks are compares on a stored length (constant-foldable for
  `C<N>`).
