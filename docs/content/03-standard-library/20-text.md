---
title: Text encoding
chapter: Chapter 3 · Standard library
---
# Text encoding

`TextEncoder` and `TextDecoder` are named exports from `std/text`:

```ts
import { TextEncoder, TextDecoder } from "std/text";
```

They convert between CP3 strings and `Uint8Array` byte sequences. Only UTF-8 is supported.

## Encoding text

`new TextEncoder()` creates an encoder; `encode(text)` returns the UTF-8 bytes of the text as a fresh `Uint8Array`:

```ts
const enc = new TextEncoder();
const bytes = enc.encode("hello"); // Uint8Array of [104, 101, 108, 108, 111]
```

A CP3 string is already a UTF-8 byte string, so encoding copies its bytes verbatim: no transcoding, no normalization, and no byte-order mark is added.

## Decoding bytes

`new TextDecoder()` creates a decoder; `decode(bytes)` returns the text represented by the bytes:

```ts
const dec = new TextDecoder();
const text = dec.decode(bytes); // "hello"
```

Decoding validates the byte stream as UTF-8. Each invalid maximal subpart — an overlong form, an encoded surrogate, a code point above U+10FFFF, a stray continuation byte, or a truncated sequence at the end of input — is replaced with U+FFFD REPLACEMENT CHARACTER. A leading UTF-8 byte-order mark is stripped. Decoding never throws on malformed bytes; malformed input is replaced, not rejected.

## Encoding label

Both constructors accept an optional encoding label, defaulting to `"utf-8"`:

```ts
const enc = new TextEncoder("utf-8");
const dec = new TextDecoder("utf8");
```

`"utf-8"` and `"utf8"` (in any letter case, ignoring surrounding whitespace) name the same encoding. Any other label is an error: a string-literal label outside the set is rejected at compile time, while a label known only at run time throws a catchable `Error` from the constructor. There is no silent fallback to UTF-8.

The `encoding` property reports the canonical label:

```ts
enc.encoding; // "utf-8"
dec.encoding; // "utf-8"
```

## API summary

| Member | Signature (exact types) | Semantics |
| --- | --- | --- |
| `new TextEncoder(encoding?)` | `TextEncoder(encoding?: string): TextEncoder` | Create an encoder. `encoding` defaults to `"utf-8"`; only `"utf-8"` / `"utf8"` (any case, surrounding ASCII whitespace ignored) are accepted. Rejects all other labels: a non-UTF-8 string literal is a compile-time type error; a non-literal label throws a catchable `Error` from the constructor. No silent fallback. |
| `encoder.encode` | `encode(text: string): Uint8Array` | Returns the UTF-8 bytes of `text` in a fresh `Uint8Array`. CP3 strings are UTF-8, so this copies the bytes verbatim: no transcoding, no normalization, no BOM. |
| `encoder.encoding` | `encoding: string` | The canonical encoding label, always `"utf-8"`. |
| `new TextDecoder(encoding?)` | `TextDecoder(encoding?: string): TextDecoder` | Create a decoder. Same label rules as the encoder. |
| `decoder.decode` | `decode(bytes: Uint8Array): string` | Decodes `bytes` as UTF-8. Each invalid maximal subpart (overlong form, encoded surrogate, code point above U+10FFFF, stray continuation byte, truncated end sequence) is replaced with U+FFFD; a leading BOM is stripped. Never throws on malformed bytes. |
| `decoder.encoding` | `encoding: string` | The canonical encoding label, always `"utf-8"`. |

Width note: `string` is a UTF-8 byte string (`length` counts bytes); `Uint8Array` is the byte-array type from [Typed arrays](16-typed-arrays.md). Instances are stateless apart from the validated label and may be reused. The surface is closed: unknown members are compile-time errors.

## Validation and failures

| Operation | Invalid input | Behavior |
| --- | --- | --- |
| `new TextEncoder("latin1")` (literal) | Unsupported encoding label | Compile-time type diagnostic |
| `new TextDecoder(label)` (non-literal) | Unsupported encoding label | Catchable runtime `Error` |
| `decoder.decode(bytes)` | Malformed UTF-8 bytes | U+FFFD replacement; never throws |

Encoders and decoders are stateless apart from the validated label, so one instance may be reused for every conversion.

## Limits

- UTF-8 only: only `"utf-8"` / `"utf8"` are accepted. UTF-16, Latin-1, and every other continuous encoding are outside this module; use `Buffer` from `std/buffer` when hex or base64 representations are needed.
- No streaming decode: `{ stream: true }`, `decodeInto`, and incremental decoding are not part of the closed surface.
- Decode never throws on malformed bytes: malformed input is replaced with U+FFFD, not rejected. Validate at the boundary if replacement is unacceptable.
- No normalization or grapheme handling: encode copies the string bytes verbatim and decode validates code-point sequences. See [Strings](15-strings.md) for byte vs code-point indexing.

## Import and type behavior

Import both classes as named exports. Each name is also the nominal type used in annotations (`let enc: TextEncoder`), available only while the module binding is in scope; programs that do not import `std/text` do not gain implicit global names.

```ts
import { TextEncoder, TextDecoder } from "std/text";

function roundTrip(text: string): string {
  return new TextDecoder().decode(new TextEncoder().encode(text));
}
```

Unknown members are diagnosed at compile time with the documented member as the candidate list.

## Conformance cases and implementation

`tests/cases/stdlib/02/text` covers byte-exact encoding, round trips, the `encoding` property, U+FFFD replacement, and the unsupported-encoding error. Semantic checks are in `src/std/text/sema.cpp`; lowering is in `src/std/text/codegen.cpp`; the runtime is header-only in `src/std/text/include/cp3/std/text.hpp`. Keep semantic checks, generated calls, and runtime failure behavior aligned when changing the surface.

## Choosing text encoding or Buffer

Use `TextEncoder` / `TextDecoder` when the boundary is between text and raw UTF-8 bytes, such as network payloads or file contents. Use `Buffer` when the data must also be rendered as hex or base64, or when byte-oriented slicing with those encodings is needed. Both treat a CP3 string as UTF-8 bytes; prefer the type that names the conversion the application actually performs.
