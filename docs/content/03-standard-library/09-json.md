---
title: JSON
chapter: Chapter 3 · Standard library
---
# JSON

`std/json` provides JSON parsing and serialization through its default namespace. Bind the import as `JSON` — the compiler recognizes the `JSON` namespace identifier:

```ts
import JSON from "std/json";
```

JSON is a data interchange format, not a general object snapshot. Values that are not part of the JSON data model require deliberate conversion before serialization.

## `JSON.stringify(value)`

Serializes a supported CP3 value to a JSON string:

```ts
const text = JSON.stringify({ name: "Ada", active: true });
```

The result can be written as text or sent over a network. Handle values that cannot be represented in JSON explicitly; do not assume that arbitrary class instances serialize every field automatically.

## File-oriented helpers

```ts
const settings = JSON.parseFileSync<Settings>("./settings.json");
const settingsAsync = await JSON.parseFile<Settings>("./settings.json");
JSON.stringifyToFileSync("./out.json", result);
await JSON.stringifyToFile("./out.json", result);
const ok = JSON.verify<Settings>(text);
```

`parseFileSync` / `parseFile` read and parse exactly like `JSON.parse<T>` on the file's text — the same type-argument rules (inline objects, interfaces, primitives and primitive arrays), the same runtime shape validation, the same duplicate-key and missing/extra-key semantics. A missing or unreadable file, invalid JSON, or a shape mismatch all throw the same error class `JSON.parse<T>` uses (the `fs` read error is boxed into it as well). `parseFile` rejects the promise on the same class; await or try/catch it as usual.

`stringifyToFileSync` / `stringifyToFile` serialize with the same rules as `stringify(value)` and then **overwrite** the destination file (raw write, no append); they take no type argument and throw on a write error.

`verify<T>(text)` runs the exact same checker as `JSON.parse<T>` — same inline-`T` and interface rules, same required/optional and extra-key semantics, duplicate keys and malformed text included — but never throws: it returns `boolean`. A missing or mistyped required field is a `false`, not an exception; keys match case-sensitively and every duplicate key in the text makes the check fail.

## `JSON.parse<T>(text)`

Parses JSON text and validates the decoded shape against the requested type:

```ts
interface Settings {
  retries: number;
  endpoint: string;
}

const settings = JSON.parse<Settings>(text);
```

Parsing malformed JSON fails. The type argument is enforced at runtime, not merely trusted as a compile-time assertion. Shape validation is structural: it checks expected members and value types, not application rules such as whether a URL is reachable or a number is within a business limit.

## Validate application constraints

After parsing, validate invariants that are not captured by the type:

```ts
if (settings.retries < 0) {
  throw new Error("retries must not be negative");
}
```

Treat external JSON as untrusted input. A successful parse only establishes syntactic validity and the supported requested shape.

## Supported surface

The module exposes `parse<T>(text)`, the file-oriented `parseFileSync<T>` / `parseFile<T>` / `stringifyToFileSync` / `stringifyToFile` / `verify<T>` helpers, and `stringify(value, replacer?, space?)`. It is not a complete compatibility layer for every JavaScript `JSON` option; a reviver callback is not exposed.

## Signature reference

| Call | Result | Failure behavior |
| --- | --- | --- |
| `JSON.parse<T>(text)` | `T` | Type argument required (bare call is `CP3208`); malformed JSON or runtime shape mismatch throws a catchable error |
| `JSON.parseFileSync<T>(path)` | `T` | Type argument required (`CP3208`); throws a catchable error on read failure, malformed JSON, or a shape mismatch (including a missing required field) |
| `JSON.parseFile<T>(path)` | `Promise<T>` | Type argument required (`CP3208`); rejects with the same class of error on read/parse/shape failure |
| `JSON.stringify(value, replacer?, space?)` | JSON text as `string` | Unsupported values follow the serializer's runtime rules |
| `JSON.stringifyToFileSync(path, value)` | `void` | Overwrites the file; throws on a write error |
| `JSON.stringifyToFile(path, value)` | `Promise<void>` | Overwrites the file; rejects on a write error |
| `JSON.verify<T>(text)` | `boolean` | Type argument required (`CP3208`); never throws — `false` on malformed JSON, a duplicate key, or a shape mismatch (including a missing required field) |

`replacer` is an ECMAScript replacer callback `(key, value) => replacement` (returning `undefined` omits a property) or a `string[]` allowlist of property names; `space` (number or string) controls pretty-print indentation. For back-compat, `stringify(value, indentNumber)` is still accepted as the second argument. A reviver callback is not exposed.

## Typed parsing is runtime validation

`parse<T>` does more than tell the static checker what type you hope to receive: CP3 validates the parsed value against the requested structural shape and throws if the shape does not match. It does not validate business constraints such as numeric ranges, authorization, allowed URLs, or relationships between fields.

A type argument is required: bare `JSON.parse(text)` is a compile error (`CP3208`). Parse against a declared interface or an inline object annotation and then perform domain validation.

```ts
interface Job {
  id: string;
  priority: number;
}

const job = JSON.parse<Job>(input);
if (job.priority < 0) {
  throw new Error("priority cannot be negative");
}
```

Use a type shape that matches the actual payload. Avoid casting an unknown document to an expansive type just to suppress checks; a shape mismatch is an input error and should be handled intentionally.

## Serialization is not an object dump

JSON has a restricted data model: null, booleans, numbers, strings, arrays, and objects with string keys. CP3 runtime values such as functions, class instances, maps, sets, buffers, and closures are not automatically guaranteed to serialize as a useful domain representation.

Convert non-JSON values into explicit JSON-compatible records first. This makes the stored format stable and avoids exposing internal runtime details as an accidental wire protocol.

## Failure handling

Malformed text and a typed-shape mismatch are runtime failures, not compile-time diagnostics. Catch them where external input enters the program, attach context that helps identify the source, and do not silently replace invalid input with a plausible default. After parsing, validate constraints that are not part of structural typing.

## Test and implementation map

`tests/cases/stdlib/02/json` covers valid parse/stringify, escaping, typed shape validation, and failure cases; `tests/cases/stdlib/02/json/file` covers the file-oriented family (`parseFile`/`parseFileSync`/`stringifyToFile`/`stringifyToFileSync`/`verify`), including the missing-file, missing-required-field, duplicate-key, and overwrite cases. The semantic surface lives in `src/std/json/sema.cpp`; the runtime parse/serializer owns concrete behavior. When adding an option, update the member signature, call checker, codegen, runtime, and tests together.
