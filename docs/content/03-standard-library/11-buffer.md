---
title: Byte buffers
chapter: Chapter 3 · Standard library
---
# Byte buffers

`Buffer` is a named export from `std/buffer`:

```ts
import { Buffer } from "std/buffer";
```

It represents a byte sequence. A Buffer is different from a CP3 string: strings are text, while buffers preserve bytes that may not form valid UTF-8.

## Creating a buffer

### `Buffer.from(text, encoding?)`

Encodes a string into bytes. Supported encodings are `"utf8"`, `"hex"`, and `"base64"`:

```ts
const greeting = Buffer.from("Hello", "utf8");
const digest = Buffer.from("4869", "hex");
```

The default encoding is UTF-8. Hex and base64 inputs must use valid encodings; do not assume arbitrary malformed text is silently accepted.

### `Buffer.alloc(size)`

Allocates a buffer of the requested byte length. Initialize bytes before treating them as meaningful application data.

## Reading and slicing

`length` is the number of bytes, not the number of Unicode characters. `slice(start, end?)` returns a range of the buffer:

```ts
const bytes = Buffer.from("hello");
const first = bytes.slice(0, 2);
```

The exact view/copy behavior is determined by the runtime Buffer implementation. Do not rely on mutation of a slice affecting the original unless the runtime contract explicitly guarantees it.

## Converting to text

`toString(encoding?)` decodes bytes using UTF-8 by default, or the selected `"hex"` / `"base64"` representation:

```ts
console.log(bytes.toString("hex"));
```

UTF-8 decoding is appropriate for text bytes; choose hex or base64 when the output must preserve arbitrary byte values in printable form.

## Buffer and typed arrays

Buffers and typed arrays serve related but distinct needs. Buffer is the byte-oriented standard-library type. Numeric typed arrays interpret storage as fixed-width values and are documented in [Typed arrays](16-typed-arrays.md).

## API summary

| Member | Role |
| --- | --- |
| `Buffer.from(text, encoding?)` | Encode text as bytes |
| `Buffer.alloc(size)` | Allocate a byte buffer |
| `buffer.length` | Read byte length |
| `buffer.toString(encoding?)` | Decode or represent bytes as text |
| `buffer.slice(start, end?)` | Select a byte range |

Supported encodings: UTF-8, hexadecimal, and base64. This is the current CP3 Buffer surface, not the full Node Buffer API.

## Byte semantics and equality

A Buffer is an immutable sequence of bytes. Its `length` counts bytes. Assigning the handle to another binding shares the same immutable value; the documented API has no byte-index setter. Equality compares the byte contents rather than object identity. This makes Buffer suitable for encoded payloads and hashes without pretending the data is text.

`Buffer.alloc(size)` creates a zero-filled buffer. The requested size must be a non-negative integer and cannot exceed the runtime's 2,147,483,647-byte cap. A negative numeric literal is rejected during semantic checking; non-integral or invalid run-time values are rejected with a catchable error. Allocation failure is not a guarantee of success merely because the size is below the cap.

## Encoding rules

Only `utf8`, `hex`, and `base64` are supported, with `utf8` as the default. A string literal naming a different encoding is rejected at compile time; an encoding value known only at runtime is validated then and throws a catchable error if unsupported.

| Encoding | `Buffer.from(text, encoding)` | `buffer.toString(encoding)` |
| --- | --- | --- |
| `utf8` | Stores the CP3 string's UTF-8 bytes | Decodes bytes as UTF-8 text |
| `hex` | Requires an even number of valid hex digits; accepts upper/lowercase digits | Returns lowercase hex text |
| `base64` | Decodes base64 text; invalid input throws | Returns base64 text with padding |

For binary values that are not valid text, do not use UTF-8 decoding as a reversible representation. Use hex or base64 to render them for logs or interchange.

## `slice` uses normalized indices and copies

`slice(start, end?)` returns a new Buffer containing the selected range. The runtime follows its documented JS-style index normalization: fractional indices truncate toward zero, negative indices count from the end, and indices beyond the ends clamp to the available range. An omitted end means the buffer length. Because CP3 Buffer has value semantics and no mutation API, the implementation copies selected bytes instead of exposing a mutable alias.

```ts
const source = Buffer.from("hello");
const prefix = source.slice(0, 2);
console.log(prefix.toString()); // "he"
```

A zero-length result is valid. If `start` normalizes after `end`, the selected range is empty.

## Validation and failures

| Operation | Invalid input | Behavior |
| --- | --- | --- |
| `Buffer.from(text, "hex")` | Odd digit count or non-hex character | Catchable runtime error |
| `Buffer.from(text, "base64")` | Invalid alphabet, padding, or dangling bits | Catchable runtime error |
| `Buffer.from(text, encoding)` | Unsupported literal encoding | Compile-time type diagnostic |
| `Buffer.from(text, dynamicEncoding)` | Unsupported run-time encoding | Catchable runtime error |
| `Buffer.alloc(size)` | Negative, fractional, non-finite, or excessive size | Compile-time rejection for negative literals where recognized; otherwise catchable runtime error |
| `buffer.toString(encoding)` | Unsupported encoding | Compile-time check for a literal, runtime check for a dynamic value |

Catch errors at the boundary where malformed external data is handled. Do not use a catch that silently returns empty bytes unless that is the documented application policy.

## Import and type behavior

Import `Buffer` as a named export. It is both the constructor-like namespace used for `from`/`alloc` and the nominal type used in annotations. The compiler makes the ambient Buffer type available with the module binding; programs that do not import it do not gain an implicit global Buffer name.

```ts
import { Buffer } from "std/buffer";

function encodeRecord(text: string): Buffer {
  return Buffer.from(text);
}
```

The method and encoding surface is closed. Node methods such as `write`, `copy`, `readUInt32LE`, `subarray`, `concat`, and mutable index assignment are not implied by the familiar name. Use the typed-array/DataView APIs for numeric layout, and check their separate bounds and endianness rules.

## Conformance cases and implementation

`tests/cases/stdlib/01/buffer` covers allocation, zero fill, lengths, construction, encodings, and slices. Compiler member typing and encoding validation are in `src/std/buffer/sema.cpp`; the runtime implementation is header-only in `src/std/buffer/include/cp3/std/buffer.hpp`. Keep semantic checks, generated calls, and runtime failure behavior aligned when changing the surface.

## Choosing Buffer or a typed array

Use Buffer when the main abstraction is a byte string that is immutable and converted with UTF-8, hex, or base64. Use a typed array when you need indexed fixed-width numeric values over an ArrayBuffer. Use DataView when fields have byte offsets, mixed widths, or explicit endian interpretation. Do not assume a Buffer can be passed to every Node Buffer API.
