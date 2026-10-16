# C+++ (`.cp3`) — Documentation

A browsable, section-by-section breakdown of the C+++ language, its CLI and tooling, and its standard library.

> Start the local viewer with `npm start` inside `docs/local-docs-viewer/`, then open <http://localhost:3000>.

## Start Here

New from TypeScript? Get productive in cp3 in about two days — what stays the same, what changes, what's new, plus the std library and CLI commands you actually need.

- [From TypeScript to C+++ in two days](getting-started/00-overview.md)
- [Day 1 — the language](getting-started/01-day-1-the-language.md)
- [Day 2 — the std library & the CLI](getting-started/02-day-2-stdlib-and-cli.md)
- [TypeScript → C+++ cheat sheet](getting-started/03-cheatsheet.md)

## Language Spec

The C+++ core language — syntax, the type system, memory model, concurrency, classes, and everything else you write in a `.cp3` file.

- [C+++ (`.cp3`)](language-spec/00-overview.md)
- [Status](language-spec/00a-status.md)
- [Why C+++?](language-spec/00b-why-cp3.md)
- [1. Philosophy](language-spec/01-philosophy.md)
- [2. Syntax](language-spec/02-syntax.md)
- [3. Type system](language-spec/03-type-system.md)
- [4. Enums](language-spec/04-enums.md)
- [5. Operators & expressions](language-spec/05-operators-expressions.md)
- [6. Control flow](language-spec/06-control-flow.md)
- [7. Error handling](language-spec/07-error-handling.md)
- [8. Variable bindings](language-spec/08-variable-bindings.md)
- [9. Arrays & Strings](language-spec/09-arrays-strings.md)
- [10. Memory management](language-spec/10-memory-management.md)
- [11. Reference cycles & the `weak` keyword](language-spec/11-reference-cycles-the-weak-keyword.md)
- [12. Pointers & references](language-spec/12-pointers-references.md)
- [13. Concurrency: `parallel.*` and workers](language-spec/13-concurrency-parallel-and-workers.md)
- [14. Async](language-spec/14-async.md)
- [15. Classes](language-spec/15-classes.md)
- [16. Generics](language-spec/16-generics.md)
- [17. Functions](language-spec/17-functions.md)
- [18. Modules & imports](language-spec/18-modules-imports.md)
- [19. C/C++ interoperability](language-spec/19-c-cpp-interoperability.md)
- [20. Standard library](language-spec/20-standard-library.md)
- [21. Design goal: 2-day onboarding for JS/TS devs](language-spec/21-design-goal-2-day-onboarding-for-js-ts-devs.md)
- [22. What C+++ combines](language-spec/22-what-cp3-combines.md)
- [23. Open design questions](language-spec/23-open-design-questions.md)

## CLI & Tooling

The `cp3` command-line tool itself — the build/run pipeline, diagnostics and crash reporting, the package manager, the `.cp3pak` format, and native-library bindgen.

- [C+++ (`.cp3`) — CLI, Compiler & Tooling Reference](cli-tooling/00-overview.md)
- [Status](cli-tooling/00a-status.md)
- [1. Compilation pipeline](cli-tooling/01-compilation-pipeline.md)
- [2. Error diagnostics & source mapping](cli-tooling/02-error-diagnostics-source-mapping.md)
- [3. Internal library wrapping](cli-tooling/03-internal-library-wrapping.md)
- [4. Package manager](cli-tooling/04-package-manager.md)
- [5. Package format: authoring & `.cp3pak`](cli-tooling/05-package-format-authoring-cp3pak.md)
- [Post-beta-one roadmap (not beta-one requirements — captured so good thinking isn't lost)](cli-tooling/05a-post-beta-one-roadmap-not-beta-one-requirements-captured.md)
- [6. Open tooling questions](cli-tooling/06-open-tooling-questions.md)

## Standard Library

The `std/*` modules — `console`, `JSON`, `Math`, `fs`, `network`, `Date`, `process`, `path`, `os`, `events`, `crypto`, `env`, `Map`/`Set`, `buffer` — with exact function signatures.

- [C+++ (.cp3) — Standard Library](standard-library/00-overview.md)
- [Status](standard-library/01-status.md)
- [Architecture (see `README.md` §20 for full detail)](standard-library/02-architecture-see-readme-md-20-for-full-detail.md)
- [Built into the language — no import needed](standard-library/03-built-into-the-language-no-import-needed.md)
- [Imported std modules](standard-library/04-imported-std-modules.md)
- [Explicitly deferred / out of scope](standard-library/05-explicitly-deferred-out-of-scope.md)
- [Streaming: fs and process stay separate implementations](standard-library/06-streaming-fs-and-process-stay-separate-implementations.md)
- [Resolved for beta one](standard-library/07-resolved-for-beta-one.md)
- [Resolved at beta-one sign-off (nothing left open)](standard-library/08-resolved-at-beta-one-sign-off-nothing-left-open.md)
