# C+++ (.cp3) - Standard Library

> The `std/*` modules - `console`, `JSON`, `Math`, `fs`, `network`, `Date`, `process`, `path`, `os`, `crypto`, `env`, `Map`/`Set`, `buffer` - with exact function signatures.

Companion spec to the core language README. Tracked separately so the core language spec isn't blocked on every std function being named.

## Documentation in the editor

Every member of the closed families - `console`, `Math`, `path`, `os`,
`process`, `env`, `fs`, `JSON`, `Date`, `crypto`, `Map`/`Set` - carries the
description written in [§04](04-imported-std-modules.md) into the editor:
hovering `console.log` shows its documented signature, what it prints and
what its parameters are for, and the completion popup shows the same text
before the member is inserted.

The documentation lives next to the member's own table in the compiler
(`src/std/<family>/sema.cpp`), so a member the compiler does not have can
never be documented and a documented member can never be missing from the
compiler.
