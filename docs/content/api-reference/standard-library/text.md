---
title: Text encoding
---
# Text encoding

`TextEncoder` and `TextDecoder` are named exports from `std/text`:

```ts
import { TextEncoder, TextDecoder } from "std/text";
```

They convert between CP3 strings and `Uint8Array` bytes — UTF-8 only. `std/text/types` publishes both class names for annotation positions. Instances are stateless apart from the validated label and may be reused.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `new TextEncoder(encoding?)` | `TextEncoder(encoding?: string): TextEncoder` | `encoding` defaults to `"utf-8"`; `"utf-8"` and `"utf8"` (any case, surrounding ASCII whitespace ignored) only; a non-UTF-8 string-literal label is a compile-time error, a non-literal one throws from the constructor |
| `encoder.encode` | `encode(text: string): Uint8Array` | UTF-8 bytes as a fresh `Uint8Array`; verbatim copy — no transcoding, no normalization, no BOM |
| `encoder.encoding` | `encoding: string` | Always `"utf-8"` |
| `new TextDecoder(encoding?)` | `TextDecoder(encoding?: string): TextDecoder` | Same label rules |
| `decoder.decode` | `decode(bytes: Uint8Array): string` | UTF-8 validation; each invalid maximal subpart is replaced with U+FFFD; a leading BOM is stripped; malformed input is replaced, never thrown |
| `decoder.encoding` | `encoding: string` | Always `"utf-8"` |

## Limits

- UTF-8 only — no UTF-16/Latin-1. No streaming decode (`{ stream: true }`, `decodeInto`), no normalization or grapheme handling.
- `TextEncoder`/`TextDecoder` name the text↔bytes boundary; for hex/base64 rendering use `Buffer` ([Buffer](./buffer.md)); for numeric byte layout use typed arrays/DataView ([Typed arrays](./typed-arrays.md)).
- UTF-8 validation is backed by a vendored simdutf build with a built-in fallback on supported targets — identical behavior either way.

Behavior details: [guide](../../standard-library/20-text.md)
