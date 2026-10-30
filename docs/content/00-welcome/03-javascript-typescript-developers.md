---
title: For JavaScript and TypeScript developers
chapter: Welcome
---
This guide is for developers who already know JavaScript or TypeScript and want to write, review, or maintain CP3 programs. It focuses on what
transfers, what does not, how to find the real supported surface, and how to work safely in the CP3 compiler repository. It is also a handoff page for
coding agents. An agent that receives this page should use CP3 source and conformance cases as the authority, avoid inventing Node or TypeScript
compatibility, and keep compiler changes separate from benchmark changes.

## What CP3 is
CP3 is a statically typed language with syntax shaped by JavaScript and TypeScript. Its compiler checks CP3 source, generates C++, and asks a native
C++ toolchain to compile and link the result. CP3 source files use the `.cp3` extension. They are not JavaScript files and are not passed to Node. CP3
does not execute TypeScript by erasing annotations at runtime. Types participate in semantic analysis and influence code generation. The native output
is an implementation detail. Application code should follow CP3 language and runtime behavior, not assumptions about which C++ types the compiler
currently emits. CP3 aims to combine familiar application syntax, static checks, explicit standard-library APIs, and native compilation. That goal
does not mean every JavaScript, TypeScript, browser, Node, npm, or C++ feature is available.

## A first CP3 program

```ts
import console from "std/console";

function sum(values: number[]): number {
  let result = 0;
  for (let index = 0; index < values.length; index = index + 1) {
    result = result + values[index];
  }
  return result;
}

console.log(sum([2, 3, 5]));
```

This example uses a module import, a function signature, a typed array parameter, a mutable local, a counted loop, indexed reads, and a console call.
Check its syntax and types with `cp3 check path/to/main.cp3`. Build and run it with `cp3 run path/to/main.cp3`. Create a starter project with `cp3
init hello-app`.

## Fast map for JavaScript and TypeScript developers

| Familiar idea | CP3 rule to remember |
| --- | --- |
| `const` and `let` | The binding is statically typed; `const` prevents rebinding, not mutation of a referenced array or object. |
| Type annotations | The compiler checks them before generating C++. |
| `number` | A 64-bit floating-point number, not a JavaScript-independent integer type. |
| Arrays | Typed arrays of CP3 values; arrays are reference-shaped and can be mutated through aliases. |
| Objects | Object literals have checked shapes in typed contexts; arbitrary dynamic index signatures are not supported. |
| Classes | Class instances are reference-shaped values under the ordinary runtime path. |
| `async` and `await` | Use the CP3 promise and event-loop APIs; async does not imply another OS thread. |
| Node globals | They are not automatically present. Import a supported `std/...` module. |
| npm packages | Arbitrary JavaScript packages do not run in CP3. |
| Garbage collection | Ordinary references use reference counting; CP3 does not have a tracing collector. |
| Native build | CP3 emits C++ and needs a supported native toolchain to create the executable. |

The rest of this guide explains these points with examples and repository workflow.

# 1. Start and build a project

## Create the scaffold

```sh
cp3 init hello-app
cd hello-app
```

The starter application includes project metadata and a `src/index.cp3` entry. For automation, `cp3 init --yes` accepts defaults without an
interactive prompt. Use `cp3 init --type package` when creating a package rather than an application.

## Check, run, and build
`cp3 check file.cp3` loads, parses, resolves, and type-checks the source without generating C++ or linking an executable. `cp3 run file.cp3` builds
and executes the selected source entry. `cp3 build file.cp3` builds the executable without running it. `cp3 build file.cp3 --release` requests an
optimized release build. `cp3 build file.cp3 --incremental` requests a fast cached build without LTO or PGO. Use the command's `--help` output for
accepted option placement and the exact flags in the compiler you have installed. Project build artifacts live under `.cp3/build`; the built
application is placed under `.cp3/out`. Normal builds can reuse cached translation units when inputs, compiler, headers, and profile still match. `cp3
clean` removes build and output artifacts. `cp3 clean --all` also clears package and native caches. Do not assume deleting an object file is necessary
after every source edit. Use the supported clean command only when a clean build is intended or the cache is suspected to be stale.

## Toolchain
CP3 application builds use the configured native C++ toolchain, normally Clang in this beta. Use `cp3 toolchain` to inspect or configure a supported
compiler. `cp3 toolchain list` displays detected options where supported by the installed CLI. The CP3 compiler repository itself is built with CMake
and a C++20-capable compiler. Building the CP3 compiler and building a CP3 application are separate workflows.

# 2. Syntax that transfers
CP3 intentionally uses familiar punctuation and core control-flow keywords. Supported forms include braces, semicolons, `if`/`else`, `while`, counted
`for`, functions, classes, interfaces, enums, imports, template strings, and `async`/`await` in supported contexts. The syntax resemblance is a
starting point, not a compatibility guarantee.

## Declarations

```ts
const appName: string = "reporter";
let retryCount: number = 0;
retryCount = retryCount + 1;
```

The type annotation follows the binding name. Initializers often allow local type inference. Give empty arrays and ambiguous declarations explicit
types. `const` declares a binding that cannot be reassigned. `let` declares a binding that can be reassigned to another value of the same checked
type. `var` is accepted for legacy function-scoped behavior, but new code should normally use `const` or `let`.

## Functions
Function declarations use typed parameters and a return type.

```ts
function formatCount(count: number): string {
  return "count=" + count;
}
```

The analyzer checks call arity, argument types, and the result expected by the function's callers. Every required return path in a non-void function
must produce a compatible value. Use `: void` when a function intentionally returns no useful value. Optional parameters use `?` after the parameter
name.

```ts
function displayName(name?: string): string {
  return name ?? "anonymous";
}
```

Default parameter values are supported in the syntax checked by the parser and analyzer. Arrow functions are used for callbacks and closures.

```ts
const isLong = (text: string): boolean => text.length > 20;
```

Annotate callback parameters when contextual inference does not determine the intended type.

## Conditions and loops
Conditions must satisfy CP3's supported boolean rules. `&&` and `||` short-circuit. `!` negates a boolean. `??` selects a fallback for `null` or
`undefined`, not every false-like value. Counted loops use initializer, condition, and update expressions.

```ts
for (let index = 0; index < 10; index = index + 1) {
  console.log(index);
}
```

`break` exits the innermost loop and `continue` starts its next iteration. The compiler supports `for...of` for implemented iterable/value shapes,
including arrays and some collections. Do not assume the full JavaScript iterator protocol is implemented. `for...in` over a plain object is rejected.
Use a typed collection or an explicit object shape instead.

## Object literals and destructuring
Object literals use familiar key-value syntax.

```ts
interface User {
  name: string;
  active: boolean;
}

const user: User = {
  name: "Mira",
  active: true,
};
```

Typed object literals are checked for required fields and relevant excess fields. Arbitrary open object index signatures are not supported. Use
`Map<K, V>` for dynamic key/value collections. Destructuring is supported for several checked array, tuple, object, and interface forms. Destructuring
is not a guarantee that every TypeScript binding pattern works. Check parser and semantic behavior before introducing a complex rest or computed-key
pattern.

## Classes and interfaces
Class fields, constructors, methods, inheritance forms, and interface implementation are checked.

```ts
interface Named {
  name: string;
}

class Person implements Named {
  name: string;

  constructor(name: string) {
    this.name = name;
  }
}
```

The compiler rejects multiple class inheritance. Overrides and interface members are checked according to CP3's class model. Decorators and arbitrary
dynamic class features should not be assumed to work. Classes are reference-shaped values in the normal runtime representation. The compiler may apply
a narrow local escape optimization to eligible objects; a `new` expression does not promise stack allocation.

## Generics, aliases, and tuples
CP3 supports generic functions, classes, interfaces, and supported type aliases.

```ts
function identity<T>(value: T): T {
  return value;
}

type Pair<T> = [T, T];

const result = identity<string>("ready");
```

The compiler specializes supported generic uses into concrete generated forms. Specialization does not guarantee allocation elimination or zero
runtime cost. CP3 does not implement all TypeScript conditional, mapped, indexed-access, or higher-kinded type features.

## Enums
CP3 supports enum forms with consistent string or numeric backing as defined by the parser and analyzer. Do not assume every TypeScript enum
auto-numbering or reverse-mapping behavior. Consult `tests/cases/enums` for the accepted forms and observable behavior.

# 3. Types and values

## Built-in types

| Type | Meaning |
| --- | --- |
| `number` | A 64-bit floating-point value. |
| `string` | UTF-8 text. |
| `boolean` | `true` or `false`. |
| `null` | A distinct null value. |
| `undefined` | The absent-value sentinel. |
| `void` | A function result with no useful value. |
| `unknown` | A value whose operations need a supported checked path. |

`any` is deliberately rejected. Do not use `unknown` as if it meant unrestricted dynamic access. Use a union when a value may have more than one
supported type.

```ts
let maybePort: number | undefined = undefined;
let selectedName: string | null = null;
```

`null` and `undefined` are separate. `number | undefined` does not accept `null`. Optional parameters may be omitted and are handled through the
optional form supported by CP3. The nullish coalescing operator handles `null` and `undefined`; `||` follows boolean short-circuit behavior.

### Selecting native type spellings

`package.json` can set `"typeSystem"` to `"typescript"`, `"cpp"`, or `"rust"`; TypeScript mode is the default. This selects how unprefixed native
scalar names are resolved. It does not select a Rust backend: CP3 still generates C++. `cp3 init --type-system rust` selects Rust-style names for a
new project; edit the manifest to change an existing one.

```json
{
  "typeSystem": "rust",
  "strictNumericCasts": true
}
```

With `typeSystem: "rust"`, plain names such as `i64`, `u32`, and `usize` resolve through CP3's supported Rust scalar table. `typeSystem: "cpp"`
accepts supported C++ spellings such as `int`, `long long`, and `size_t`. The ordinary CP3 names `number`, `string`, `boolean`, and related types
remain usable in either mode. This setting changes type-name resolution; it does not make foreign classes, pointers, templates, or ABI declarations
available.

Prefixes make the vocabulary explicit in a single annotation, regardless of project mode:

```ts
let recordId: r_'u64' = 9007199254740992;
let rate: ts_'number' = 0.75;
let cppCount: c_'long long' = 9007199254740992;
```

`r_`, `c_`, and `ts_` select supported Rust, C++, and CP3/TypeScript names. The prefix followed by a quoted spelling, such as `r_'u32'`, is a type
annotation. It is not a cast, pointer declaration, or import of another language's runtime. `strictNumericCasts` is a separate boolean that tightens
implicit native numeric conversions. The canonical mapping and policy are implemented in `src/common/src/native/native_types.cpp` and
`src/sema/src/checks/native_types.cpp`; the related conformance fixtures live under `tests/cases/types/02` and `tests/cases/types/03`. See the [native
scalar type guide](#01-language/07-native-types/00-overview) for the supported spellings and numeric rules.

## Inference and annotations
The analyzer infers types from many initializers.

```ts
let attempts = 0;
let caption = "Waiting";
let connected = false;
```

Reassignment remains type checked after inference. An inferred string binding does not become a union merely because a later assignment uses another
type. Add an annotation at API boundaries, for uninitialized or empty values, and where it makes intent clear.

```ts
const values: number[] = [];
function findName(id: number): string | undefined {
  return undefined;
}
```

Type aliases name supported type expressions and do not create runtime wrapper objects.

## Numbers are not JavaScript integers
The general `number` type is a double-precision floating-point value. Integer-valued arithmetic is exact only within the range and values
representable by that representation. Use fixed-width typed arrays when the data layout specifically requires 8-, 16-, or 32-bit elements. Do not
infer an integer overflow contract from C++, Rust, or JavaScript. CP3 also has explicit native integer types for interop and supported low-level use.
Those native types have different portability and ABI considerations from `number`.

## Strings are UTF-8
CP3 strings are UTF-8 text. `string.length` reports byte length, not grapheme count. For multi-byte text, a visible character may occupy several
bytes. `charAt` returns a whole UTF-8 character in its documented form. `charCodeAt` is byte-oriented and does not return a general Unicode scalar.
Indexing and string helpers should be chosen with the byte representation in mind. String methods return new strings; strings are not mutated in
place.

## Arrays and references
`T[]` is a growable typed array. The element type is checked for indexed writes and method arguments. An empty literal generally needs a contextual or
explicit element type. Array references can be aliased.

```ts
const first: number[] = [1];
const second = first;
second.push(2);
```

Both bindings refer to the same array value; `const` prevents rebinding `second`, not mutation through `push`. Array `push`, `pop`, `sort`, `unique`,
and `shuffle` mutate the referenced array. `map`, `filter`, `slice`, and `flat` return arrays according to their documented behavior. Read the array
reference for each method's exact return type, mutation, and edge cases.

## Typed arrays and DataView
Typed arrays provide fixed-width numeric elements in a bounded view. Available families include `Int8Array`, `Uint8Array`, `Uint8ClampedArray`,
`Int16Array`, `Uint16Array`, `Int32Array`, `Uint32Array`, `Float32Array`, and `Float64Array`. `ArrayBuffer` owns bytes; typed arrays and `DataView`
interpret them. `DataView` supports byte-offset reads/writes for several widths and explicit endianness. Its default byte order is big-endian; the
supported `littleEndian` argument can request little-endian behavior. Out-of-range reads and writes follow the checked runtime semantics of that view
family. Do not convert them to raw-pointer accesses in hand-written code. The compiler's DataView raw-read analysis is currently incomplete: recorded
facts are not consumed by per-read lowering. There is no basis to claim SIMD for a loop without generated C++ and a backend report for that loop.

## Maps and sets
`Map<K, V>` and `Set<T>` are named exports from `std/collections`. Map lookup may return `undefined` when the key is absent. Both collections preserve
insertion order in their documented iteration methods. They are not plain JavaScript objects and do not inherit object-property semantics. Use the
exact CP3 generic types and methods from the collection page.

# 4. Modules and platform APIs

## Imports are explicit
There are no ambient Node or browser globals unless a CP3 module or language feature provides them. Standard modules are resolved from a closed CP3
registry. Import syntax depends on the module: some expose a default namespace, and some expose named exports.

```ts
import console from "std/console";
import { Buffer } from "std/buffer";
import { env } from "std/env";
```

Check the standard-library overview before choosing an import form. An unknown `std/...` module is not silently accepted as a global. Unknown members
on closed standard modules are compile-time errors. Project modules use CP3's module resolver and project/package metadata. Do not assume Node's
CommonJS loader, arbitrary URL imports, or npm resolution.

## Standard-library map

| Import | Current purpose |
| --- | --- |
| `std/console` | Terminal output, input, key reads, and stdin listeners. |
| `std/env` | Read environment values, test presence, and load a .env-style file. |
| `std/process` | Arguments, environment Map, exit, and child-process helpers. |
| `std/fs` | Synchronous, promise-based, and streaming file operations. |
| `std/path` | Platform-aware path manipulation. |
| `std/os` | Host, CPU, home, temporary directory, and memory information. |
| `std/math` | Numeric constants and math helpers. |
| `std/date` | Current time, ISO parsing, formatting, and date arithmetic. |
| `std/json` | JSON parsing and serialization. |
| `std/crypto` | Random bytes, UUID, hashes, and AES-256-GCM helpers. |
| `std/buffer` | Byte buffers and UTF-8, hex, and base64 conversion. |
| `std/collections` | Generic Map and Set types. |
| `std/network` | HTTP fetch/server and WebSocket APIs. |

Arrays, strings, typed arrays, ArrayBuffer, DataView, and other language built-ins do not require those imports unless their page says otherwise.

## Node process is not CP3 process
`std/process` exposes `argv`, `env`, `exit`, `exec`, and `spawn`. `process.env` is a CP3 `Map<string, string>`. `process.exec` captures stdout and
stderr and returns an `ExecResult` after the child exits. A non-zero child exit code is a resolved `ExecResult`; launch failure rejects.
`process.spawn` returns a live `SpawnHandle` with `on`, `write`, and `kill`. The current API does not provide Node's `process.stdout.write`,
`process.stderr`, or `process.stdin` stream objects. Use `console.log` and `console.error` for complete terminal lines. Use `spawn` when you need
child output events while a process is running. Do not promise a Node API merely because the name `process` exists.

## No browser platform
CP3 does not run in a browser and does not provide the DOM. Browser globals such as `window`, `document`, and `localStorage` are not automatically
defined. Browser APIs must be replaced with a supported CP3 module or an explicit native binding.

## npm and third-party packages
CP3 does not execute arbitrary npm JavaScript packages. The CLI has CP3 package workflows for `.cp3pak` artifacts and native dependencies. A package
name in `package.json` is not proof that its JavaScript can be imported. Before porting a dependency, check whether a CP3 package or standard-library
equivalent exists. If no implementation exists, decide whether to replace the dependency or expose a narrow native API.

## Network scope
`std/network` supports the documented CP3 fetch, HTTP server, and WebSocket surfaces. The current fetch and WebSocket client implementation has
protocol limits, including no HTTPS/TLS in this beta. The server exposes CP3 route and response abstractions; it is not a complete Express or Node
compatibility layer. Check the module page for request/body buffering, callback shapes, timeout, and close behavior.

## File-system scope
File paths are strings interpreted by the host file system. Relative paths are based on the process working directory. Use `std/path` for separators
and composition. Handle the synchronous throw or asynchronous rejection behavior documented by the selected operation. An `exists` check can become
stale before the following operation; perform the operation and handle its failure.

# 5. Errors, async, and concurrency

## Compiler errors
Parsing, module-resolution, and type errors are reported before C++ generation. Diagnostics include a CP3 code and source location when available. Fix
the earliest source error first; later messages may depend on it. The compiler may create an HTML diagnostic page with the source excerpt and the
three guide pages with the greatest TF-IDF cosine text similarity. The ranked pages are suggestions, not guaranteed explanations or fixed error
mappings.

## Runtime errors
Some value-dependent failures occur while a program runs. File operations can fail because of permissions, paths, or device state. Network operations
can reject for DNS, connection, timeout, or protocol failures. Typed-buffer operations enforce their range and conversion rules. Use the documented
`try`/`catch` and promise rejection paths for the API you call. Do not assume all runtime failures are represented by a returned `Error` object.

## Async and await
CP3 async functions use promises and the event-loop runtime. `await` suspends a continuation until the operation settles. Async I/O is not equivalent
to creating a thread. A long synchronous computation can block event-loop progress. Await operations that can reject, or allow the failure to
propagate intentionally. Check promise combinator and callback signatures against the CP3 version.

## Parallel and worker execution
Parallel APIs and workers are separate from ordinary async functions. They use OS-thread execution or worker boundaries according to the API. The
semantic checker restricts supported captures, mutation, payloads, and result shapes. Do not assume every object can be shared safely because it is
represented by a reference. Reference counting protects control-block lifetime; it does not make the object's fields thread-safe. Use only the
concurrency APIs and synchronization rules documented by CP3.

# 6. Memory model and performance expectations

## Reference ownership
Ordinary reference-shaped values use the CP3 runtime's `Ref<T>` model. In the current runtime, `Ref<T>` aliases `std::shared_ptr<T>`. The standard
`makeRef<T>` constructs ordinary objects through `std::allocate_shared` and CP3's bounded thread-local free-list allocator. Copies and drops use
atomic shared ownership bookkeeping. A restricted class set can use `LocalRef<T>` with a non-atomic count when the compiler's conservative
compilation-unit gate permits it; this still uses an allocated control block. CP3 does not have a general tracing garbage collector. Strongly
connected reference cycles can keep objects alive; use weak references where the language and runtime support them. `const` is not a lifetime
annotation and does not promise stack storage.

## Escape analysis
The code generator contains a conservative, local escape-analysis optimization. It recognizes a restricted set of simple class constructions and uses.
Some eligible local objects or arrays may become local value storage instead of shared heap objects. This is not whole-program escape analysis and is
not a guarantee for every allocation inside a loop. Unknown calls, aliases, escapes, unsafe constructors, unsupported fields, or cross-frame use can
disable the transformation. Use the runtime and optimization chapters for exact current eligibility.

## Non-atomic references and arenas
The runtime pool reuses some released allocation blocks; it is not a bump arena and does not bulk-free objects. The separate escape-analysis pass
can lower only narrowly supported local objects and arrays to stack/value storage. Inspect generated C++ before attributing a benchmark result to
any of these representations.

## SIMD and native optimization
CP3 emits C++, then a native compiler optimizes that generated program. Native compilation does not imply vectorization. Helper calls, bounds checks,
complex conditions, aliasing, and unsupported pointer proofs can prevent a loop from vectorizing. The DataView fast-read facts in the current analysis
are not wired into per-read code generation. Do not claim that a CP3 DataView loop is vectorized unless generated C++ shows the direct loop and a
compiler report identifies that loop. A report pointing into a standard-library header does not establish vectorization of benchmark logic.

## Benchmark discipline
Compare the same workload, output, compiler versions, build profile, and machine conditions. Report median, best, p95, and variation when the runner
provides them. Large variation means the median may not be stable evidence. Do not change benchmark source to make a compiler pass or benchmark result
look better. Inspect generated C++ before attributing a speedup to a compiler optimization. Record the exact command and compiler build used for the
measurement.

# 7. A practical porting workflow

## Step 1: inventory external dependencies
List the Node, browser, and npm APIs used by the source. Classify each as a language feature, a CP3 standard module, a package, or a native
dependency. Do not start by translating imports mechanically. Check the supported module table first.

## Step 2: define data types
Replace untyped or `any`-based boundaries with CP3 types. Use unions to represent explicit optional states. Use `Map<K, V>` for dynamic keys. Use
typed arrays only when fixed-width numeric storage is intended. Add interface declarations for stable object contracts.

## Step 3: port one module at a time
Start with a small function or leaf module. Annotate exported function parameters and results. Replace Node globals with supported imports. Port
errors and failure paths deliberately. Keep the public module boundary smaller than the original dynamic JavaScript surface where practical.

## Step 4: check before building
Run `cp3 check` against the changed entry or use the repository's relevant test filter. Read the first diagnostic in full. Confirm whether it is a
parse, import, type, C++ compile, or link failure. Do not treat a C++ diagnostic as proof that CP3 accepted the intended semantics.

## Step 5: execute representative paths
Run success and failure paths. Check output, exit code, file effects, and async rejection behavior. Verify empty and boundary inputs. Use the targeted
test category for a language or runtime change.

## Step 6: inspect native output for performance work
Build with the intended profile and record the native compiler. Use the runtime snapshot option when you need to preserve the generated C++ and
support files. Inspect the actual hot loop or allocation path. Run the benchmark with enough repetitions and report variation. Keep correctness
verification separate from speed claims.

# 8. Working in the CP3 repository
This section is intended for coding agents and contributors modifying CP3 itself.

## Find the owning layer first
The repository separates compiler and runtime responsibilities.

| Area | Typical responsibility |
| --- | --- |
| `src/lexer` | Tokenization of CP3 source. |
| `src/parser` | Grammar and AST construction. |
| `src/sema` | Name resolution, type checking, and diagnostics. |
| `src/codegen` | Lowering checked AST forms into C++. |
| `src/runtime` | Shared value, async, memory, and concurrency support. |
| `src/std` | Standard-library semantic, codegen, and runtime families. |
| `src/modules` | Project and standard module resolution. |
| `src/cli` | User commands, build pipeline, caches, and diagnostics. |

Do not implement a parser rule in codegen. Do not implement a type restriction only as a C++ compile error if the analyzer can issue a CP3 diagnostic.
Do not put standard-module behavior in an unrelated generic runtime helper when the module family owns it. Follow nearby files and CMake target
ownership before adding a source file.

## Use implementation and tests as one contract
Before changing a feature, find its parser node, semantic rule, code generation, runtime call, and conformance cases. The behavior often spans more
than one layer. A passing `cp3 check` covers the front end but does not prove C++ generation or runtime behavior. A generated program that compiles
does not prove edge cases or diagnostics are correct. When implementation and prose disagree, identify which one is wrong and update the appropriate
source and documentation. Do not change an expected output merely to silence a regression.

## Test-case organization
The conformance runner accepts two layouts: flat `<name>.cp3` plus `<name>.expect.json`, and a case directory containing `main.cp3` plus `output.json`.
Both use a JSON expectation with `mode: "check"` for front-end behavior or `mode: "run"` for native execution and output comparison. An `error`
assertion checks a compile diagnostic code, source position, or message substring. Add a new case for new behavior rather than rewriting unrelated
existing cases. See `tests/cases/README.md` and the discovery logic in `tests/runner/run_tests.py` for the current schemas and pairing rules.

## Running focused tests
Use the repository runner:

```sh
python tests/runner/run_tests.py --filter typed-arrays --no-bench
```

Replace the filter with the category or path fragment relevant to the change. The runner can invoke the benchmark suite after tests; pass `--no-bench`
when only conformance cases are intended. Use `--jobs 1` to run the corpus sequentially when concurrency hides a failure or the machine is resource
constrained. Use `--verbose` to see every case. Read the runner's current `--help` before relying on old flags.

## Build the compiler
The normal repository compiler build is:

```sh
cmake --build build
```

The configured build directory may be reused. CMake may reconfigure if source lists or configure dependencies change. An incremental CMake build
should rebuild changed translation units and their dependents rather than deleting every object. Do not invent ad-hoc build scripts when the
repository has a documented CMake or runner workflow.

## Benchmark source discipline
Benchmark source is a measurement input. Do not modify benchmark cases when the task is to improve compiler or runtime performance. Run named targets
with `python benchmarks/runner/run_benchmark.py --only <name>`. Use `--reps N` when the runner supports repeated samples and report the exact count.
Wait for other compiler builds or benchmark processes to finish before collecting a supposedly quiet measurement. Do not treat high-variation data as
a stable optimization win.

## Generated artifacts
For CP3 application debugging, use the supported `--show-runtime` / `--show` option to preserve generated C++ and the exact runtime support snapshot
in the project build tree. Generated files help explain the compiler output but should not be patched as a substitute for changing the owning compiler
source. For SIMD work, match the vectorization report to the generated benchmark source, not to any line in the whole compiler invocation. For
allocation work, verify whether generated output still contains `makeRef`, `make_shared`, or per-iteration helper calls.

## Documentation sources
The user-facing guide lives under `docs/content/` as Markdown. The viewer lives at `docs/viewer/index.html`. CMake embeds the Markdown entries and
viewer into the compiler. After changing a page or viewer, rebuild `cp3c` to refresh the embedded copy. Keep front matter with both `title` and
`chapter` in every Markdown page. Use existing page slugs for internal links and check links after moving a page. Do not add a fixed
diagnostic-to-page table: diagnostic help ranks sections from their text similarity.

# 9. Common porting replacements

| JavaScript or Node usage | CP3 direction |
| --- | --- |
| `console.log` | Import `std/console`; use its documented output functions. |
| `process.env.NAME` | Use `std/env` or `process.env` Map methods. |
| Node `fs.readFileSync` | Import `std/fs`; check CP3 signature and failure behavior. |
| Node `path.join` | Import `std/path`. |
| Browser `fetch` | Import CP3 `fetch` from `std/network`; check protocol and option limits. |
| `Buffer.from` | Import `Buffer` from `std/buffer`; use supported encodings. |
| Dynamic object dictionary | Use `Map<K, V>` when keys are open-ended. |
| `any` | Define a union/interface or use `unknown` with an implemented checked path. |
| npm JavaScript dependency | Find a CP3 package, port the small required behavior, or design native interop. |
| `process.stdout.write` | No direct Node stream equivalent currently; use console line output or a supported native interface. |
| Detached async work | Keep explicit ownership of promise/task lifecycle and captured values. |

These are starting points, not source-to-source transformations. Match error behavior, ordering, byte/text representation, and lifecycle semantics.

# 10. Questions to answer before an agent edits CP3
Before changing a feature, establish the accepted source syntax, owning semantic/codegen/runtime layers, existing success and failure tests, and the
exact command that verifies the change. For a standard-library member, confirm its module registration, export style, closed member signature, runtime
implementation, and sync/async behavior. For an optimization, state the proof, fallback, observable semantics, generated-code evidence, and benchmark
method before claiming a win. Use the review checklist below as the final gate. Never infer support from TypeScript, Node, Rust, or C++ alone.

# 11. A concise review checklist

## Language and type changes

- Parser accepts only the intended grammar.
- Semantic analysis reports CP3 diagnostics at the source location.
- Valid source still reaches code generation.
- Invalid or unsupported forms fail before native compilation when possible.
- `null`, `undefined`, unions, and optional values retain their distinctions.
- Closures, aliases, mutation, and control flow preserve their observable behavior.

## Runtime and standard-library changes

- Runtime results match the declared CP3 types.
- Bounds, conversion, and empty-input cases are specified.
- Async failures and process exit behavior are checked.
- Resource shutdown is explicit where the API owns a socket, file, or child.
- Cross-thread writes are synchronized or rejected.
- Strong-reference cycles and lifetime boundaries are considered.

## Optimization changes

- The proof is conservative and has a checked fallback.
- No benchmark name or contents are hard-coded into compiler behavior.
- Observable error, mutation, alias, and ordering semantics remain unchanged.
- Generated code demonstrates the intended transformation.
- Native compiler reports refer to the right source loop.
- Correctness results and performance results are reported separately.

## Documentation changes

- Examples use syntax accepted by CP3.
- Imported module names and members exist in the registry or semantic table.
- Runtime claims are verified against the owning implementation.
- Limitations are explicit where compatibility is incomplete.
- Internal page links resolve to existing Markdown slugs.
- The built compiler is rebuilt when bundled docs change.

When writing CP3 for a user, prefer the smallest supported construct that makes the intended behavior clear. When changing the compiler, follow the
owning layer, add focused conformance coverage when appropriate, and state exactly what you built or measured. Continue with [Chapter
1](#01-language/00-overview), [Runtime](#02-runtime/00-overview), [Standard library](#03-standard-library/00-overview), or [Compiler and
tools](#04-compiler-cli/00-overview) for topic-specific detail.
