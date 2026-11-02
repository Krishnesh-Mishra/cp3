# CP3 Roadmap

This file records what each release is changing, what is deliberately not
included, and why. It is the public checklist for the current release.

## v0.2.0 - TypeScript compatibility

Status: **in progress**

Theme: ordinary TypeScript class and error code should work in CP3, while the
compiler keeps emitting plain native C++ with no hidden runtime machinery.

### CP3 v0.2 APPROVED WORK LEDGER

**Lead-approved scope. Anything not listed here needs lead approval before it
is added.** Rule 0 applies to every entry:

- User-visible API stays TypeScript-like.
- Every fast path is internal to the standard library/runtime.
- Every proof/check fails closed.

#### A. Collections (`std/collections`)

| Item | User API | Internal mechanism (fixed) | Hard rule |
| --- | --- | --- | --- |
| `Queue<T>` | `new Queue<T>()`, `push/pop/peek/size/empty/reserve` | ring buffer over **one contiguous `std::vector<T>`**, head/tail indices, 2x growth | never `std::deque` |
| `Stack<T>` | `push/pop/peek/size` | `std::vector` `push_back`/`pop_back` | `pop` returns a plain value, no optional wrap |
| `PriorityQueue<T>` | `push/pop/peek` with comparator | binary heap over a contiguous vector, manual sift or `push_heap`/`pop_heap` | no tree nodes |
| `LRUCache<K,V>` | `get/set/has/delete` with capacity | flat hash map (`ankerl::unordered_dense`) + intrusive doubly-linked list stored as **indices inside entries** | no pointer chasing, no per-node allocation |
| `RingBuffer<T>` | fixed capacity, overwrite mode | same ring as `Queue` with an overwrite flag | — |
| `BitSet` | `set/get/clear/count/and/or/xor` | `std::vector<uint64_t>` + `__builtin_popcountll`, word-parallel operations | 64 flags per word |
| `MultiMap<K,V>` | `get` all, `add`, `has` | flat hash map to inline `SmallVector<V,1>` | one allocation per key, not per pair |

#### B. Reserved

No new approved items in this revision. Add only with lead approval.

#### C. Text, web, and data modules

| Item | Module / API | Internal mechanism (fixed) | Hard rule |
| --- | --- | --- | --- |
| `URL` + `URLSearchParams` | `std/url`, `new` | hand-written RFC 3986 parser over `string_view`, zero-copy slices; reuse `percentDecode` from the network runtime | no regex in the parser |
| `TextEncoder` / `TextDecoder` | `std/text`, new | `simdutf` for UTF-8/UTF-16 validate + convert; hand-written fallback | library internal, API stable |
| `stopwatch` + `console.time` / `console.timeEnd` | `std/time`, new | `QueryPerformanceCounter` / `CLOCK_MONOTONIC`; monotonic only, zero allocation: `const sw = stopwatch(); sw.start(); sw.elapsed(); sw.stop();` | never wall clock |
| `Logger` | `std/log`, new | bundled `fmt`, level filter, synchronous by default, optional async sink over `Channel` | — |
| Checksums | `std/hash`, new; named **checksums, not security** | `crc32`, `crc32c`, `xxhash64`, `fnv1a` via bundled XXHash/CRC32C | documented non-crypto |

#### D. Extensions to existing modules (no new module)

| Module | Additions |
| --- | --- |
| `std/crypto` | `hmacSha256`/`hmacSha512` (libsodium `crypto_auth`), `blake2b` (`crypto_generichash`), `getRandomValues` (`randombytes`), `uuidParse`/`uuidValidate`/`uuidVersion`, `sha256Raw` returning `Buffer` |
| `std/buffer` | `base64Url` variant of the existing codec |

#### E. Hash placement rule (security line, non-negotiable)

- `std/crypto` holds **only** libsodium crypto-safe primitives.
- `std/hash` holds fast **non-crypto** checksums only.
- `Map`/`Set` bucket hash = `wyhash` or `xxhash3` with a **per-process random
  seed from libsodium** (HashDoS defense), never exposed.
- `wyhash`/`rapidhash`/`xxhash` **never** appear under a crypto name. Fast and
  safe in crypto = `blake2b` or HMAC.

#### F. Internal-only libraries (user API unchanged, user sees nothing)

- `ankerl::unordered_dense` backs `Map`/`Set` hot paths; keep the
  insertion-order vector for D44 order.
- `fast_float` for JSON/env/URL number parsing; keep JS number semantics.
- `simdutf` for validation, conversion, and `charAt` walking.
- `miniz` for `std/compress`.
- `moodycamel` or `Boost.Lockfree` for `Channel` and the async log sink.

#### G. Compiler import suggestions

When a source file uses a standard-library name without importing its module,
the compiler must fail with the normal "cannot find name" diagnostic **and**
include the exact import line that fixes it. This applies to default modules
(`console`, `path`, `Math`, ...) and named modules (`fetch`, `createServer`,
...) alike. The LSP may auto-insert the same line, but the command-line
compiler must suggest it too; LSP-only behavior is not enough.

#### H. Standard Library & API implementation targets

Status legend: **Done** = available today, **Partial** = a surface exists but
the listed API is incomplete, **Planned** = not implemented, awaiting lead
selection. Every row follows Rule 0 and the verification rules in §8.

| Module / scope | Feature / API | Implementation notes / details | Status |
| --- | --- | --- | --- |
| Number / Math | `toFixed`, `toPrecision`, `toExponential` | Number formatting methods. | Planned |
| Number / Math | `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `EPSILON` | Standard number constants. | Planned |
| Number / Math | `Number.isInteger`, `Number.isFinite`, `Number.isNaN` | Number validation methods. | Planned |
| Math | Math extras | Add the remaining standard math utilities (inverse trig, hyperbolic, `exp`, `log1p`, `hypot`, `sign`, constants). Today: `PI`, `E`, `random`, `min`, `max`, `pow`, `gcd`, `lcm`, `clamp`, `lerp`, `sqrt`, `abs`, `floor`, `ceil`, `round`, `trunc`, `cbrt`, `log`, `log2`, `log10`, `sin`, `cos`, `tan`. | Partial |
| Array | Callback signature `(value, index, array)` | Standardize every array iteration callback to the JS/TS shape. Today callbacks receive `(value)` only. | Partial |
| Array | `flatMap`, `findLast`, `findLastIndex`, `at` | Modern query, search, and access methods. | Planned |
| Array | `toSorted`, `toReversed` | Non-mutating array transformation methods (return new arrays). | Planned |
| Object | `assign`, `fromEntries`, `is`, `freeze` | Standard object manipulation, creation, and immutability methods. Today: `keys`, `values`, `entries`. | Planned |
| String | `codePointAt`, `fromCodePoint`, `matchAll`, `raw` | Unicode-aware and modern string methods. Today: no `charCodeAt` lowering either (typed, but not emitted). | Planned |
| JSON | `JSON.stringify(value, replacer?, space?)` | Extend the serializer to support pretty printing (for example 2-space indent) and a replacer. Today only a numeric indent is typed and emitted. | Partial |
| Collections | `ConcurrentMap` | Thread-safe map implementation. | Planned |
| Promise | Full Promise API | Complete TS-compatible Promise surface. Today: `resolve`, `reject`, `all`, `race`, `allSettled`, `then`, `catch`, `finally`. Missing: `any`, `withResolvers`. | Partial |
| Global | `structuredClone(obj)` | Deep copy without a worker. Reuse the existing worker deep-copy cloner on the main thread. | Planned |
| `std/process` | `cwd`, `chdir`, `pid`, `ppid`, `uptime`, `memoryUsage`, `hrtime` | Thin OS calls for process information and high-resolution timing. Today: `argv`, `env`, `exit`, `exec`, `spawn`. | Planned |
| `std/network` | HTTP Router (path params & wildcards) | For example `app.get("/users/:id", ...)`. Today `:name` params work; wildcard segments are not exposed. | Partial |
| `std/network` | `dns.lookup(hostname)` | Thin wrapper over the libuv `getaddrinfo` already used by `fetch`. | Planned |
| `std/fs` | `fs.watch(path, callback)` | File-system watcher using libuv `fs_event` (already linked). | Planned |
| `std/os` | `homedir`, `tmpdir`, `freemem`, `totalmem`, `loadavg`, `networkInterfaces` | Thin OS calls for system environment and resource info. Today: `homedir`, `tmpdir`, `hostname`, `platform`, `cpus`, `totalMemory`, `freeMemory` (note the spelling difference from Node's `totalmem`/`freemem`). | Partial |

The A-H ledger above is part of the v0.2.0 scope and ships with the TypeScript
compatibility work. The class/error sections below describe the language
surface; the ledger describes the standard-library additions. Additional
JavaScript APIs found by the compatibility audit are listed in the issue/PR
notes and need lead selection before they are added here.

### 1. Error model

- Add the built-in hierarchy:
  `Error`, `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`,
  `EvalError`, `URIError`, `AggregateError`.
- Every error has `message`, `name`, `stack`, and `cause`.
- `new Error(...)` and the callable form `Error(...)` work.
- The TypeScript/JavaScript options object is supported:
  `new Error("boom", { cause: originalError })` and
  `Error("boom", { cause: originalError })`.
- `AggregateError` accepts its errors iterable plus the same options:
  `new AggregateError(errors, "boom", { cause: originalError })`.
- Error options are ordinary constructor arguments, not runtime metadata:
  `cause` is stored on the error object and is available as `e.cause`.
- **Only `Error` and its subclasses may be thrown.**
  - `throw new Error("boom")` - allowed.
  - `throw new TypeError("bad value")` - allowed.
  - `throw 404`, `throw "missing"`, `throw {}` - compile error.
  - The diagnostic must say that TypeScript allows throwing any value, but
    CP3 does not, because CP3 has one catch model and one runtime contract.
- `catch (e)` binds **`Error` directly**. No narrowing is required:
  `e.message`, `e.name`, `e.stack`, and `e.cause` work.
- Runtime and standard-library failures throw `Error` objects, never raw
  strings, numbers, or `std::exception` values.
- Promise rejections and worker failures use the same `Error` contract.

### 2. TypeScript classes

The target is practical TypeScript class behavior:

- TypeScript method syntax: `greet(): string { ... }` without a `function`
  keyword.
- Constructor parameter properties:
  `constructor(public name: string, private id: number) {}`.
- `extends` with native C++ inheritance, `super(...)` constructor calls, and
  `super.method()`.
- `implements` conformance checks.
- `readonly` enforcement.
- `override` checks.
- `abstract` classes and methods.
- Getters and setters.
- `public`, `private`, and `protected` access checks.
- `static` members.
- `#private` fields and methods.
- `static { ... }` initialization blocks.
- Class expressions: `const C = class { ... }`.
- Zero-overhead `instanceof`.
- TypeScript-compatible constructor and method behavior for the supported
  surface.

Existing CP3-only class syntax (`function greet() {}`) and CP3-only
extensions such as `weak` are **not** kept in v0.2.0: this release moves to
the TypeScript spelling and the TypeScript compatibility target.

### 3. Compile-time decorators

Decorators are compile-time macros, not runtime decorators:

- `@Name` names a `comptime function`.
- The function receives the decorated class or method AST.
- It may validate the declaration and raise compile errors.
- It may inject plain CP3 code: fields, methods, constructor checks, or static
  registration.
- It runs only inside `cp3c`. The emitted binary contains only the injected
  plain CP3 code.
- No runtime metadata tables.
- No `Reflect` / `Reflect.metadata`.
- No wrapper functions around decorated methods.
- No evaluation at class-definition time.
- A decorator that wants per-call behavior (logging, timing) must inject
  statements into the method body at compile time.
- Replacing a method with an indirect-call wrapper is a compile error.

### 4. Compile-time `extends`

- `extends Name` keeps native C++ inheritance and has zero runtime cost.
- `extends expr` is accepted only when `expr` is comptime-evaluable:
  constants, `comptime` calls, and conditionals over comptime values.
- The compiler folds the expression to a concrete class and emits ordinary
  inheritance.
- Any other expression is a compile error:
  `extends expression must resolve at compile time`.
- No runtime base-class lookup, hidden class objects, or dictionary objects.

### 5. Zero-overhead `instanceof`

- `x instanceof ClassName` lowers to a static type-id comparison.
- There is one static id per class, decided by the compile-time hierarchy.
- No additional runtime type-info tables.
- `catch (e)` does not need `instanceof`; the binding is already `Error`.

### 6. Imports and packages

Known defects fixed in v0.2.0:

- Cross-module exported values and types must propagate into the importing
  unit, so type checking and code generation use the real declaration.
- Package imports must resolve and link, not fall through to codegen as an
  unknown ambient name.
- `.cp3pak` archives must record a `main` path that actually exists inside the
  archive (`index.cp3`, not `src/index.cp3`).
- `.cp3/lock.json` must store project-relative `resolved` and `target` paths,
  not machine-specific absolute paths.
- A `.cp3pak` must carry every native binding its implementation imports:
  `native.json`, the declaration index, headers, import libraries, runtime
  DLLs and the recorded source, relocated under `.cp3/native/<id>/` with the
  metadata rewritten to those self-contained paths. Installing the archive
  hoists the binding to the consuming project's `.cp3/native/<id>/`; a
  precompiled package object and `cp3 recompile` both compile against the
  embedded include roots. A separate platform asset is now only the fallback
  for beta-one archives that shipped source-only.

### 7. TypeScript-only diagnostics

Whenever CP3 rejects something that TypeScript accepts, the diagnostic must
say all three things:

1. what the TypeScript feature is,
2. that TypeScript allows it but CP3 does not, and
3. why CP3 rejects it (static native output, no runtime metadata, no hidden
   wrappers, no dynamic class resolution, and so on).

Examples:

- Throwing a number or string: TypeScript permits any thrown value; CP3 only
  permits `Error` objects.
- A runtime decorator: TypeScript can evaluate decorators at runtime; CP3
  decorators are compile-time macros only, so the binary stays metadata-free.
- A dynamic `extends` expression: TypeScript can resolve a base class at
  runtime; CP3 requires a compile-time class so inheritance stays native C++.
- A TypeScript type-level keyword such as `keyof`, `infer`, `satisfies`,
  `infer`, or `asserts`: TypeScript computes types at compile time; CP3's type
  surface is the static native subset and rejects it with the reason.
- Other TypeScript-only type features (conditional types, mapped types,
  template-literal types, indexed-access types, `unique symbol`, variance
  annotations, `const` type parameters, `namespace`/`module` declarations):
  rejected with the same explicit explanation.

The rule is: never emit a generic "syntax error" when the construct is a known
TypeScript feature that CP3 intentionally does not support.

### 8. Verification

- Generate C++ for a decorated class and a comptime-`extends` class.
- Diff against hand-written equivalent classes.
- Output must be identical except for the injected plain CP3 code.
- Confirm that generics monomorphisation, interface dispatch, and object-churn
  behavior do not regress against the v0.2 option-1 baseline.
- Run the full compiler and runtime test suites.

### 9. Out of scope for v0.2.0

These are rejected with the diagnostic rule above:

- TypeScript runtime decorators.
- `Reflect.metadata`.
- Runtime-dynamic `extends`.
- Any feature that needs runtime type metadata or wrapper functions to work.

## v0.3.0 - Performance upgrades

Status: **coming soon**

The v0.3 line is reserved for performance work. It will start after the v0.2.0
TypeScript compatibility surface is complete and verified. The plan will be
published here before implementation starts.

Areas under consideration:

- Faster compilation for large module graphs.
- Better generated-code throughput and lower allocation churn.
- Improved monomorphisation and interface-dispatch code generation.
- Further escape-analysis and stack-allocation wins.
- Lower-overhead class construction and object layout.
- Faster startup and smaller native binaries.
- More performance regression coverage in the test suite.
