# `cp3 install <dir>` — compile or reject; class exports; `cp3 test`

Status: implemented in the CLI only (`src/cli/src/**`), per ROADMAP L:

- `cp3 install <dir>` compiles the source or rejects with a clear
  "use a `.cp3pak`" error; classes are accepted via a package's default
  or named exports (the generated `index.cp3` exposes them as named class
  exports so consumers type-check without the CP3401 default-object
  class-value rejection); `cp3 test` discovers and runs test files for
  application and package projects alike.

File map (all paths are inside `src/cli/src/**` except this doc):

- `pkg/manifest/signatures.cpp`, `detail/pkg/signatures.hpp` —
  `hasPackageExportSurface`, class force-exports in
  `stripToSignatures`.
- `commands/pkg/pack.cpp` — package builds accept named class exports.
- `pkg/remote/link.cpp` — `cp3 link <dir|.cp3pak>` accepts named exports.
- `pkg/remote/install.cpp`, `commands/build/install.cpp` —
  directory installs compile into the `.cp3pak`-shaped layout.
- `commands/run/run.cpp`, `detail/core/commands.hpp`, `cli.cpp` —
  `cp3 test` command plus `testMode`.

## 1. Directory install: compile, then install like a `.cp3pak`

A directory install (`cp3 install <dir>`, `file:<dir>` dependencies) mints
the same members a `.cp3pak` carries, then writes them under
`.cp3/packages/<name>/`:

```text
package.json          verbatim from the author directory
index.cp3             stripToSignatures + ensureNativeImports (see §2)
pak/<module>.{cpp,hpp} generated C++ for the entry module + its graph
<build.files>         verbatim extra files
```

The entry module (`package.json` → `main`) is loaded through the same
frontend the `build`/`run` commands use: `loadModuleGraph`,
`analyzeProgramGraph` (or the single-unit path when the graph has no
whole-program modules — `hasProgramModules` decides), then
`generateProgramModule` / `generateModule`, and the `index.cp3` is the
same `stripToSignatures` + `ensureNativeImports` pack ships. Native
bindings (`import native "<id>"`) relocate to the consuming project's
`.cp3/native/<id>/` exactly as for `.cp3pak` and legacy directory
installs (`installNativeBindingsFromDirectory`), and `build.files`
entries are copied verbatim (mirroring `pack.cpp`). The lock entry is
unchanged (`dir-sha256:` over the installed tree, `file:<relative>`),
and `engines.cp3` warnings still apply.

When the directory cannot be compiled, install fails with a
"use a `.cp3pak`" error and exits 1 (it never copies broken source):

- the declared `main` file does not exist → "has no compilable package
  entry … use a `.cp3pak` instead (build it with 'cp3c build')";
- the entry fails the compiler frontend/codegen (its diagnostics print
  first) → same error;
- the entry has no export surface (no `export default`, no
  `export <declaration>`, no `export { ... }`) → same error (see §2).

What is *not* changed:

- A directory with **no** entry file at all (metadata-only fixtures such
  as a bare `package.json` with `engines`) keeps the legacy
  `copyTree` behaviour (skipping `.cp3/`), so existing installs and
  tests keep working; "no entry file" is the only legacy carve-out.
- `.cp3pak` installs, `cp3 publish`/`link`/`unlink`, and bare
  `cp3 install` (dependency specs) are untouched.
- No C++ toolchain runs during install: the transpile produces the
  `pak/` C++ source that downstream `cp3 build` compiles and links
  (`collectPackageImplementations`); "compiles" here means the
  transpile, identical to what `cp3 build` does for a package.

## 2. Class exports (the CP3401 fix)

Previously a package build required `export default { ... }`, so
`export class Counter` alone never packed, and a class reached only
through the default object hit the downstream CP3401 class-value
rejection at the consumer site (`new pkg.Counter()`).

The CLI now accepts class packages in every common shape:

```ts
export class Counter { ... }            // named export
class Counter { ... } export { Counter };
export default Counter;
export default { Counter };
export default class Counter { ... }    // default + declaration
```

- `hasPackageExportSurface` accepts a default export, any
  `export <declaration>`, or a non-empty `export { ... }` list
  (including re-exports). It gates `packProject`,
  `install <dir>`, and directory/`index.cp3` validation; the exact
  export shape is still checked by the compiler frontend.
- `stripToSignatures` additionally force-exports every top-level class
  a package surfaces through its default export or an `export { ... }`
  list, by emitting its declaration as `export class <Name> …`
  (idempotent `ensureExported`, so an already-exported class is not
  doubled). Redundant `export { <Name> }` specifiers for those classes
  are dropped from their export list to avoid a duplicate-export
  error; other specifiers, re-exports (`export { … } from "m"`,
  `export * …`), and default exports stay verbatim.
- Consumers then import the class through the named surface
  `import { Counter } from "<pkg>"` and `new` it, which carries the
  real class type end to end. The default-object member path
  (`new pkg.Counter()`) keeps the verbatim default export; the named
  surface is the supported path for classes today.

## 3. `cp3 test`

`cp3 test [file.cp3 ...]` builds and executes each test file and prints
a pass/fail summary (`N passed, M failed (T total)` on stderr). Exit
codes: 0 when every test passes, 1 when any fails, 2 when no files are
found or the command is misused.

- Explicit files come from the command positionals.
- With no arguments the files are discovered under the project root
  (nearest `package.json` ancestor): `tests/` then `test/`
  (recursive `*.cp3`), deduplicated and sorted, then `*.test.cp3` at
  the root.
- A `"type": "package"` project is allowed: each file runs with
  `testMode` set, so `cmdBuildOrRun` treats the entry as a runnable
  application (importing the package entry inline, e.g.
  `import { Counter } from "../src/index.cp3"`) instead of rejecting
  it as a package project.
- The incremental (fast) build profile is the default (as for `run`);
  `--release`/`--debug`/`--incremental` still apply.

## 4. Limits / follow-ups

- Frontend/codegen errors during `install <dir>` surface the diagnostics
  first and then the "use a `.cp3pak`" error; fixing the source (or
  publishing a `.cp3pak`) is the only recovery — there is no legacy
  raw-copy fallback once an entry file exists.
- Only the entry module plus its resolved graph is transpiled; worker
  units (`new Worker("…")`) in a package entry follow the normal build
  path and are not generated here.
- The CP3401 root cause (a class value in a default-export object
  literal) lives below the CLI; this change gives consumers a
  type-checking named-export path and keeps the default export verbatim
  for backward compatibility.
