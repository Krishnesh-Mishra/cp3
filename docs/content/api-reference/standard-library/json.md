---
title: JSON
---
# JSON

`std/json` provides JSON parsing and serialization. Bind the import as `JSON` — the compiler recognizes the `JSON` namespace identifier:

```ts
import JSON from "std/json";
```

The parse and stringify backends are vendored native libraries (simdjson for parsing, glaze for serialization in the v0.2.6 change set - see [Runtime libraries](../../internal-engineering/05-runtime-libraries.md)); the public API and error behavior are unchanged. Duplicate keys in an object throw. Escapes are `\" \\ \/ \b \f \n \r \t \uXXXX` — anything else is an "unknown escape" error.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `JSON.parse<T>` | `parse<T>(text: string): T` | Type argument required — bare `JSON.parse(text)` is a compile error (`CP3208`); malformed JSON or a shape mismatch throws a catchable error |
| `JSON.parseFileSync<T>` | `parseFileSync<T>(path: string): T` | `CP3208` without `T`; throws on read/parse/shape failure |
| `JSON.parseFile<T>` | `parseFile<T>(path: string): Promise<T>` | `CP3208` without `T`; rejects with the same error class |
| `JSON.stringify` | `stringify(value, replacer?, space?): string` | `replacer` is an ECMAScript `(key, value) => replacement` callback or a `string[]` allowlist; `space` (number or string) sets indentation; `stringify(value, indentNumber)` is accepted for back-compat |
| `JSON.stringifyToFileSync` | `stringifyToFileSync(path: string, value): void` | Serializes like `stringify`, then **overwrites** the destination; throws on a write error |
| `JSON.stringifyToFile` | `stringifyToFile(path: string, value): Promise<void>` | Overwrites; rejects on a write error |
| `JSON.verify<T>` | `verify<T>(text: string): boolean` | `CP3208` without `T`; never throws — `false` on malformed JSON, a duplicate key, or a shape mismatch |

## Rules and limits

- Parsing is runtime validation: CP3 checks the decoded value against the requested structural shape, not application invariants. Validate those after the parse.
- Inline objects, interfaces, primitives and primitive arrays are supported shapes; required/optional and extra keys are checked, keys match case-sensitively, and every duplicate key throws (`verify` returns `false`).
- A reviver callback is **not** exposed (stringify's `replacer` is); the module is not a complete compatibility layer for every JavaScript `JSON` option.
- Unsupported values in `stringify` follow the serializer's runtime rules; functions, class instances, maps, sets and buffers are not guaranteed useful representations.
- `parseFile`/`stringifyToFile` ride the [File system](./fs.md) API; `stringifyToFile*` overwrite — they do not append.

Behavior details: [guide](../../standard-library/09-json.md)
