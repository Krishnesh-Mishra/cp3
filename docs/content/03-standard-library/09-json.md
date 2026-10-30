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
| `json.stringify(value, indent?)` | JSON text as `string` | Unsupported values follow the serializer's runtime rules |

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
