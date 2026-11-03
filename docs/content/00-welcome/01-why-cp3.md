---
title: Why CP3 exists
chapter: Welcome
---
# Why CP3 exists

CP3 is an experiment in combining a familiar, statically checked programming surface with native deployment. Its syntax borrows conventions that JavaScript and TypeScript developers already know, while its compiler checks source code and produces a native executable through a C++ backend.

The goal is not to reproduce every feature or runtime behavior of another language. CP3 has its own type rules, standard library, module resolver, runtime, and compiler diagnostics. Familiar syntax lowers the learning cost; it does not make CP3 a JavaScript implementation or a TypeScript type checker.

## The problem CP3 is exploring

Developers often choose between a convenient language with a large managed runtime and a native language that exposes more of the machine but asks the programmer to learn a different ownership model. CP3 explores whether a compact, familiar language can retain compile-time checking and produce a standalone native program without asking application authors to write C++ for ordinary work.

That is a design direction, not a guarantee that every program will be as fast as hand-written C++ or Rust. Native code is only the final compilation format. Generated code, runtime calls, data layout, optimization proofs, and the selected backend all affect the result.

## Design choices at a glance

| Area | CP3 choice | Practical meaning |
| --- | --- | --- |
| Source style | JavaScript and TypeScript shaped syntax | Familiar declarations, functions, classes, modules, and common expressions are available. |
| Type checking | Static analysis in the compiler | Invalid assignments and calls can be rejected before native compilation. |
| Executable | C++ is emitted and compiled by a native toolchain | A built program is a native executable; normal users do not edit generated C++. |
| Memory | Runtime-managed references and value representations | CP3 code does not manually call `delete`; ownership still affects lifetime and cost. |
| Standard library | CP3-owned modules | Node APIs and npm packages are not implicitly present. |
| Interoperability | Explicit native boundary features | Foreign ABI, ownership, and platform assumptions remain the application's responsibility. |

## Why the source resembles JS and TS

JavaScript and TypeScript syntax is approachable to many application developers. CP3 therefore uses familiar forms such as `const`, `let`, typed parameters, arrow functions, classes, and imports. The similarity is useful when reading code, but rules can differ in ways that matter to correctness:

- CP3 checks types before generating native code; annotations are not editor-only hints.
- The language does not inherit JavaScript's dynamic coercions or the full TypeScript type system.
- `number` and native integer types have distinct rules; the project can select the vocabulary used for unprefixed native types.
- CP3's runtime modules are a documented set, not a hidden Node compatibility layer.
- Strings are UTF-8 byte strings, and some APIs expose byte-oriented behavior.
- Errors, asynchronous execution, process APIs, and filesystem operations follow CP3's implementation and tests.

The [JavaScript and TypeScript guide](03-javascript-typescript-developers.md) is a migration map. The language chapter is the authority for CP3 rules.

## Why use a C++ backend?

C++ provides a mature native compilation path across the platforms targeted by the project. CP3 can implement a high-level front end while delegating machine-code generation and much of the platform-specific toolchain work to Clang or another configured compiler.

This separation has useful consequences:

1. CP3 source remains the program developers maintain.
2. The CP3 compiler owns parsing, module resolution, type checking, and lowering.
3. The C++ backend can optimize the generated translation units according to the selected profile and target.
4. Generated C++ is available as a debugging artifact when a runtime cost needs investigation.

The backend cannot optimize away every cost. A helper call in a loop, unclear aliasing, allocations, or checks that are needed to preserve semantics can prevent transformations such as inlining or vectorization. The [optimization pipeline](../04-compiler-cli/03-optimization-pipeline.md) states what the current compiler proves and what it does not.

## Why avoid a tracing collector?

CP3's current general reference representation is based on reference counting, not a tracing garbage collector. A reference count lets an acyclic object be destroyed when its last strong owner is released. It does not automatically collect a cycle, and reference updates have runtime cost.

This choice is not a claim that reference counting is universally faster or simpler than tracing collection. It is a runtime design with different tradeoffs. CP3's local escape analysis can remove some allocations in restricted patterns, but it is not a general stack-allocation guarantee. Read [Values and references](../02-runtime/01-value-model.md), [Escape analysis](../02-runtime/02-escape-analysis.md), and [Cycles and weak links](../02-runtime/09-reference-cycles-and-weak-links.md) before relying on a particular lifetime behavior.

## What CP3 is not promising

- It is not a drop-in replacement for Node, a browser, or the TypeScript compiler.
- It does not promise that arbitrary npm packages can be imported.
- It does not promise that every allocation is placed on the stack or that every reference is non-atomic.
- It does not promise SIMD merely because a program is compiled to native code.
- It does not promise identical performance across machines, compilers, optimization profiles, or workloads.
- It does not make foreign native pointers safe solely because they have a type annotation.

These limits are part of an honest evaluation. A feature should be described as implemented only when the compiler/runtime behavior and tests support that description.

## How to evaluate CP3

For a language feature, begin with its page in Chapter 1 and follow the linked test family when a corner case matters. For a standard library call, check the module's documented signatures and supported behavior. For a performance claim, record the benchmark source, compiler versions, build profiles, output validation, repetition count, and run variation.

Useful next pages:

- [Create and run a project](04-first-project.md)
- [Project type-system selection](../01-language/07-native-types/01-project-type-system.md)
- [Runtime value model](../02-runtime/01-value-model.md)
- [Performance measurements](02-performance.md)
- [Compiler pipeline](../04-compiler-cli/02-compilation-pipeline.md)
