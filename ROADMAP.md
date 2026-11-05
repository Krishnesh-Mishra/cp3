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
| `LRUCache<K,V>` (Done) | `new LRUCache<K,V>(capacity)`, `get/set/has/delete/clear/size/capacity/keys` | flat hash map + intrusive doubly-linked list stored as **indices inside entries** | no pointer chasing, no per-node allocation |
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
| Number / Math | `toFixed`, `toPrecision`, `toExponential` | Number formatting methods. | Done |
| Number / Math | `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `EPSILON` | Standard number constants, plus `MAX_VALUE`, `MIN_VALUE`, `POSITIVE_INFINITY`, `NEGATIVE_INFINITY` and `NaN`. | Done |
| Number / Math | `Number.isInteger`, `Number.isFinite`, `Number.isNaN` | Number validation methods. | Done |
| Math | Math extras | Add the remaining standard math utilities (inverse trig, hyperbolic, `exp`, `log1p`, `hypot`, `sign`, constants). Today: `PI`, `E`, `LN2`, `LN10`, `LOG2E`, `LOG10E`, `SQRT1_2`, `SQRT2`, `random`, `min`, `max`, `pow`, `gcd`, `lcm`, `clamp`, `lerp`, `sqrt`, `abs`, `floor`, `ceil`, `round`, `trunc`, `cbrt`, `log`, `log2`, `log10`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `exp`, `log1p`, `hypot`, `sign`. | Done |
| Array | Callback signature `(value, index, array)` | Standardize every array iteration callback to the JS/TS shape. Callbacks may declare any prefix of `(value, index, array)`. | Done |
| Array | `flatMap`, `findLast`, `findLastIndex`, `at` | Modern query, search, and access methods. | Done |
| Array | `toSorted`, `toReversed` | Non-mutating array transformation methods (return new arrays). | Done |
| Object | `assign`, `is`, `freeze` | Standard object manipulation, creation, and immutability methods. Today: `keys`, `values`, `entries`, `assign`, `is`, `freeze`. | Partial |
| String | `codePointAt`, `fromCodePoint`, `matchAll`, `raw` | Unicode-aware and modern string methods. | Planned |
| JSON | `JSON.stringify(value, replacer?, space?)` | Extend the serializer to support pretty printing (for example 2-space indent) and a replacer. Today only a numeric indent is typed and emitted. | Partial |
| Collections | `ConcurrentMap` | Thread-safe map implementation. | Planned |
| Promise | Full Promise API | Complete TS-compatible Promise surface. Today: `resolve`, `reject`, `all`, `race`, `allSettled`, `any`, `withResolvers`, `then`, `catch`, `finally`. | Done |
| Global | `structuredClone(obj)` | Deep copy without a worker. Reuse the existing worker deep-copy cloner on the main thread. | Done |
| Global | `parseTo(value, Type)` | Typed scalar conversion between `string`, `number`, `boolean` and the native numeric types, in both directions (for example `parseTo("123", number)`, `parseTo(n, string)`, `parseTo<r_'i32'>(i64Value)`). The target is a static type keyword or type argument, never a value. A literal source is checked at compile time (CP3401 when the target cannot represent it); a variable source is converted by the runtime coercion helpers. | Done (native targets take the type-argument form `parseTo<r_'i32'>("777")` since `r_'i32'` is type syntax, not a value) |
| Standard library | Exact integer widths | One semantic-width table (`cp3/host/std_integer.hpp`): HTTP status/ports `u16`, pids/exit codes `i32`, timestamps `i64`, file sizes `u64`, lengths/indexes/counts `usize`. A `typescript` project keeps `number`; `rust`/`cpp` projects get the exact native width, and generated docs use the project spelling. Roll out module by module: Math, network, fs, process, os, date, collections. | In progress (table + Math + network + fs + process + os) |
| Native types | Checked integer arithmetic and explicit conversions | `+ - * / %`, unary `-`, `++`/`--` and arithmetic compound writes throw on overflow, division by zero and `INT_MIN / -1`; signed/unsigned mixing is rejected; `as` and `parseTo` narrow with runtime range checks while widening stays implicit. Tests: `tests/cases/types/03/native_checked`. | Done |
| Native types | Exact 64-bit literals | The literal source spelling is kept through checking and codegen, so `9223372036854775807`, `-9223372036854775808` and `18446744073709551615` initialize `i64`/`u64` without an intermediate `number` rounding step. Tests: `tests/cases/types/03/native_checked/narrowing` and `tostring`. | Done |
| Native types | `.toString()` and `wrappingMul` | Every supported native numeric width (Rust and C++ aliases, including `char`, `long`, `unsigned long` and `f32`) formats exactly via `.toString()`; `value.wrappingMul(other)` opts into modulo-2^N multiplication for FNV-style hashing while checked `*` remains the default. Tests: `native_checked/tostring`, `native_checked/wrapping_mul`, `native_project_cpp/tostring`. | Done |
| String | `String.fromCharCode` | Byte-oriented inverse of `charCodeAt`: each code is reduced modulo 256 and appended as one raw UTF-8 byte; no arguments yields the empty string. Tests: `tests/cases/arrays-strings/02/str/fromcharcode`. | Done |
| `std/process` | `cwd`, `chdir`, `pid`, `ppid`, `uptime`, `memoryUsage`, `hrtime` | Thin OS calls for process information and high-resolution timing. Today: `argv`, `env`, `exit`, `exec`, `spawn`, `cwd`, `chdir`, `pid`, `ppid`, `uptime`, `memoryUsage` (rss real; heap fields `0`), `hrtime`, via vendored libuv. | Done |
| `std/network` | HTTP Router (path params & wildcards) | For example `app.get("/users/:id", ...)`. Today `:name` params work; wildcard segments are not exposed. | Partial |
| `std/network` | `dns.lookup(hostname)` | Thin wrapper over the libuv `getaddrinfo` already used by `fetch`. | Done (`network.lookup` and the named `lookup` import; resolves the first address as text) |
| `std/fs` | `fs.watch(path, callback)` | File-system watcher using libuv `fs_event` (already linked). | Planned |
| `std/os` | `homedir`, `tmpdir`, `freemem`, `totalmem`, `loadavg`, `networkInterfaces` | Thin OS calls for system environment and resource info. Today: `platform`, `homedir`, `tmpdir`, `hostname`, `cpus`, `totalMemory`, `freeMemory`, `totalmem`, `freemem` (Node names), `loadavg` (1/5/15 min tuple), `networkInterfaces` (`Array<[name, address, family, internal]>`), via vendored libuv. | Done |
| `std/io` | `waitReadable`, `waitWritable`, `pollUntilDone`, `POLL_*` | Shared-`EventLoop` fd readiness for libraries with a socket and a non-blocking step API. Phase 1 is the two wait primitives; phase 2 is the shared helper. Phase 3 (`import native async`) is deferred. | Done |

#### J. Editor and VS Code extension

| Item | Module / command | Implementation notes | Status |
| --- | --- | --- | --- |
| `cp3.run` | `cp3.run` in `cp3-vscode` | A command contributed by the VS Code extension, surfaced as a **run button in the editor title bar** (`editor/title`) when `editorLangId == cp3`. Clicking it saves the active document, resolves `cp3.compilerPath`, runs `<cp3c> run <file>`, streams stdout/stderr into the `cp3 Run` output channel (no terminal panel is opened), and reports non-zero exits with a `Show Output` notification. The command is also in the command palette and the editor context menu. | Done |

#### K. Type-system cleanup (remove `void` and `unknown`)

**Rule: v0.2 has exactly two nullish types, `null` and `undefined`. The `void` and `unknown` keywords are removed from the language surface.**

| Item | Rule | Notes / migration |
| --- | --- | --- |
| `void` type | Removed. A function with no `return` infers `undefined` as its return type. | Every `: void` annotation is a compile error whose diagnostic names the fix (`undefined`); TypeScript permits `void`, CP3 does not. A trivial `fmt`-style rewrite updates existing source. |
| `unknown` type | Removed. `JSON.parse` requires a type argument: `JSON.parse<T>(text)`. | Omitting `T` is a compile error. The internal permissive callback placeholder used by the compiler is not a user type and is never printed as `unknown`; std members receive precise types. |
| `null`, `undefined` | Kept as the only two nullish primitives. | `===`/`!==` flavor rules, `??` and `== null` semantics are unchanged. |
| Migration & verification | Corpus, spec, docs and diagnostics referencing `void`/`unknown` are updated; the corpus is grep-rewritten (`: void` → `undefined`, bare `JSON.parse(` → `JSON.parse<T>(`). | Diagnostics explain why (static native output; `catch` binds `Error`; `unknown` forced narrowings) and what to write; llm.md and `docs/content/01-language/01-variables-and-bindings/02-types.md` are updated in the same change. |

**Status: Planned** (not started; spec/corpus-wide breaking change, scheduled with the v0.2 audit).

#### L. JavaScript/TypeScript compatibility backlog (v0.2, highest value first)

The remaining JavaScript and TypeScript surface needed so ordinary application code compiles unchanged against CP3. Ordered highest value first; every row follows Rule 0 and the verification rules in §8. Nothing here is automatic: implementation is feature-by-feature, with tests and a docs update in the same commit.

| Feature | Why | Size | Status |
| --- | --- | --- | --- |
| Array/Object/String catalog gaps: `reduce`, `reduceRight`, `splice`, `concat`, `reverse`, `lastIndexOf`, `keys`/`values`/`entries`, `Array.from`/`isArray`/`of`, `Object.fromEntries`, `parseInt` | The `llm.md` catalog does not list them; `reduce`, `splice` and `concat` are everywhere in TS code, so the TS-compatibility theme fails without them. | Small | In progress (all done except `Object.fromEntries`, blocked on `Record<string,V>`) |
| Timers, if missing: `setTimeout`, `setInterval`, `clearTimeout`, `sleep` | Every service waits; a 100 ms timer regression test needs them. | Small | Done |
| Process lifecycle: `process.on("SIGINT" \| "SIGTERM" \| "exit" \| "unhandledRejection")`, `server.close()` | Servers need graceful shutdown; libuv signal handles are already available. | Small | Planned |
| Built-in utility types: `Partial`, `Required`, `Readonly`, `Pick`, `Omit`, `Record` | Very common in real TS. The compiler resolves them natively instead of user-written mapped types. | Medium | Planned |
| Date gaps: `getTime`, `getDay`, the UTC getters, setters, `Date.parse` | The current surface is very thin. | Small | Done |
| `cp3 install <dir>` semantics | Compile the source or reject with a clear "use a `.cp3pak`" error; accept classes via a package's default or named exports (today CP3401 blocks it); make `cp3 test` work for packages. | Small-medium | Planned |
| Coercion: `parseInt`, `parseFloat`, `Number(x)`, `String(x)`, `Boolean(x)`, `isNaN`, `isFinite` | Basic coercion in every TS codebase. | Small | Done |
| Number helpers: `Number.toString(radix)`, `Number.isSafeInteger`, `Number.parseInt`/`parseFloat` | Common helpers plus hex/binary output. | Small | Done |
| `encodeURIComponent`, `decodeURIComponent`, `encodeURI`, `decodeURI`, `atob`, `btoa` | URL building and Basic auth headers; the network runtime already carries a `percentDecode` helper to adapt. | Small | Done |
| Scheduling primitives: `queueMicrotask`, `setImmediate`, `process.nextTick` | What libraries expect alongside timers. | Small | Done |
| Generic constraints and defaults: `<T extends X = Y>` | Required for real generic APIs. | Medium | Planned |
| `const enum` and enum reverse mapping | Common enum use. | Small | Planned |
| Map/Set constructors from iterables and spread (`[...set]`, `new Map(entries)`) | Everyday code. | Small | Done |
| Set algebra: `union`, `intersection`, `difference`, `isSubsetOf` | ES2025 methods; simple on the flat hash set. | Small | Done |
| `Object.groupBy` / `Map.groupBy` | Common in reporting code. | Small | In progress (`Map.groupBy` done; `Object.groupBy` returns an object with runtime keys and waits on `Record`) |
| `toSpliced`, `with`, `flat(depth)`, `copyWithin` | Rest of the ES2023 non-mutating array methods. | Small | Done |
| `process.stdout.write` / `process.stderr.write` | No newline; CLIs and progress output need it. | Small | Done |
| `process.stdin` (read all, read lines) or `std/readline` | Piping input into CLIs; `console.input` covers prompts only. | Small-medium | Planned |
| `process.exitCode`, `process.platform`, `process.arch`, `process.version` | Basic process info and a graceful exit path. | Small | Done |
| `console.table`, `console.group`/`groupEnd`, `console.dir`, `console.count`, `console.trace` | Common debug output; `console.time`/`timeEnd` are already covered by the `std/time` ledger entry. | Small | Done |
| `Error.captureStackTrace`, `error.toString()`, `error.name` overrides on subclasses | Custom error classes need them. | Small | Done |
| `res.redirect`, `res.sendFile`, `res.cookie`, `req.cookies`, `req.ip` | Basic web-server features every app writes by hand today. | Small-medium | Done |
| Built-in middleware: `static(dir)`, `cors(options)`, `bodyLimit(n)` | Production servers need these first; body limits are also a DoS defense. | Medium | Planned |
| Error-handling hook: `server.onError((err, req, res) => ...)` | An uncaught handler throw returns 500 instead of crashing or hanging. | Small | Planned |
| `Headers`, `Request`, `Response`, `FormData` classes and `fetch(Request)` | Standard fetch shapes TS code expects. | Medium | Planned |
| `AbortSignal.timeout(ms)` on `fetch` | Complements the existing `timeout` init option. | Small | Planned |
| All other Express-compatible features which are doable (static file serving, redirect/sendFile, cookies, CORS, body limits, error hooks) | The set of Express patterns users actually write; implement those that carry no runtime metadata or wrapper machinery. | Medium | Planned |

#### M. Nullable class types and narrowing

| Feature / API | Why | Size | Status |
| --- | --- | --- | --- |
| `T \| null`, `T \| undefined`, optional fields (`next?: T`) on class types | Linked lists, trees and graphs cannot be written as ordinary TypeScript without them (sentinel arrays are slower and unidiomatic). Lowers to a nullable `Ref<T>` (null = `nullptr`), no extra allocation. | Small | Done |
| Control-flow narrowing: `!== null`, `=== null`, truthiness, `&&`, `\|\|`, `??`, `?.`, early return | Makes `n.next.value` inside `if (n.next !== null)` type-check; assignment to the narrowed target invalidates it. | Small | Done |
| Non-null assertion `x!`, definite-assignment `x!: T`, `as` assertions (class types and native numerics only) | Everyday TS code; `x!` throws a catchable `Error` on null. Unrelated `as` types are CP3401. | Small | Done |

#### I. Non-blocking native I/O (v0.2 phases 1-2)
`async` only schedules continuations on the one event loop. It does not make a
blocking C call concurrent. v0.2 therefore separates two mechanisms and ships
only the socket/reactor path first:

| Library has... | Package author uses |
| --- | --- |
| A socket fd and a non-blocking step API | `std/io` `pollUntilDone` (phases 1-2) |
| Neither (pure blocking C) | `import native async "<lib>"` (phase 3, shared pool) |
| Neither, and calls take seconds | a larger/separate pool, or another library |

Networking libraries that can be non-blocking MUST use the socket path, never
the pool. Reference socket-path libraries: libpq (`PQconnectPoll`,
`PQconsumeInput`, `PQisBusy`, `PQsocket`), MySQL/MariaDB non-blocking APIs plus
`mysql_get_socket`, hiredis async mode or a Redis protocol client written in
cp3, libcurl (`curl_multi_socket_action`), and Kafka/NATS clients exposing a
pollable fd. Verify the exact library API when writing each shim. Reference
pool-path libraries: sqlite, bcrypt, zlib, image libraries, `getaddrinfo`/DNS,
and regular-file reads (epoll cannot watch regular files).

Phase 1 (reactor primitive):

```ts
import io from "std/io";

await io.waitReadable(fd); // Promise<void>
await io.waitWritable(fd); // Promise<void>
```

- One waiter suspends only its async function; timers, microtasks and other fd
  waiters keep running.
- Many waiters are supported, including several on the same fd with different
  directions.
- The promise rejects on poll error, hangup/disconnect, invalid descriptor, or
  a descriptor closed while the wait is registered.
- Phase 1 has no cancellation token. A waiter stays pending until it settles or
  the descriptor fails; cancellation only becomes a public surface when the
  abort/cancellation model is specified.

Phase 2 (shared helper):

```ts
await io.pollUntilDone(
  () => pqsocket(conn),
  () => pqProgress(conn), // io.POLL_READING | io.POLL_WRITING | io.POLL_DONE | io.POLL_FAILED
);
```

`pollUntilDone` calls `getSocket()` again on every iteration because the fd can
change during connect. `POLL_DONE` resolves, `POLL_FAILED` rejects,
`POLL_READING` awaits `waitReadable`, and `POLL_WRITING` awaits `waitWritable`.
It is implemented once in the runtime; packages do not write their own loop.

Poller and wakeup (required platform statement):

- One libuv loop per thread, shared by `std/network` and `std/io` through
  `cp3/async/uv_loop.hpp`.
- Linux: libuv uses epoll; cross-thread wakeup is libuv's async handle
  (eventfd/pipe internally).
- Windows: libuv uses IOCP; cross-thread wakeup is libuv's async handle
  (PostQueuedCompletionStatus internally).
- The runtime `EventLoop` remains authoritative: it drives libuv from its idle
  phase and keeps the existing timer/exit contract.

Phase 3 (deferred until phases 1-2 pass their tests): `import native async
"<lib>" as x;` runs blocking calls on a fixed shared pool (default 4 threads),
not one pool per library. Required rules before phase 3 starts:

1. Fixed shared pool, configurable, shared by all libraries.
2. Per-handle serialization: calls on one handle never overlap; different
   handles may run in parallel.
3. Copy strings/buffers into owned memory before dispatch.
4. Pool threads never touch the cp3 runtime; they write plain result structs.
5. Completion wakes the idle loop immediately.
6. Thread-pinned handles are opt-in for libraries that require one thread.

Known phase-3 limits, to document rather than hide: pool size caps concurrency
(the (N+1)th blocking call queues), and a started blocking call cannot be
cancelled; cancellation only discards the result.

Phase 1-2 acceptance tests:

1. A 100 ms timer fires roughly 20 times while a socket wait resolves after
   about 2 seconds.
2. 1000 concurrent waiters on distinct sockets resolve correctly with no
   thread growth.
3. Closing the descriptor while waiting rejects, with no hang or crash.
4. Windows and Linux use the pollers/wakeups stated above.

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

Existing CP3-only class syntax (`function greet() {}`) is **not** kept in
v0.2.0: this release moves to the TypeScript spelling and the TypeScript
compatibility target. The manual `weak` field modifier is retained as an
escape hatch for reference cycles: a weak field does not count toward the
refcount and reads as `T | null`.

Automatic cycle handling (memory item), no GC and no cycle collector:
- Stage 0 (`tools/cycle_stats.py`, `docs/cycle-stage0.md`): 61 classes, 5
  cycle-capable in 3 groups (A/B mutual, BST self-recursive, Child/Parent
  already weak), 56 acyclic (92%). 2 of 3 groups have no `weak` edge.
- Stage 1 + shared analysis: strong-edge class graph (weak excluded,
  unknown fail-closed) published as `SemaResult::cycleCapableClasses` with
  per-field store/escape sites in `cycleEscapeSites`.
- Step 4 diagnostics: a cycle through two or more distinct edges (SCC > 1)
  with no `weak` edge is CP3401 naming the cycle path and asking to mark one
  edge `weak`. Single self-referencing fields (`next: Node | null`) are
  exempt (recursive types, not runtime cycles).
- `cp3 check --report` lists cycle-capable classes, store/escape sites and
  the manual-weak share. `tools/fuzz_graphs.py` differential fuzzer is clean.
- Regions, compiler-injected non-owning edges and group counting (stages
  2-3) are deferred: they need the whole-program ownership proof plus the
  fuzzer/ASan gate, which is not yet met. Cycles resolve today by an
  explicit `weak` edge.

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
TypeScript compatibility surface is complete and verified. The list below is a
candidate backlog, not a commitment: every item will be evaluated, measured and
ordered before implementation begins. No v0.3 optimization is implemented as
part of v0.2.

Program-level goals:

- Faster compilation for large module graphs.
- Better generated-code throughput and lower allocation churn.
- Improved monomorphisation and interface-dispatch code generation.
- Further escape-analysis and stack-allocation wins.
- Lower-overhead class construction and object layout.
- Faster startup and smaller native binaries.
- More performance regression coverage in the test suite.

Candidate optimization backlog:

| Optimization | What CP3 would do | Expected gain |
| --- | --- | --- |
| Escape analysis | If an object never leaves a function, put it on the stack instead of `Ref<T>` on the heap. | Removes allocation and refcounting. |
| Ownership/move analysis | If a value has one owner, emit `std::move` and skip the refcount. | Removes atomic increments, a major cost. |
| Integer inference | TypeScript `number` is a double; if the range is provably small, emit `int32_t`. | Faster math, vectorizable code. |
| Bounds-check elimination | Prove that `for (i = 0; i < arr.length; i++)` needs no check inside the loop. | Removes a branch per array access. |
| Closed-world devirtualization | Since every class is visible, avoid virtual calls where only one implementation exists. | Enables inlining. |
| Emit `__restrict`, `const`, `noexcept` | TypeScript objects do not alias like raw pointers, so tell C++ the stronger aliasing and purity contracts. | Lets Clang vectorize more. |
| Compile-time evaluation | Fold constants, evaluate `constexpr` configuration and precompute tables. | Zero runtime cost. |
| Strings as views | Lower `slice`/`substring` to `string_view` when the source outlives the slice. | Removes copies. |
| Arrays of numbers stored flat | Store `number[]` as `std::vector<int32_t>` rather than boxed elements. | Cache-friendly layout. |
| Async without heap | If a promise never escapes, keep the state machine on the stack; fuse sequential awaits. | Cheaper async. |
| Per-request arena | In servers, allocate everything for a request from one arena and free it at once. | No per-object free. |
| Dead code removal | Remove unused functions, branches and fields across the whole project. | Smaller, faster binary. |
| Small string optimization | Store short strings inline in the object. | Removes `malloc` for most strings. |
| Interned string literals | Share one copy of each literal; equality can become a pointer compare. | Faster compares, less memory. |
| Compile-time string switch | Lower `switch` on literals to a length check plus a hash/perfect-hash jump. | Removes chains of string compares. |
| Perfect hashing for fixed keys | Objects with known keys become structs, not hash maps. | Field access is one load. |
| Struct-of-arrays layout | Store hot fields of `User[]` in separate arrays when they are scanned often. | Cache-friendly, vectorizable. |
| Hidden-class elimination | Closed-world types become fixed structs, with no dynamic property lookup. | No lookup cost. |
| Field reordering and packing | Sort struct fields by size and pack `bool`s together. | Smaller objects, fewer cache misses. |
| Non-atomic refcount | Use plain increments when the value is proven to stay on one thread. | Removes atomic cost. |
| Refcount elision on borrows | Pass an object to a non-storing function as `const T&`. | No increment/decrement per call. |
| Last-use move | At a variable's final use, emit `std::move`. | Removes copies and refcount traffic. |
| Return value optimization | Build results directly in the caller's slot. | No copy on return. |
| Scalar replacement | Split short-lived objects into plain locals in registers. | The object disappears entirely. |
| Closure-to-lambda inlining | Non-escaping callbacks become templated lambdas, not `std::function`. | Inlined, no indirect call. |
| Monomorphization of generics | Emit one specialized copy per concrete type, with no boxing. | Native speed for generic code. |
| Map/filter/reduce fusion | Turn `a.map().filter().reduce()` into one loop with no temporary arrays. | Removes intermediate allocations. |
| `for...of` to indexed loop | Lower array iterators to plain pointer loops. | Vectorizable code. |
| Loop-invariant hoisting | Move `arr.length`, property reads and pure calls out of loops. | Fewer loads. |
| Array pre-sizing | Reserve capacity when the final size is known, such as `map` or fixed pushes. | No reallocation copies. |
| Typed arrays as raw buffers | Map `Uint8Array` and `Float64Array` to contiguous memory with SIMD-friendly code. | Fast numeric and binary work. |
| Purity analysis | Mark functions with no side effects, enabling caching, hoisting and reordering. | Enables other passes. |
| Exception-free fast path | Mark provably non-throwing functions `noexcept`; use result codes internally. | Smaller, faster code. |
| Cold-path marking | Add `[[unlikely]]` and `[[gnu::cold]]` to error and throw branches. | Better branch layout. |
| Hot/cold function splitting | Move rare code out of hot functions. | Better instruction-cache use. |
| Tail-call and recursion to loop | Turn self-recursive tail calls into loops. | No stack growth. |
| Inline-caching removal | Remove runtime type checks where types are statically known. | No dynamic dispatch. |
| Async fusion | Sequential awaits on already-ready values skip the scheduler. | Fewer context switches. |
| `Promise.all` without heap | Use an inline array for fixed-size `Promise.all([a, b, c])`. | No allocation. |
| Allocation-free hot paths | Report allocations inside loops, then hoist or pool them. | Predictable latency. |
| Object pooling | Reuse frequently created and destroyed objects. | Skips `malloc` and `free`. |
| Bump allocator for startup data | Allocate program-lifetime data from a bump arena. | Faster allocation, no free. |
| Slab allocators per type | Allocate same-size objects from fixed pools. | Less fragmentation, faster allocation. |
| Static initialization at compile time | Build module-level constants and tables in the compiler, not at startup. | Zero startup cost. |
| Lookup-table generation | Replace pure computation over a small domain with a precomputed table. | Constant-time math. |
| Division by constant | Lower `x / 10` and `x % 8` to multiply and shift. | Much cheaper than division. |
| Integer-overflow proofs | Prove a value cannot exceed 2^53, then use `int64` instead of `double`. | Integer speed. |
| Branchless codegen | Lower simple ternaries, `min` and `max` to conditional moves. | No branch misprediction. |
| SIMD intrinsics for known patterns | Use AVX2/NEON for `indexOf`, `includes`, `sum`, `fill` and `copy` on typed data. | 4x to 16x on bulk operations. |
| Fast JSON | Compile typed `JSON.parse<T>` and `JSON.stringify` into schema-specific code. | Several times faster than generic JSON. |
| Compile-time regex | Compile regex literals into a state machine at build time. | No regex setup at runtime. |
| Compile-time validation | Compile Zod-like schemas into direct field checks. | No schema interpretation. |
| Compile-time routing | Compile HTTP routes into a trie or perfect hash. | Constant-time dispatch. |
| Compile-time SQL checks and prepared statements | Parse and prepare literal SQL once, with typed result structs. | Less runtime parsing. |
| Zero-copy parsing | Keep HTTP and JSON fields as views into the receive buffer. | No string copies. |
| Buffer reuse for I/O | Reuse read and write buffers per connection. | No allocation per request. |
| Batched syscalls | Use `writev`, `sendmmsg`, `io_uring` or IOCP batching where supported. | Fewer kernel transitions. |
| Pipelined DB queries | Use libpq pipeline mode for multiple queries per round trip. | Much lower latency. |
| Connection and statement caching | Cache prepared statements per connection automatically. | Skips repeated planning. |
| Tree shaking across modules | Drop unused exports, classes and methods across the whole program. | Smaller binary, better cache use. |
| Cross-module inlining | Use whole-program visibility and one translation unit where practical. | Inlining across files. |
| Identical code folding | Merge duplicate functions with `/OPT:ICF` or `--icf=all`. | Smaller binary. |
| Link-time layout | Order functions by call frequency. | Fewer instruction-cache misses. |
| Compiler flags | Evaluate `-O3`, `-march` per target, `-fno-plt`, `-fvisibility=hidden` and `-fno-semantic-interposition`. | Free speed. |
| LTO and ThinLTO | Optimize across all object files. | 5% to 15% typical gain. |
| PGO and BOLT | Profile, then rebuild and relayout using real hot paths. | 10% to 30% typical gain. |
| Fast allocator default | Replace system `malloc` with mimalloc or snmalloc. | Faster allocation, less contention. |
| Flat containers | Use flat hash maps and small vectors instead of `std::unordered_map` and `std::vector`. | Cache-friendly. |
| Small-vector optimization | Keep usually-tiny arrays inline. | No heap for small arrays. |
| Prefetch hints | Insert prefetch in list and tree traversals. | Hides memory latency. |
| False-sharing avoidance | Align per-thread data to cache lines. | Better multithreaded scaling. |
| Thread-local state | Use per-thread caches and counters, merged later. | No locks on hot paths. |
| Lock-free queues for the offload pool | Use MPSC ring buffers for completions. | Lower pool overhead. |
| Sanitized debug builds | Run ASan and UBSan in debug mode; strip them in release mode. | Catches unsafe analysis bugs. |
| Optimization report | `cp3 build --report` lists allocations, refcounts and bounds checks left in each function. | Shows where to fix next. |
| Benchmark regression gate | Fail CI when a benchmark gets slower. | Keeps gains from eroding. |

The per-request arena item appears once here; it belongs to the memory-allocation
workstream and will be evaluated together with object pooling, bump allocation
and slab allocation.

Additional deduplicated candidates:

| Optimization | Borrowed from | What CP3 does internally | Gain |
| --- | --- | --- | --- |
| Region inference | Goose, MLKit, Cyclone | Infer each allocation's lifetime (scope, caller, request, program) and allocate it in the matching bump region. | Allocation is a pointer bump; free is one store. |
| Multiple data stacks | Goose | Assign growable arrays and strings to separate bump stacks, with at most one growing value on top of each. | Growth never moves memory; no capacity checks. |
| Inline nested layout | Goose, Cap'n Proto | Store an array of strings or objects as one contiguous block, with small strings inline. | Fewer cache misses and mallocs. |
| Variable-size union layout | Goose | Each tagged-union value takes the size of its own variant. | Less memory, denser arrays. |
| Narrow in-region links | Goose, Cap'n Proto | Use 1-, 2- or 4-byte offsets between objects in the same region instead of 8-byte pointers. | Smaller nodes, better cache use. |
| Stable element addresses | Goose | Arrays on bump stacks never move on growth, so cached element pointers stay valid. | Removes re-lookups and reserve workarounds. |
| RC elision and pairing | Swift OSSA, Lobster | Remove retain/release pairs that cancel inside a function. | Fewer refcount operations. |
| Deferred / batched release | Nim ORC, Go GC ideas | Collect releases and free them at a safe point, such as end of request. | Smoother latency, cheaper frees. |
| Cycle detection only where possible | Nim ORC, Python | Run cycle analysis only for types that can form cycles, decided at compile time. | No cycle-collector cost for most types. |
| Value types by default | Swift, Go, C# structs | Small immutable-shaped objects become stack structs, not `Ref<T>`. | No allocation, no refcount. |
| Copy-on-write arrays and strings | Swift, PHP | Share the buffer on copy and copy only on first write. | Cheap passing and assignment. |
| Unique-ownership fast path for COW | Swift | If the refcount is 1, mutate in place with no check. | Fast mutation of owned data. |
| Rope or builder for string concatenation | Go builder, JVM StringBuilder | Turn `a + b + c` chains and loop appends into one builder with one allocation. | Removes quadratic copying. |
| Dictionary of keys-as-enum | Zig comptime | Maps with literal-only keys become indexed arrays or structs. | No hashing. |
| Auto-vectorization help | Mojo, ISPC | Emit loops in a shape clang can vectorize, with alignment and no-alias hints. | 4x to 16x on numeric loops. |
| Zero-cost-when-unused exceptions | C++ Itanium ABI | Table-based unwinding only for functions that can throw. | No cost on the happy path. |
| Thread-local allocation caches | tcmalloc, mimalloc | Per-thread free lists, no locks. | Scales across threads. |
| Share-nothing thread handoff | Goose, Erlang, Go channels | Copy flat values between the loop and pool, no shared mutable state. | No locks, no data races. |
| Tiered layout choice per allocation site | JVM tiered JIT, adapted ahead of time | Pick stack, region, in-place, copy or heap by a cost model using size and call counts. | Best choice per site. |
| Specialization on constant arguments | Partial evaluation, LLVM IPSCCP | Clone functions for common constant arguments, such as a fixed format string. | Removes branches. |
| Fast hash by key type | Swiss tables, Rust hashbrown | Integer keys use cheap hashes, short strings use fast hashes, small maps use linear search. | Faster maps. |

Rows already represented above are not repeated: in-place construction (return
value optimization), compile-time ownership inference (ownership/move analysis),
borrow inference (refcount elision on borrows), non-atomic RC by default,
last-use destruction (last-use move), string views, symbol interning,
shape-based objects, whole-program devirtualization, type-based alias hints,
interval/range analysis, loop fusion, stackless async state machines, promise
fusion, result-code error paths, slab/size-class allocators, escape-free closure
inlining, profile-guided layout, perfect hashing for known string sets,
small-collection inline storage, cache-line-aware layout, prefetch insertion,
debug-only soundness checks, the per-function optimization report and the
benchmark regression gate.
