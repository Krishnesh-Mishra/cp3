---
title: Chapter 1 · The language
chapter: Chapter 1 · The language
---
# Chapter 1 · The language

This chapter explains CP3’s source language: how names are declared, how values are typed, what can be changed, and how expressions and declarations fit together.

## Pages in this chapter

- [Bindings and declarations](#01-language/01-variables-and-bindings/01-bindings)
- [Types and inference](#01-language/01-variables-and-bindings/02-types)
- [Native type-system selection](#01-language/07-native-types/01-project-type-system)
- [Rust, C++, and TypeScript-prefixed types](#01-language/07-native-types/02-prefixed-types)
- [Native numeric behavior and conversions](#01-language/07-native-types/03-numeric-rules)
- [Native type limitations and diagnostics](#01-language/07-native-types/04-limitations)
- [Mutability and immutability](#01-language/01-variables-and-bindings/03-mutability)
- [Scope, hoisting, and initialization](#01-language/01-variables-and-bindings/04-scope-and-initialization)
- [Functions and closures](#01-language/02-functions/01-functions-and-closures)
- [Conditions and loops](#01-language/03-control-flow/01-conditions-and-loops)
- [Arrays and object literals](#01-language/04-values-and-objects/01-arrays-and-objects)
- [Classes and interfaces](#01-language/04-values-and-objects/02-classes-and-interfaces)
- [Generics](#01-language/05-generics/01-generics)
- [Modules and exports](#01-language/06-modules-and-errors/01-modules)
- [Errors and exceptions](#01-language/06-modules-and-errors/02-errors)

## Static checking is part of the language

CP3 does not treat its type annotations as comments for an editor. The analyzer uses them, along with inferred expression types, to reject invalid calls and assignments before C++ generation. A successful `cp3 check` means the front end accepted the source; it does not compile or link the program.

The type checker is intentionally not a complete TypeScript implementation. In particular, CP3 rejects several dynamic or open-ended forms such as `any`, arbitrary object index signatures, and `for...in` over plain objects. The compiler emits CP3-specific diagnostics for these forms.

## Source and native representation

The syntax is familiar, but generated C++ representation is an implementation detail. `number` currently maps to a floating-point representation unless an explicitly supported native integer type is used. Reference-shaped CP3 values are handled by the runtime. The generated C++ should not be used as a substitute for the language rules.

See [the compiler pipeline](#04-compiler-cli/02-compilation-pipeline) for how source becomes a native program.
