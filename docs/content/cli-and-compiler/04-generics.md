---
title: Generics and the build
---
# Generics and the build

Type arguments on generic functions, classes, interfaces, and type aliases are **checked at compile time** and instantiated into concrete native forms in the generated code — they are not a dynamically typed escape hatch, and they are not implicitly free either.

```ts
function identity<T>(value: T): T { return value; }

const name = identity<string>("Ada"); // explicit
const count = identity(3);            // inferred
```

Practical expectations:

- **Inference when supported, explicit arguments when not** — add `<...>` when inference is ambiguous or the API reads clearly with them.
- **Generic classes with empty constructors need arguments**: `new Map<string, number>()` — the checker cannot guess element types from nothing.
- **Not all of TypeScript's type-level machinery exists.** Arbitrary conditional, mapped, indexed-access, and higher-kinded types are not implemented; unsupported forms get diagnostics, not silent pass-through.
- **"Compiles" is not "zero cost."** Generic instantiation improves type knowledge; calls, allocation, and reference operations in the body still cost. If a hot generic path matters, look at the generated C++ rather than trusting the angles.

The language guide covers the rules in full: [Generics](../language/05-generics/01-generics.md). For how instantiation is spelled in the emitted code, see [Compiler design](../internal-engineering/01-compiler-design.md).

## Checking a generic build

Use `cp3 check` for type-level feedback and `cp3 build` for the native result. When a generic instantiation behaves unexpectedly, compile with `--show-runtime` and read the code that was emitted for it — that, not the source angles alone, determines what runs.

## Related pages

- [Generics, in the language guide](../language/05-generics/01-generics.md)
- [Collection generics (Map/Set)](../standard-library/12-collections.md)
- [Compiler design](../internal-engineering/01-compiler-design.md)
- [CLI commands](./01-cli-commands.md)
