---
title: Native scalar types
---
# Native scalar types

CP3 has ordinary language types such as `number`, `string`, and `boolean`, plus a restricted set of native scalar names for code that needs fixed numeric width or familiar C++/Rust spelling. The native tier exists to express requirements at boundaries: binary formats, native APIs, exact large integer values, and low-level numeric storage.

It is deliberately narrower than either C++ or Rust. CP3 maps supported names into its own checked type identities and applies its own arithmetic and conversion rules. A type name that looks native does not automatically give access to pointers, references, platform headers, Rust ownership, or a foreign calling convention.

## Pages in this guide

- [Select the project vocabulary](./01-project-type-system.md): configure `typeSystem` and `strictNumericCasts` in `package.json`.
- [Use explicit prefixes](./02-prefixed-types.md): understand `r_`, `c_`, and `ts_`, plus supported mappings.
- [Apply numeric rules](./03-numeric-rules.md): learn literal ranges, promotion, mixed arithmetic, and strict checking.
- [Understand limitations](./04-limitations.md): pointers, unsupported ecosystem types, unions, and diagnostics.

## A short example

```ts
// In a TypeScript-mode project, this still declares a fixed-width native value.
let sequence: r_'u64' = 9007199254740993;
let ratio: number = 0.5;
```

`sequence` is not routed through the floating-point `number` representation. `ratio` remains ordinary CP3 numeric data. The `r_` prefix says which vocabulary resolves `u64`; it does not generate Rust code or perform a cast.

## Start with the project setting

The `typeSystem` property in the nearest project `package.json` selects how unprefixed native names resolve. `typescript` is the default. Choose `cpp` for supported C++ scalar names or `rust` for supported Rust scalar names. Project mode does not change the C++ code-generation backend.

A source file that must make its native intent independent of project configuration can use a prefix. A project can also remain fully portable and never use native types. Both approaches are valid; choose based on the code's actual representation requirements.

## Understand the representation trade-off

Native integer types preserve integer values in a fixed-width representation and follow integer promotion rules. Ordinary `number` has broad compatibility with CP3 APIs and convenient floating-point math. Crossing between these representations can lose precision or trigger stricter checking, so choose the representation before data is parsed or stored.

Native values do not automatically make a benchmark faster. Their main semantic benefit is precise numeric representation and interop declarations. Performance depends on generated code, optimization, memory access, and the operations actually performed.

## Keep interop claims narrow

The native type resolver is not an ABI verifier. Before connecting an external C++ or Rust library, verify how the compiler declares the function, the target's scalar widths, calling convention, symbol linkage, ownership, and error behavior. Use a narrow module boundary and add compiler tests for the supported declaration.

## Related type guides

- [Types and inference](../01-variables-and-bindings/02-types.md) explains CP3's ordinary types and type checking.
- [Native interop](../../cli-and-compiler/07-native-interop.md) explains the separate foreign-function boundary.
- [Compiler pipeline](../../cli-and-compiler/02-compilation-pipeline.md) explains when type resolution happens.
