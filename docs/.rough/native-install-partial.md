# `cp3c native-install` - partial (beta-one command half)

**Status: the command half of the feature; the compiler half landed after it.**
**Superseded (as a whole) by [`native-install-complete.md`](native-install-complete.md)**,
which records the finished feature: the `native.json` reader, the
`import native "id" as alias` surface, the sema type check, the codegen
lowering, the build integration, the spec-19 pointer decision and the final
completion percentage. This document stays as the record of the command half.

**Scope of this pass: the `cp3c native-install` command itself.**
**Not in this pass: the compiler half** - `cp3 check` / `cp3 build` do not read
`native.json`, and `import native "id" as alias` is not a thing yet.

This document states exactly what works, exactly what does not, which tests
were run, and how the completion percentage was calculated. Nothing here is
aspirational: every "supported" row below is exercised by a test in
`tests/cli/cases/native-install.json` or by the manual commands in
"Verification".

---

## 1. What landed

| Piece | Where |
| --- | --- |
| Command + option parser | `src/cli/src/commands/native_install.cpp` |
| Pipeline (resolve -> bind -> emit -> write) | `src/cli/src/commands/native_install_support.cpp` |
| Option struct / shared declarations | `src/cli/src/detail/native_install.hpp` |
| libclang discovery + dynamic loading | `src/cli/src/native/libclang.{hpp,cpp}` |
| SDK / header resolution | `src/cli/src/native/sdk.{hpp,cpp}`, `src/cli/src/native/sdk_libs.cpp` |
| Header binder (libclang AST -> declarations) | `src/cli/src/native/bindgen*.{hpp,cpp}` |
| `native.json` renderer | `src/cli/src/native/metadata.{hpp,cpp}` |
| `index.cp3` renderer | `src/cli/src/native/bindgen_index.cpp` |
| Declaration model (shared with the future compiler half) | `src/common/include/cp3/native_bindings.hpp`, `src/common/src/native_bindings.cpp` |
| Vendored libclang C API headers | `vendor/third_party/clang-c/` (Apache-2.0 WITH LLVM-exception) |
| CLI wiring (`cp3c help`, dispatch) | `src/cli/src/cli.cpp` |
| `clean` keeps `.cp3/native`; `clean --all` removes it | `src/cli/src/commands/clean.cpp` |
| Tests | `tests/cli/cases/native-install.json`, `tests/cli/cases/clean.json`, `tests/cli/fixtures/native-sdk/` |

The command writes two files per binding:

```
.cp3/native/<id>/index.cp3    readable declaration view, ordinary cp3 syntax
.cp3/native/<id>/native.json  the metadata (schema "cp3-native-binding/1")
```

`native.json` follows the beta-one schema documented in the archived design
(`docs/cli-tooling/04-package-manager.md`, "native.json schema"): `schema`,
`id`, `source`, `includeDirs`, `defines`, `headers`, `libDirs`, `linkNames`,
`staticLibraries`, `sharedLibraries`, `runtimeDlls`, `abi`, `target`,
`libclangVersion`, `contentHash` (SHA-256 of the exact `index.cp3` bytes),
`functions`, `classes`, `enums`, `rejected`.

---

## 2. Supported surface (what the command does today)

### 2.1 Command line

```
cp3c native-install <sdk-path-or-name> [options]
  --include <dir>        extra include directory (repeatable)
  --define <name[=val]>  preprocessor definition (repeatable)
  --lib-dir <dir>        library search directory (repeatable)
  --link <name>          link library (repeatable)
  --name <id>            binding id (default: package.json name)
  --out <dir>            binding root (default: .cp3/native)
  --force                overwrite an existing binding
  --dry-run              print the plan without writing files
  --json                 stable machine-readable output
  --verbose              show discovery details
  --help, -h             show this help
```

Exit codes (also printed by `--help`):

| code | meaning |
| --- | --- |
| 0 | success |
| 1 | bind failure (libclang could not parse the headers) or write failure |
| 2 | usage error: bad option, unresolved SDK, missing libclang, existing binding without `--force` |

### 2.2 SDK / header input resolution

| Input form | Behaviour |
| --- | --- |
| SDK directory | uses `<dir>/include` when it exists, else `<dir>`; every header under it is bound |
| Directory of headers | same, without the `include/` step |
| Single header file (`foo.h`, `.hpp`, `.hh`, `.hxx`, `.h++`) | that one file is bound; its directory becomes the include root |
| Package name | `CP3_NATIVE_SDK_PATH` (path list) -> `pkg-config` -> `vcpkg` (`VCPKG_ROOT` / `VCPKG_INSTALLATION_ROOT`); an unresolved name exits 2 and prints every searched path plus the install hint |
| `--include <dir>` | extra include root; also a public root (declarations under it are bound) |

Headers are recorded **relative to their include root** when possible
(`demo/native_demo.h`), so a moved SDK still resolves; absolute paths are used
for headers outside every root.

### 2.3 libclang

* Loaded dynamically at runtime - cp3c links no LLVM library and ships no
  LLVM tree.
* Search order: `CP3_LIBCLANG` (authoritative when set) -> `ProgramFiles`
  and `ProgramFiles (x86)` `LLVM/bin/libclang.dll` -> `C:\Program Files\LLVM\bin\libclang.dll`
  -> every `PATH` entry -> (macOS) Homebrew/CLT paths -> (Linux) the usual
  `libclang.so` locations.
* Every entry point the binder uses is resolved by name; a library that is
  missing a symbol is rejected with that symbol named, not silently skipped.
* Missing libclang exits 2 and prints the platform install command
  (`winget install --id LLVM.LLVM -e` / `apt install libclang-dev` /
  `dnf install clang-devel` / `brew install llvm`) and every searched path.
* The **declarations** are vendored (`vendor/third_party/clang-c/`, pinned to
  `llvmorg-23.1.0`) so that *building* cp3c never requires LLVM. Only the
  runtime library is needed to run `native-install`.

### 2.4 Bound declarations

| C++ shape | cp3 shape | Notes |
| --- | --- | --- |
| free function | `export function f(a: number): number;` | |
| `bool` | `boolean` | |
| arithmetic types (`int`, `double`, `float`, `long`, ...) | `number` | |
| `std::string`, `const std::string&` | `string` | |
| `const char*` parameter / return | `string` | a `const char*` return is recorded with `returnsCStr: true` |
| `void` | `void` | |
| scoped / unscoped `enum` | `export enum Color { ... }` | enumerators keep their C++ spelling |
| plain `struct` / `class` | `export class Point { ... }` | public fields only |
| instance method | `function m(...): T;` | |
| static method | `static function m(...): T;` | |
| `const unsigned char*` + length | `Buffer` | one cp3 parameter, two C++ arguments (`cppArgCount: 2`) |
| `const char*` + length | `Buffer` | |
| `const <arithmetic>*` + length | `number[]` | one cp3 parameter, two C++ arguments |
| declaration in a namespace | flattened | `demo::add` is bound as `add`; the C++ name is kept in `cppName` |

`typedef` / `using` aliases are followed to their underlying type when the
alias is arithmetic or `std::string`.

### 2.5 Library discovery

* `--link <name>` is mandatory-shaped: a requested link that cannot be found
  in the search directories is an **error** (exit 2) naming the library and
  every directory searched.
* `--lib-dir <dir>` adds a search directory.
* Without `--link`, libraries whose file stem matches a bound header stem are
  detected automatically (`libfoo.a`, `foo.lib`, `foo.dll`, `libfoo.so`,
  `libfoo.dylib`, and the `foo.dll.a` import-library form).
* `staticLibraries`, `sharedLibraries` and `runtimeDlls` are recorded with
  absolute paths in `native.json` for the future build half.

### 2.6 Rejected declarations are named

Anything outside the table above is **rejected with a named diagnostic**; it is
never dropped silently. Every rejection is listed in `index.cp3` (header
comment) and in `native.json`'s `rejected` array, and is printed on stderr.
Current rejection reasons: templates, overload sets, variadic functions,
operator overloading, multiple inheritance, virtual bases, anonymous
records/enums, non-const pointers, returned raw pointers, `void*`, pointer to
`std::string`, array-by-value parameters, function-pointer types, and any type
outside the beta-one map.

A binding that keeps at least one usable declaration still succeeds and exits
0. A binding with **no** usable declaration exits 1 and prints the rejection
list.

---

## 3. Unsupported surface (what this pass does NOT do)

| # | Missing | Where it would go |
| --- | --- | --- |
| 1 | **`cp3 check` / `cp3 build` do not read `native.json`.** Nothing consumes a binding yet. | `src/cli/src/commands/check.cpp`, `src/cli/src/commands/build.cpp` |
| 2 | **`import native "id" as alias` is not implemented.** The module resolver has no `native` source kind; `import native` is a normal unresolved import. | `src/modules/src/support/import_resolver.cpp` |
| 3 | **No sema checking of native calls.** No cp3-native diagnostics, no argument type checking against a binding, no "stale binding" check (`contentHash` is written but never verified). | `src/sema/` |
| 4 | **No codegen lowering.** No `import native` call is turned into a C++ call; the generated C++ does not include the SDK headers, does not pass the binding's include dirs / defines, and does not link the recorded libraries. | `src/codegen/` |
| 5 | **No build integration.** `cp3 build` does not add `-I`/`-D`/`-L`/`-l` from a binding, and does not copy `runtimeDlls` next to the executable (`package.json`'s `"build": { "bundleNativeDlls" }` does not exist yet). | `src/cli/src/commands/build.cpp` |
| 6 | **No `cp3 native` / `cp3 native-add` surface, no `native.json` reader or validator.** `native.json` is written but never read back by cp3c. | `src/cli/src/native/metadata_read.cpp` (does not exist yet) |
| 7 | **No class instances / object lifetimes.** A bound class is a declaration shape only; there is no construction, destruction, ownership or `finalizer` story. | spec 19 follow-up |
| 8 | **No returned-pointer ownership classification.** Returned raw pointers are *rejected*, not classified as owned/borrowed. | spec 19 pointer safety table |
| 9 | **No `import native` aliasing of namespaces.** `cv::Mat` is bound as `Mat` in the index; the "flatten into the module alias" behaviour described in the design is not implemented because there is no module alias yet. | `src/codegen/`, `src/modules/` |
| 10 | **No multi-header / transitive-header policy.** Every header under the include root is parsed and bound; a declaration from a *system* header is skipped silently (it is not the SDK's API). | `src/cli/src/native/bindgen.cpp` (`inBoundHeader`) |
| 11 | **No `.cp3pak` / registry distribution of bindings.** | package manager follow-up |
| 12 | **No C-only (`.c`) headers.** The parser always uses `-x c++`. | `src/cli/src/native/bindgen.cpp` |
| 13 | **No cross-compilation target selection.** The target triple comes from the local `g++ -dumpmachine`, not from a `--target` flag. | `src/cli/src/native/sdk.cpp` (`gccParseArgs`) |

---

## 4. Verification

### 4.1 Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cp3c --parallel 2
```

Result: **green**, from a clean `build/` (the cache and `CMakeFiles/` were
deleted first, so all 97 targets were rebuilt). The build no longer needs an
LLVM installation: the libclang C API headers are vendored under
`vendor/third_party/clang-c/include` (pinned to `llvmorg-23.1.0`,
commit `ea7d852a70e8bdfaf601d6626a760f9771b2c4b4`, Apache-2.0 WITH
LLVM-exception). `-DCP3_LIBCLANG_INCLUDE_DIR=<dir>` builds against a system
LLVM include directory instead.

### 4.2 Targeted CLI tests (`--jobs 1`)

```
python tests/cli/run_cli_tests.py --filter native-install --jobs 1
python tests/cli/run_cli_tests.py --filter clean --jobs 1
```

| group | cases | pass | fail | skip |
| --- | ---: | ---: | ---: | ---: |
| native-install | 10 | 10 | 0 | 0 |
| clean (+ fmt/global/hooks cases matching "clean") | 17 | 17 | 0 | 0 |

The live cases carry `"requires": ["libclang", "compiler"]`; the harness's
`libclang` predicate skips them cleanly (with a printed reason) when no
libclang is installed, and points `CP3_LIBCLANG` at the discovered DLL
otherwise. No test uses the network.

### 4.3 Full CLI suite

```
python tests/cli/run_cli_tests.py --jobs 1 --full
```

Result: **332 ok, 1 failing, 0 skipped, 15 xfail** (333 cases).

The single failure is **`package/publish-out-file`**, which is **pre-existing
and unrelated to this branch**: `cp3c publish --out dist/bundle.cp3pak` calls
`fs::copy_file` without creating the missing `dist/` parent directory, so it
exits 2 instead of 0. Neither `src/cli/src/commands/publish.cpp` nor
`tests/cli/cases/package.json` is touched by this branch, and the publish path
does not go near the native code. The 15 xfail entries are the repository's
existing, deliberately marked known issues.

### 4.4 Layout lint

`python vendor/tools/lint.py` reports **0 errors** in every file this branch adds or
touches. (The repository-wide run still exits 1 because of the pre-existing
zero-placeholder gate - 391 markers in `src/std/typed-arrays` and elsewhere -
which is unrelated to this branch. `src/cli/src/cli.cpp` was brought back
under the 80-line hard function budget by extracting the self-owned command
dispatch.)

### 4.5 Manual end-to-end

```
cp3c native-install sdk --name demo --include sdk/include \
    --define DEMO_FEATURE=1 --lib-dir sdk/lib --link native_demo --out out
```

produces `out/demo/index.cp3` and `out/demo/native.json` with 9 functions,
2 classes (3 methods), 1 enum and 5 named rejections.

---

## 5. Completion percentage: **55%**

### 5.1 How it was calculated

The feature was split into **14 equally weighted work items** - 8 for the
command half, 6 for the compiler half - taken from the structure of the
archived design (`docs/cli-tooling/04-package-manager.md`). Each item scores
1 point when it is implemented *and* exercised by a test, 0 otherwise. No item
is scored on "most of it works": partial shapes are listed in section 3
instead.

| # | Work item | Half | Status | Points |
| ---: | --- | --- | --- | ---: |
| 1 | CLI surface: command, `--help`, every option, exit codes | command | done | 1 |
| 2 | SDK / header input resolution (directory, header, package name) | command | done | 1 |
| 3 | libclang discovery + dynamic loading (`CP3_LIBCLANG`) | command | done | 1 |
| 4 | Header binding: free functions, enums, simple classes/structs | command | done | 1 |
| 5 | Type mapping: primitives, strings, `Buffer`, `number[]` | command | done | 1 |
| 6 | `index.cp3` generation | command | done | 1 |
| 7 | `native.json` generation (beta-one schema) | command | done | 1 |
| 8 | Named rejection diagnostics for unsupported declarations | command | done | 1 |
| 9 | `native.json` reader + validator + staleness check | compiler | **not started** | 0 |
| 10 | `import native "id" as alias` module resolution | compiler | **not started** | 0 |
| 11 | Sema type-checking of native calls (cp3-native diagnostics) | compiler | **not started** | 0 |
| 12 | Codegen lowering of native calls (C++ include, `cppArgCount`, `returnsCStr`) | compiler | **not started** | 0 |
| 13 | Build integration (`-I`/`-D`/`-L`/`-l`, `runtimeDlls` bundling) | compiler | **not started** | 0 |
| 14 | Pointer-ownership and object-lifetime model (spec 19) | compiler | **not started** | 0 |

```
score = 8 done / 14 items = 0.571  ->  55% (rounded down to the nearest 5%)
```

Cross-check: the **command half is 8/8 = 100%** of the items this pass
claimed, and the **whole feature is 57%**. The quoted 55% is the honest
single number for the feature; the command half is finished, the compiler
half is not started. The smaller gaps listed in section 3 that sit *inside*
the command half (C-only headers, an explicit `--target`, the
system-header skip policy) are sub-features of items 4 and 5 and are not
scored separately.

### 5.2 What would move the number

* Item 9 (reader + validator) is the cheapest next step: ~7 points.
* Items 10 + 11 (import resolution + sema) together: ~14 points.
* Items 12 + 13 (codegen + build integration) together: ~14 points.
* Item 14 is a design decision, not just code: it is worth 7 points but
  requires the spec-19 pointer safety table to be settled first.

Items 9-13 together would take the feature to ~100% of the *binding
pipeline*, still short of a complete native-interop story (item 14).


---

## 6. Follow-up: what the "complete native-install" agent must do

1. **Add a `native.json` reader + validator** (`src/cli/src/native/metadata_read.cpp`):
   parse the file, check `schema == "cp3-native-binding/1"`, verify
   `contentHash` against the on-disk `index.cp3`, and report a stale binding
   with the remedy `re-run 'cp3 native-install <sdk> --name <id> --force'`.
2. **Add the `native` import kind** to the module resolver
   (`src/modules/src/support/import_resolver.cpp`): `import native "id" as alias`
   resolves `<project>/.cp3/native/<id>/native.json` and exposes its
   declarations under the alias.
3. **Add sema checking** (`src/sema/`): type-check native calls against the
   binding's parameter list, report cp3-native diagnostics (not C++ errors),
   and check the argument conversions the metadata records (`cstr`,
   `byteBuffer`, `numberArray`).
4. **Add codegen lowering** (`src/codegen/`): emit the C++ `#include` of the
   binding's headers, lower `alias.f(...)` to the real C++ call using
   `cppName` and the recorded `cppArgCount`, and map the return value
   (`returnsCStr`).
5. **Add build integration** (`src/cli/src/commands/build.cpp`): add the
   binding's `includeDirs` / `defines` / `libDirs` / `linkNames` to the
   incremental and `--release` profiles, and copy `runtimeDlls` next to the
   executable unless `package.json`'s `"build": { "bundleNativeDlls": false }`
   says otherwise.
6. **Decide the pointer-ownership and object-lifetime story** (spec 19) and
   either extend the binder or document the rejections as permanent.
7. **Add the compiler-side tests**: `cp3 check` on a program that imports a
   binding (both a passing and a failing case), and a `cp3 build` that links
   against a recorded library.

---

## 7. Design references

* `docs/cli-tooling/04-package-manager.md` - the archived beta-one design
  (command surface, binding surface, `native.json` schema, build
  integration). This pass implements the command half of that document.
* `docs/rough/DECISIONS.md` - compiler-wide decisions.
* `docs/rough/STYLE.md` - the layout budgets this branch was linted against.
* Preserved WIP for reference (read-only, **not** merged):
  `C:\Users\Krishnesh\dev\cp3-preserved\2026-10-21\deferred\native-install`
  (39 files, 16 conflicted files). This branch reuses only the `clang-c`
  headers from it.
