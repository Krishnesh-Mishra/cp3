---
title: JSON
chapter: Chapter 3 · Standard library
---
# JSON

`std/json` provides JSON parsing and serialization through its default namespace:

```ts
import json from "std/json";
```

JSON is a data interchange format, not a general object snapshot. Values that are not part of the JSON data model require deliberate conversion before serialization.

## `json.stringify(value)`

Serializes a supported CP3 value to a JSON string:

```ts
const text = json.stringify({ name: "Ada", active: true });
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

`parseFileSync` / `parseFile` read and parse exactly like `parse<T>` on the file's text — the same type-argument rules (inline objects, interfaces, primitives and primitive arrays), the same runtime shape validation, the same duplicate-key and missing/extra-key semantics. A missing or unreadable file, invalid JSON, or a shape mismatch all throw the same error class `JSON.parse<T>` uses (the `fs` read error is boxed into it as well). `parseFile` rejects the promise on the same class; await or try/catch it as usual.

`stringifyToFileSync` / `stringifyToFile` serialize with the same rules as `stringify(value)` and then **overwrite** the destination file (raw write, no append); they throw on a write error.

`verify<T>(text)` runs the exact same checker as `parse<T>` — same inline-`T` and interface rules, same required/optional and extra-key semantics, duplicate keys and malformed text included — but never throws: it returns `boolean`.

## `json.parse<T>(text)`

Parses JSON text and may validate the decoded shape against the requested type:

```ts
interface Settings {
  retries: number;
  endpoint: string;
}

const settings = json.parse<Settings>(text);
```

Parsing malformed JSON fails. When a type argument is supplied, the runtime checks the value shape rather than merely trusting a compile-time assertion. Shape validation is structural: it checks expected members and value types, not application rules such as whether a URL is reachable or a number is within a business limit.

## Validate application constraints

After parsing, validate invariants that are not captured by the type:

```ts
if (settings.retries < 0) {
  throw new Error("retries must not be negative");
}
```

Treat external JSON as untrusted input. A successful parse only establishes syntactic validity and the supported requested shape.

## Supported surface

The module exposes `parse<T>(text)` and `stringify(value)`. It is not a complete compatibility layer for every JavaScript `JSON` option or replacer/reviver callback.

## Signature reference

| Call | Result | Failure behavior |
| --- | --- | --- |
| `json.parse<T>(text)` | `T` when a type argument is supplied; otherwise `unknown` | Malformed JSON or runtime shape mismatch throws a catchable error |
| `json.parseFileSync<T>(path)` | `T` | Throws a catchable error on read failure, malformed JSON, or a shape mismatch |
| `json.parseFile<T>(path)` | `Promise<T>` | Rejects with the same class of error on read/parse/shape failure |
| `json.stringify(value, indent?)` | JSON text as `string` | Unsupported values follow the serializer's runtime rules |
| `json.stringifyToFileSync(path, value)` | `void` | Overwrites the file; throws on a write error |
| `json.stringifyToFile(path, value)` | `Promise<void>` | Overwrites the file; rejects on a write error |
| `json.verify<T>(text)` | `boolean` | Never throws; `false` on malformed JSON, a duplicate key, or a shape mismatch |

The optional `indent` is a number controlling pretty-print indentation. Keep it absent for compact JSON. The API does not expose JavaScript replacer or reviver callbacks.

## Typed parsing is runtime validation

`parse<T>` does more than tell the static checker what type you hope to receive: CP3 validates the parsed value against the requested structural shape and throws if the shape does not match. It does not validate business constraints such as numeric ranges, authorization, allowed URLs, or relationships between fields.

Without a type argument, parsing returns `unknown`. That is the honest result for arbitrary external JSON. Narrow it through a supported checked path or parse against a declared interface and then perform domain validation.

```ts
interface Job {
  id: string;
  priority: number;
}

const job = json.parse<Job>(input);
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

`tests/cases/stdlib/02/json` covers valid parse/stringify, escaping, typed shape validation, and failure cases. The semantic surface lives in `src/std/json/sema.cpp`; the runtime parse/serializer owns concrete behavior. When adding an option, update the member signature, call checker, codegen, runtime, and tests together.
