---
title: Modules and exports
chapter: Chapter 1 · The language
---
# Modules and exports

Modules organize CP3 code and define explicit import boundaries. A module specifier is resolved by the CP3 module loader; it is not a request to execute an arbitrary Node package.

## Import a standard module

```ts
import console from "std/console";
import { Buffer } from "std/buffer";
```

The importer must use the form supported by that module’s registry entry. Some modules expose a default namespace, others expose named values, and some support both. The [standard-library overview](#03-standard-library/00-overview) lists the current import form for every registered module.

## Import project modules

Project source can import other CP3 modules using the local module resolver’s supported path forms. Keep imports explicit and use the project entry and directory layout created by `cp3 init`. A missing or unresolved module is reported before C++ generation.

## Exports

Modules can export supported declarations and values. Package projects have a package-specific export contract: the package entry exports its public surface in the form accepted by the packer. Use `cp3 init --type package` to create a starting project that satisfies that contract.

## Resolution and diagnostics

The module loader considers project files, standard module names, and package metadata according to its resolution rules. A misspelled `std/...` specifier is not silently treated as a global. Removed module names may receive a more specific migration message.

## Module initialization

Imported modules are resolved and linked as part of the build pipeline. The compiler tracks dependencies and generated translation units for incremental builds. See [Compilation pipeline](#04-compiler-cli/02-compilation-pipeline).

## Default and named imports

A local module may export one default value and named declarations. An importer can request either or both:

```ts
import double, { add } from "./math.cp3";
```

The default import name is chosen by the importer. A named import must match an exported name. Import aliases rename a local binding without renaming the export:

```ts
import { add as sum } from "./math.cp3";
```

Standard modules have their own registered export forms. For example, `std/console` uses a default namespace, while `std/buffer` and `std/collections` use named exports. Use the registry-backed forms from the [standard-library overview](#03-standard-library/00-overview).

## Export forms

Export declarations supported by the compiler include default exports, named exports, and re-export patterns covered by module tests. A package project has an additional package-entry contract; a normal application module should not be written as though it were being packed. `cp3 init --type package` scaffolds the package-specific entry shape.

For a small module, make the public boundary explicit:

```ts
export function add(left: number, right: number): number {
  return left + right;
}
```

Use the export form actually supported for the declaration. Do not assume decorators, dynamic `require`, CommonJS assignment, or every TypeScript `export =` form is accepted.

## Local specifiers and project layout

Relative module paths are resolved by CP3's project module loader. Prefer explicit relative forms used by existing tests, such as `./math.cp3`, and keep the extension and path spelling consistent. The tests include nested project layouts and import cycles. Bare local specifiers have an ambiguity marker in the corpus; do not build new code on that unresolved form without checking the current resolver behavior.

`std/...` specifiers are looked up in the closed standard-module registry rather than as project files. Package imports use CP3 package metadata and installed package artifacts; they are not npm JavaScript imports.

## Resolution failures

A missing local module, unknown standard module, or missing named export is a compile-time issue before native linking. Read the message at the import site and verify the importer path, exported spelling, package root, and module kind. A native linker error is later and usually concerns a symbol or library, not a failed CP3 import.

## Module cycles

The test corpus includes circular imports. When a change affects module initialization order or dependency collection, inspect those tests and the module graph implementation. Do not assume Node's exact live-binding or initialization timing rules unless CP3 tests and runtime behavior establish them.

## Compiler contributor trail

Module behavior crosses parsing of import/export declarations, module resolution, semantic bindings, generated translation units, and CLI dependency/build tracking. Search `src/modules` and `src/cli` for resolution and registry ownership, then inspect the relevant `tests/cases/modules` fixtures. Test a missing import and a missing named member as well as the successful path.
