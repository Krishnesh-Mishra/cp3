---
title: Buffer
---
# Buffer

`Buffer` is a nominal byte-sequence type, exported as a named export:

```ts
import { Buffer } from "std/buffer";
```

A Buffer is not a CP3 string: strings are UTF-8 text, buffers preserve bytes that may not be valid UTF-8. `length` counts bytes. A Buffer is immutable through the documented surface, comparing equal when its bytes are equal (not by identity). Supported encodings only: `"utf8"` (default), `"hex"`, `"base64"`, `"base64url"` — `"base64Url"` is accepted as an alias; other literal encodings are compile-time errors (a runtime encoding value is validated then, throwing a catchable error).

`std/buffer/types` publishes `Buffer` for annotation-only positions.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `Buffer.from` | `Buffer.from(text: string, encoding?: string): Buffer` | Encodes text; default `"utf8"`; `hex` needs an even digit count (upper/lowercase); `base64`/`base64url` decode on invalid input → catchable error |
| `Buffer.alloc` | `Buffer.alloc(size: number): Buffer` | Zero-filled buffer; size must be a non-negative integer ≤ 2,147,483,647; negative literal rejected at compile time, other invalid sizes throw |
| `buffer.length` | `length: number` | Byte length |
| `buffer.toString` | `toString(encoding?: string): string` | Decodes/renders as `"utf8"` (default), `"hex"` (lowercase), `"base64"` (padded) or `"base64url"` (RFC 4648 §5, unpadded) |
| `buffer.slice` | `slice(start?: number, end?: number): Buffer` | New Buffer copy of the range; indices follow JS-style normalization (fractional truncates toward zero, negative counts from the end, out-of-range clamps); omitted `end` means the length; start-after-end is empty |

## Encoding table

| Encoding | `Buffer.from(text, encoding)` | `buffer.toString(encoding)` |
| --- | --- | --- |
| `utf8` | UTF-8 bytes of the string | Decodes bytes as UTF-8 text |
| `hex` | Even number of valid hex digits, upper/lowercase | Lowercase hex |
| `base64` | Decodes padded base64; throws on bad alphabet/padding | Padded base64 |
| `base64url` | Unpadded RFC 4648 §5 decoding; `=` only at the end; `+`/`/` rejected | Unpadded base64url |

## Notes

- There is no Node `Buffer` compatibility surface: `write`, `copy`, `readUInt32LE`, `subarray`, `concat`, `compare`, `includes`, `indexOf`, `allocUnsafe`, `byteLength`, and mutable index assignment do not exist. Use typed arrays / `DataView` for numeric layout (see [Typed arrays](./typed-arrays.md)).
- Encodings known only at runtime are validated at the call and throw a catchable error; a literal encoding naming a different value is a compile-time diagnostic.
- For cryptographic random bytes, see [Cryptography](./crypto.md) (`randomBytes`).

Behavior details: [guide](../../standard-library/11-buffer.md)
