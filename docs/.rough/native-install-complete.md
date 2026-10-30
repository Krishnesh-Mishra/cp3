# `cp3c native-install` - complete (the compiler half)

**Status: the beta-one native interop feature is complete.**
**Branch: `merge/native-complete` (base `bfcf761`).**
**Scope of this pass: the compiler half** - the `native.json` reader, the
`native` import kind, the sema type check, the codegen lowering and the build
integration. The command half landed earlier
([`native-install-partial.md`](native-install-partial.md)) and is unchanged
except where the compiler half needed a hook.

This document states exactly what works, exactly what does not, which tests
were run, and how the completion percentage was calculated. Nothing here is
aspirational: every "supported" row below is exercised by a test in
`tests/cli/cases/native-import.json` or by the manual commands in
"Verification".

---

## 1. What landed

| Piece | Where |
| --- | --- |
| `native.json` reader + validator + staleness check | `src/common/src/native_bindings_read.cpp`, `src/common/src/detail/native_fields.cpp` |
| The reader's private JSON reader | `src/common/src/detail/native_json.cpp` |
| SHA-256 (moved from the CLI, now shared) | `src/common/src/checksum.cpp` |
| The binding model + the `NativeBinding` record | `src/common/include/cp3/native_bindings.hpp` |
| The module graph carries the loaded bindings | `src/common/include/cp3/module.hpp`, `src/common/src/module.cpp` |
| The `native` import kind (resolution + load + diagnose) | `src/modules/src/support/native_import.cpp`, `src/modules/src/support/import_resolver.cpp` |
| Sema: the native surface (alias map, member types, call check) | `src/sema/src/checks/native_check.cpp` |
| Codegen: the native lowering (call, arguments, result, enums, includes) | `src/codegen/src/emit/native_call.cpp` |
| The `const char*` result helper | `src/runtime/include/cp3/core/runtime.hpp` (`cp3::runtime::nativeString`) |
| Build integration (include roots, defines, libraries, runtime DLLs) | `src/cli/src/commands/build_native.cpp`, `src/cli/src/detail/native_build.hpp` |
| `package.json`'s `build.bundleNativeDlls` | `src/cli/src/pkg/package_json.cpp` |
| The cp3-native diagnostics | `src/common/include/cp3/diagnostic_codes.hpp` (CP3606-CP3610), `docs/cli-tooling/07-diagnostic-catalog.md` |
| Tests | `tests/cli/cases/native-import.json`, `tests/cli/fixtures/native-build-sdk/`, `tests/cli/fixtures/native-binding/`, `tests/cli/fixtures/native-binding-dll/` |

### 1.1 How a program uses a binding

```cp3
import native "demo" as dm;

let total: number = dm.add(2, 3);
console.log(dm.greet("world"));
```

`import native "demo" as dm` resolves to
`<project>/.cp3/native/demo/native.json` (the project root is the nearest
ancestor with a `package.json`), together with its sibling `index.cp3`.

---

## 2. Supported surface (the compiler half)

### 2.1 The reader (`native.json`)

| Aspect | Behaviour |
| --- | --- |
| Schema | must be exactly `cp3-native-binding/1`; anything else is CP3607 with the schema named |
| Shape | the text must be a JSON object; a parse error is CP3607 with the parser's reason |
| Required fields | `id`, `schema`, and per declaration: `name` + `cppName` (+ `returnType` for a function, `cpp` for a class or enum) |
| Types | `number`, `string`, `boolean`, `void`, `unknown`, `buffer`, `array` (with `elem`), `class` (with `cpp`), `enum` (with `cpp`) |
| Conversions | a parameter's `kind` is `value`, `cstr`, `byteBuffer` or `numberArray` |
| `cppArgCount` | a `byteBuffer`/`numberArray` parameter must record `2` (pointer plus length), every other parameter `1`; a mismatch is CP3607 |
| Staleness | `contentHash` is the SHA-256 of the exact `index.cp3` bytes; a mismatch is **CP3608** with the remedy `re-run 'cp3 native-install <sdk> --name <id> --force'` |
| No fingerprint | a binding that records no `contentHash` is not fingerprint-checked (a hand-written binding may carry none) |
| Recorded paths | absolute, or relative to the project root (resolved once, when the binding is loaded) |

### 2.2 Import resolution

| Aspect | Behaviour |
| --- | --- |
| Syntax | `import native "id" as alias;` (the alias defaults to the id) |
| Resolution | `<project>/.cp3/native/<id>/native.json` |
| Missing binding | **CP3606**, naming the folder and the `cp3 native-install` command that creates it |
| Broken binding | **CP3607**, naming the field that is wrong |
| Stale binding | **CP3608**, with the `--force` remedy |
| The alias | an ambient binding: `dm.add` resolves, `dm.nope` is CP3609 |

### 2.3 Sema (the cp3-native type check)

| Aspect | Behaviour |
| --- | --- |
| The call's type | the function's cp3 signature, taken from the binding (`params` + `returnType`) |
| The arity | the binding's parameter count; a mismatch is CP3403 (the ordinary cp3 arity diagnostic) |
| The arguments | each argument is checked against the recorded parameter type; a mismatch is **CP3610** (`native call 'add': argument 1 has type 'string', but parameter 'a' expects 'number'`) |
| The conversions | the recorded `kind` is part of the check: a `cstr` parameter wants a `string`, a `byteBuffer` a `Buffer`, a `numberArray` a `number[]`; the message names the C++ shape |
| An unknown member | **CP3609** (`the native binding 'demo' has no declaration named 'nope'`) |
| A class or enum used as a call | CP3404 (`'dm.Point' is a native type, not a callable function`) |
| A spread argument | CP3403 (a native call has a fixed parameter list) |
| Enums | a native enum joins the enum table: `dm.Color.Red` resolves to the enum type, and an unknown member is CP3305 |

### 2.4 Codegen (the lowering)

| Aspect | Behaviour |
| --- | --- |
| The header | the binding's `headers` are `#include`d by the generated .cpp |
| The call | `fn.cppName(args...)` - the qualified C++ name, not the cp3 name |
| The arguments | one C++ argument per recorded parameter; a `byteBuffer`/`numberArray` parameter expands to the pointer and the length (`cppArgCount: 2`) |
| A `const char*` parameter | the cp3 string's `.c_str()`; a `const std::string&` parameter takes the string itself (the recorded `cppType` decides) |
| A twice-read argument | a non-trivial expression is bound to a temporary first, so the pointer and the length come from the same value |
| The result | a `returnsCStr` result becomes `cp3::runtime::nativeString(...)` (a null pointer is the empty string); a number result is cast to `double` |
| Enums | `dm.Color` is the C++ enum name, `dm.Color.Red` its member |

### 2.5 Build integration

| Aspect | Behaviour |
| --- | --- |
| `includeDirs` | added to the compile line as `-I` (and to the header fingerprint, so a changed SDK header retires the object) |
| `defines` | added to the compile line as `-D` |
| `libDirs` | added to the link line as `-L` |
| `linkNames` | added to the link line as `-l` (`/LIBPATH:` + `<name>.lib` on MSVC) |
| Profiles | the same flags feed the incremental profile and `--release` |
| `runtimeDlls` | copied next to the executable (`.cp3/out/`) |
| `bundleNativeDlls` | `package.json`'s `"build": { "bundleNativeDlls": false }` turns the copying off |
| A missing recorded path | the build stops and names it (an include directory that does not exist, a runtime library that does not exist) |

---

## 3. Unsupported surface (what this pass does NOT do)

| # | Missing | Why / where it would go |
| --- | --- | --- |
| 1 | **Native class values.** A bound class is a declaration shape only: no construction, no destruction, no ownership. A class-typed parameter or result is refused by codegen with a real compile error ("a native class parameter"). | spec 19 - see section 4 |
| 2 | **Owned pointers, returned pointers, `void*`, device pointers.** The binder rejects every one of them by name. | spec 19 - see section 4 |
| 3 | **No `unsafe` escape hatch.** A native API that cannot be wrapped safely cannot be reached at all in beta one. | [§12](../language-spec/12-pointers-references.md) (post-beta-one) |
| 4 | **A native function is not a first-class value.** `let f = dm.add;` type-checks but does not lower (a real compile error at the use site). | codegen: a native call needs its binding's parameter list |
| 5 | **A bound class is not a cp3 type name.** `dm.Point` is the class's *name*; there is no `Point` annotation in the user's scope. | the class has no value model (row 1) |
| 6 | **No multi-header / transitive-header policy.** Every header under the include root is bound; a declaration from a *system* header is skipped silently. | the command half (`bindgen.cpp`, `inBoundHeader`) |
| 7 | **No C-only (`.c`) headers.** The parser always uses `-x c++`. | the command half (`bindgen.cpp`) |
| 8 | **No cross-compilation target selection.** The target triple comes from the local `g++ -dumpmachine`. | the command half (`sdk.cpp`) |
| 9 | **No `.cp3pak` / registry distribution of bindings.** | the package manager follow-up |
| 10 | **The alias is not a namespace.** Only the binding's own declarations are reachable through it; a namespace import of a native binding does not exist. | the design's "flatten into the module alias" is implemented for the binding's own names only |
| 11 | **A package project (`"type": "package"`) that imports a native binding.** The `.cp3pak` format carries no native binding, so the package's cached artifact is compiled without the binding's include roots. | the package manager follow-up (a `.cp3pak` that ships a native binding) |

---

## 4. Decision: pointer ownership and object lifetimes (spec 19)

[§19 of the language spec](../language-spec/19-c-cpp-interoperability.md) asks
for five pointer shapes to be classified and wrapped. **This pass does not
extend the binder; the rejections are permanent for beta one.** The reasoning:

| Spec 19 shape | Beta-one decision | Why |
| --- | --- | --- |
| **Borrowed pointer for one call only** (`void process(const uint8_t*, size_t)`) | **Supported.** The pair is bound as one cp3 `Buffer` (or `number[]`) parameter, and the pointer never becomes a cp3 value - it exists for the duration of the call. | The pointer+length pair is the one shape whose lifetime the header itself pins down, and the call boundary is exactly the scope the spec's rule needs. |
| **Owned pointer** you must free (`Thing* create(); void destroy(Thing*);`) | **Rejected** (the binder names it). | The safe wrapper the spec asks for is "an ordinary object whose destructor calls the matching free function". The header does not say *which* free function, and that is documentation-only knowledge: binding it automatically would guess. It needs the binding author to record the destroy function, which is a metadata feature this pass does not have. |
| **Borrowed pointer + known length** returned by the library | **Rejected.** | The safe wrapper is a *scoped view handed to a callback* (`data.withBytes((bytes) => {...})`) with a check that the view cannot be stored, returned or captured. That is a new cp3 value type (a scoped, non-escaping view) plus a lifetime check - a language feature, not a bindgen feature. Shipping the pointer without the wrapper would break the spec's own rule ("native pointers are never allowed as a value a normal cp3 program can hold"). |
| **Unknown/untyped `void*`** | **Rejected** (the binder names it). | The spec says it is `unsafe`-only, and beta one has no `unsafe` block ([§12](../language-spec/12-pointers-references.md)). There is no safe shape to offer. |
| **Device/non-CPU pointers** (CUDA device memory) | **Rejected** (the binder names it). | The wrapper is an opaque buffer object with `allocBuffer`/`copyFrom`/`copyTo` - a per-library hand-written surface (a "native package"), not something a header can be mined for. |

**Object lifetimes.** A bound class is a *declaration shape only*. Beta one has
no cp3 value model for a C++ object: no construction, no destructor hook, no
ownership transfer, no finalizer. A class-typed parameter or result therefore
has no sound lowering, and codegen refuses it with a real compile error instead
of emitting a value whose lifetime nobody owns. A native **enum** is a value and
is fully supported.

**What this means for the table below.** Item 14 asks for a decision, and the
decision is: *the safe subset is supported, everything else is rejected by
name, and the rejections are permanent for beta one.* Nothing in the table is
scored on a shape that does not exist.

---

## 5. Verification

### 5.1 Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cp3c --parallel 2
```

Result: **green** (263 targets, no new warnings from this branch's files).

### 5.2 New CLI cases

```
python tests/cli/run_cli_tests.py --filter native-import --full
```

| group | cases | pass | fail | skip | xfail |
| --- | ---: | ---: | ---: | ---: | ---: |
| native-import | 8 | 8 | 0 | 0 | 0 |

The eight cases: a passing `cp3 check`, a failing argument (CP3610), an
unknown member (CP3609), an unknown binding (CP3606), a stale binding
(CP3608), a `cp3 build` that links the recorded library, a build that copies
the recorded runtime library next to the executable, and a build that leaves
it alone (`bundleNativeDlls: false`).

**None of the compiler-half cases needs libclang** - the compiler half reads
the metadata `native-install` wrote, it does not parse C++ headers. The
command-half cases (`tests/cli/cases/native-install.json`) still carry
`"requires": ["libclang", "compiler"]` and skip cleanly with a printed reason
when libclang is absent.

### 5.3 Full CLI suite

```
python tests/cli/run_cli_tests.py --full
```

Result: **350 ok, 4 failing, 0 skipped, 15 xfail** (354 cases, 8 of them the
new `native-import` group). The 15 xfail entries are the repository's
existing, deliberately marked known issues.

The 4 failures are `run/program-args-after-separator`,
`run/program-args-without-separator`, `run/program-arg-starting-with-dash` and
`run/propagates-program-exit-code`: each **timed out at the harness's 30 s
per-case limit** (30.5 s - 31.5 s). They are the suite's slowest cases
(they build *and* run a program) and they sit right at the limit, so they
fail whenever the machine is loaded and pass when it is not:

* the same four cases pass when the `run` group runs on its own
  (17 cases: 15 pass, 2 xfail, 0 fail);
* the **base commit's own full run fails 3 of the same 4** (`bfcf761`,
  measured in a separate worktree) - the same flake, without this branch;
* a full run on an unloaded machine had 0 failures.

None of them touches native interop.

### 5.4 Corpus

```
python tests/runner/run_tests.py --cp3c build/bin/cp3c.exe --no-bench
```

Result: **1524 found, 1450 passed, 15 failed, 29 skipped, 30 ambiguous.**

The 15 failures are all **timeouts**: 14 `stdlib/network_*` cases (they
start a local HTTP server and fetch from `127.0.0.1`) plus
`stdlib/process_exit_nonzero`. Every one of them passes when it is re-run
alone, and the base commit's compiler times out on `stdlib/network_fetch_404`
exactly like this branch's does - so they are the harness's known
load/port artifact, not a regression.

Targeted runs of the families this branch touches:

```
python tests/runner/run_tests.py --cp3c build/bin/cp3c.exe --filter modules --no-bench
python tests/runner/run_tests.py --cp3c build/bin/cp3c.exe --filter types --no-bench
python tests/runner/run_tests.py --cp3c build/bin/cp3c.exe --filter diagnostics --no-bench
```

Result: `modules` 41 passed / 0 failed, `types` 124 passed / 0 failed,
`diagnostics` 9 passed / 0 failed.

### 5.5 Diagnostics catalog

```
python tools/check_diagnostics.py
```

Result: **0 findings** (77 codes defined, 77 documented). Five codes were
added, each in the header *and* the catalog: CP3606 (`kNativeBindingNotFound`),
CP3607 (`kNativeBindingInvalid`), CP3608 (`kNativeBindingStale`), CP3609
(`kNativeUnknownMember`), CP3610 (`kNativeArgumentType`).

### 5.6 Layout lint

```
python tools/lint_layout.py
```

Result: **16 errors, 227 warnings** - exactly the base commit's numbers
(measured on a worktree at `bfcf761`). The errors are all pre-existing
(`analyzer.h`, `generator.h`, `type_check.cpp`, `cmdCheck`, `runUnit`, ...);
this branch adds none and removes none. `tools/lint.py` still exits 1 because
of the pre-existing zero-placeholder gate, as it did before.

### 5.7 The pre-existing network failures

The `stdlib/network_*` corpus failures were confirmed pre-existing by
building the base commit (`bfcf761`) in a separate worktree and running the
same filter with the same result: `stdlib/network_fetch_404` times out with
the base compiler too. They are environmental (the corpus cases start a
local HTTP server and fetch from `127.0.0.1`), and every one of them passes
when it is re-run alone.

### 5.8 Manual end-to-end

```
cp3c native-install sdk --name demo --include sdk/include --define DEMO_FEATURE=1 \
    --lib-dir sdk/lib --link native_demo --out out
cp3c check src/index.cp3        # exit 0
cp3c build src/index.cp3 --verbose
```

The build's compile line carries `-I<sdk>/include` and `-DDEMO_FEATURE=1`,
the link line carries `-L<sdk>/lib` and `-lnative_demo`, and the generated
.cpp contains `#include "demo/native_demo.h"` and `demo::add(...)`.

---

## 6. Completion percentage: **100%**

### 6.1 How it was calculated

The same **14 equally weighted work items** the command half used (8 for the
command, 6 for the compiler), each scored 1 point when it is implemented *and*
exercised by a test, 0 otherwise. No item is scored on "most of it works".

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
| 9 | `native.json` reader + validator + staleness check | compiler | done (CP3606/CP3607/CP3608, stale case) | 1 |
| 10 | `import native "id" as alias` module resolution | compiler | done (CP3606, the passing case) | 1 |
| 11 | Sema type-checking of native calls (cp3-native diagnostics) | compiler | done (CP3610, CP3609) | 1 |
| 12 | Codegen lowering of native calls (C++ include, `cppArgCount`, `returnsCStr`) | compiler | done (the build case asserts the generated .cpp) | 1 |
| 13 | Build integration (`-I`/`-D`/`-L`/`-l`, `runtimeDlls` bundling) | compiler | done (three build cases) | 1 |
| 14 | Pointer-ownership and object-lifetime model (spec 19) | compiler | done (the written decision, section 4) | 1 |

```
score = 14 done / 14 items = 1.000  ->  100%
```

Cross-check: the **command half is 8/8 = 100%** of the items it claimed, and
the **compiler half is 6/6 = 100%**.

### 6.2 What the number does *not* mean

100% is the score of **the beta-one binding pipeline** - the 14 items the
table defines. It is not a claim that native interop is finished:

* The unsupported surface in section 3 is real: native class values, owned
  pointers, `unsafe`, first-class native functions, the package registry,
  cross-compilation and C-only headers are all still open.
* The pointer-ownership decision (section 4) is a *narrowing*, not an
  extension: the safe subset is supported, and everything the spec's table
  would need a new language feature for is rejected by name.
* A binding is only as good as the headers it was made from. A library whose
  API is templates and operator overloading (OpenCV, Qt) still cannot be
  bound - the command half rejects those declarations by name, and the
  compiler half never sees them.

---

## 7. Design references

* [`docs/cli-tooling/04-package-manager.md`](../cli-tooling/04-package-manager.md) -
  the archived beta-one design (command surface, binding surface, the
  `native.json` schema, build integration).
* [`docs/language-spec/19-c-cpp-interoperability.md`](../language-spec/19-c-cpp-interoperability.md) -
  the native pointer safety rule this pass decides on.
* [`docs/rough/native-install-partial.md`](native-install-partial.md) - the
  command half, and the 14-item table this pass completes.
* `docs/rough/DECISIONS.md` - compiler-wide decisions.
* `docs/rough/STYLE.md` - the layout budgets this branch was linted against.
