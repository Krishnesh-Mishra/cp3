---
title: JavaScript and TypeScript developers
chapter: Welcome
---
This is a practical reference for developers who already know JavaScript or TypeScript. It covers the command line, project setup, CP3 source syntax, the standard library, async and concurrency, native types, diagnostics, and a JavaScript/TypeScript → CP3 porting map. Every API entry names its import form, signature, and behavior. Examples use syntax that type-checks in CP3.

## cp3 0.2 highlights

What landed in the v0.2 line that changes everyday code:

- **Checked JSON everywhere** — `JSON.parse<T>` requires the type argument and validates the value at runtime; `T` may be an inline object annotation (`{ x: number }`) as well as a registered interface. The file-oriented family (`JSON.parseFileSync<T>` / `parseFile<T>` / `stringifyToFile(Sync)` / `verify<T>`) reads, writes and validates JSON files — see §15 `std/json`.
- **Fresh-literal `Object.assign` merge** — `Object.assign({ ...fresh }, ...sources)` produces a new object with the union of fields in JS key order (later sources win a duplicate key, any number of sources); a reused binding keeps its fixed shape and source keys not on it are rejected — see §18.4 `Object` built-ins.
- **Virtual member dispatch** — a method overridden anywhere in the program dispatches dynamically through the vtable, fail-closed (an unmodelable base marks every method virtual; non-overridden members stay direct) — see §4.7 Classes and interfaces.
- **Parallel call diagnostics** — `parallel.for` / `parallel.reduce` / `parallel.run` report unknown members, misplaced or wrong-shaped closures, and a missing range first argument at check time — see §5 Runtime essentials.
- **Typed promise handlers** — `.then` / `.catch` / `.finally` handlers take typed parameters, and `Promise` combinators accept plain values as already-resolved promises (wrapped per element) — see §5.1 `async` / `await` and promises.

Full notes: [`docs/release/v0.2.md`](https://github.com/Krishnesh-Mishra/cp3/blob/master/docs/release/v0.2.md). Build from source and run the suite: [`docs/BUILD.md`](https://github.com/Krishnesh-Mishra/cp3/blob/master/docs/BUILD.md).

# 1. One-file summary: what transfers and what does not

| JavaScript / TypeScript concept | CP3 equivalent | Note |
|---|---|---|
| `node` / `npx` | `cp3` (or `cp3c`) CLI | All development is through the CP3 CLI |
| `import` / `require` | `import` only | Only ES-module style imports; no CommonJS, no `require` |
| `npm` packages | CP3 packages (`.cp3pak`) / `std/...` modules | Arbitrary npm JS does not run in CP3 |
| Type annotations | Type annotations | Checked by the compiler; they are part of the program, not comments |
| `number` | `number` | A 64-bit float. No separate JS integer type |
| `string` | `string` | UTF-8 **bytes**, not UTF-16 code units |
| Arrays | `T[]` built-in arrays + `std/collections` | No index signatures; use `Map<K,V>` for dynamic keys |
| `object` with unknown keys | `Map<K, V>` | Open index signatures are not supported |
| `console.log` | `std/console` | Import explicitly; no global `console` |
| Node globals (`process`, `Buffer`, `fetch`) | `std/process`, `std/buffer`, `std/network` | Nothing is global unless a module is imported |
| `async` / `await` | `async` / `await` | Event-loop, not per-call OS threads |
| `try` / `catch` | `try` / `catch` | Catchable values are not guaranteed to be `Error` |
| `for...in` | `for...of` | `for...in` over objects is rejected |
| `any` | rejected (CP3421) | Use a concrete type or `object` |
| Classes with decorators | classes, no decorators | Decorators are not supported |
| Static typing | static typing | But a narrower, closed type surface |
| Browser / DOM | not available | CP3 runs on the host OS, not in browsers |

The compiler: **CP3 parses and type-checks `.cp3` source → generates C++ → a native C++ toolchain (normally Clang) compiles and links an executable.** The `.cp3` files are never passed to Node.

# 2. Command line

The CLI executable may be named `cp3` or `cp3c`; use whatever is installed:

```sh
cp3 help
cp3 toolchain list
```

## 2.1 Everyday commands

| Command | What it does |
|---|---|
| `cp3 init <dir> --yes` | Create an application scaffold (manifest + `src/index.cp3`). `--type package` makes a library. |
| `cp3 check [file]` | Parse, resolve modules, semantic check, type-check. No C++ generation, no link. Fast iteration. |
| `cp3 dev [file]` | Watch mode: rerun the check/build workflow on changes. `--once` does a single pass. |
| `cp3 run <file>` | Build if needed and launch the program; propagates its exit code. |
| `cp3 build [file]` | Generate C++, compile, link — no execution. Defaults to **release** profile. |
| `cp3 fmt <file>` | Format CP3 source with the project formatter. |
| `cp3 clean` | Remove `.cp3/build` and `.cp3/out`. |
| `cp3 clean --all` | Also clear `.cp3/packages`, `.cp3/lock.json`, and `.cp3/native` caches. |

Artsifacts: `.cp3/build` holds build intermediates; `.cp3/out` holds the built executable. `check` does not prove the native C++ compile/link will succeed — `run`/`build` do.

## 2.2 Profile and native-compiler options (for `build` / `run`)

| Switch | Effect |
|---|---|
| `--release` | Optimized release build (default for `build`). |
| `--incremental` | Prefer cached translation units; disables LTO/PGO. |
| `--debug` | `-O0` with debug information. |
| `--opt-level <0-3|s|z>` | Override optimization level. |
| `--no-lto` / `--no-native` / `--no-unroll` | Turn off specific release optimizations. |
| `--strict-fp` | Retain floating-point errno behavior. |
| `--pgo <file>` / `--pgo-generate[=<dir>]` / `--no-pgo` | Use / produce / disable profile-guided optimization. |
| `--toolchain <clang[@ver]\|path>` | Select Clang for this invocation. |
| `--strict-toolchain` | Fail if the saved pinned compiler is missing. |
| `--show-runtime` | Preserve runtime headers and translation units used. |

Profile affects generated machine code — keep the same profile and toolchain for any comparison.

## 2.3 Toolchain management

```sh
cp3 toolchain list
cp3 toolchain detect
cp3 toolchain use clang@18 --project
cp3 toolchain clear --project
cp3 toolchain list --json   # for scripts
```

## 2.4 Package commands

`install`, `publish`, `recompile` (add native binary to a package archive), `link`, `unlink`, `exec`. Packages are `.cp3pak` artifacts. Use `--dry-run` where supported.

## 2.5 Editor and docs tooling

```sh
cp3 docs                 # serve bundled guide on loopback (default 4173)
cp3 docs --port 0        # free port, prints the address
cp3 lsp                  # language server (stdio for editors)
cp3 dap                  # debug adapter
cp3 editor-plugin        # editor extension workflow
```

## 2.6 Exit status

CLI: `0` success, `1` compile error, `2` usage error. `run` can propagate the built program's exit code. Pass `--` to forward arguments to the program/external tool where supported.

# 3. Project setup

Create a project:

```sh
cp3 init hello-app --yes
cd hello-app
cp3 check
cp3 run
cp3 build --release
```

## 3.1 `package.json` as CP3 project metadata

The manifest supplies the entry point, project kind, type-name vocabulary, toolchain pin, and package settings. It is **not** an npm runtime contract.

```json
{
  "name": "hello-app",
  "version": "1.0.0",
  "type": "application",
  "main": "src/index.cp3",
  "typeSystem": "typescript",
  "strictNumericCasts": false,
  "build": {
    "bundleNativeDlls": true
  }
}
```

| Field | Meaning |
|---|---|
| `type` | `"application"` (has an entry) or `"package"` (library). |
| `main` | The source entry point used by project commands. |
| `typeSystem` | `"typescript"` (default), `"cpp"`, or `"rust"` — vocabulary for supported unprefixed scalar names **and the default type of un-annotated integer literals** (`8080` → `i32` under both native modes, `number` under TypeScript). CP3 still generates C++. |
| `strictNumericCasts` | Boolean: reject implicit mixed-width numeric conversions. |
| `build` | Project-specific build settings such as `bundleNativeDlls`. |

## 3.2 Files

```
hello-app/
├── package.json
└── src/
    └── index.cp3
```

Run from the project root so the nearest `package.json` is used. For nested projects, an explicit source path selects the manifest.

# 4. Language quick reference

## 4.1 Bindings

```ts
const appName: string = "reporter";  // binding cannot be reassigned
let retryCount: number = 0;          // can be rebound to same type
retryCount = retryCount + 1;
```

| Declaration | Use when |
|---|---|
| `const` | Assigned once, stays associated with that value. Does **not** freeze the object graph. |
| `let` | Reassigned a new compatible value later. |
| `var` | Legacy function-scoped hoisting behavior. New code: avoid. |

`const` on an array prevents rebinding the name, not `push`/mutations. Annotations follow the name: `name: Type`. Local inference from an initializer is supported; annotate empty arrays, uninitialized values, and API boundaries.

```ts
let attempts = 0;              // inferred number
let caption = "Waiting";       // inferred string
const names: string[] = [];    // empty array: explicit annotation required
```

Reassignment after inference stays type-checked; a `let` that is `string` does not become a union when a later assignment differs. Use an explicit union for genuinely alternate states:

```ts
let maybePort: number | undefined = undefined;
let selectedName: string | null = null;
```

`null` and `undefined` are separate. `number | undefined` does not accept `null`.

## 4.2 Functions

```ts
function formatCount(count: number): string {
  return "count=" + count;
}

function displayName(name?: string): string {
  return name ?? "anonymous";
}

const isLong = (text: string): boolean => text.length > 20;
```

Every required return path of a function with a declared non-`undefined` result type must produce a compatible value. Optional parameters use `?`; the missing argument is `undefined` inside the body. Arrow functions are for callbacks/closures.

## 4.3 Control flow

```ts
if (retryCount < 3) {
  retry();
}

let index = 0;
while (index < values.length) {
  process(values[index]);
  index = index + 1;
}

for (let i = 0; i < 10; i = i + 1) {
  console.log(i);
}

for (const item of items) {
  process(item);
}
```

Conditions require a supported boolean expression — CP3 does not silently coerce arbitrary values to boolean. `&&` / `||` short-circuit; `!` negates; `??` falls back on `null`/`undefined`; `||` follows boolean/falsey rules. `break`/`continue` target the innermost loop. `for...in` over plain objects is **rejected**; use `for...of` on arrays/collections or an explicit loop.

## 4.4 Values and types

```ts
interface User {
  name: string;
  active: boolean;
}

const user: User = { name: "Mira", active: true };
const points: [number, number] = [3, 4];
type Pair<T> = [T, T];
type UserId = number;
```

Built-in types:

| Type | Meaning |
|---|---|
| `number` | 64-bit float |
| `string` | UTF-8 text |
| `boolean` | `true` / `false` |
| `null` | distinct null value |
| `undefined` | absent-value sentinel |
| `void` / `unknown` annotations | rejected (CP3420 / CP3421) — use `undefined` for no result, `object` for an untyped value |
| `number` vs native `i32/u64/usize` | declared width wins in inference; mixing `number` with native ints (and mixing signed/unsigned) is rejected with CP3411, whose diagnostic names the operand and points at `parseTo(x, <target>)` — `parseTo('123', i32)` for a literal, `parseTo(x, i64)` or `parseTo(x, number)` for a value. `parseInt(x)` is typed `number` (never nullable) for JS parity; use `parseTo` when the native width matters |
| `T[]` | growable typed array |
| `Map<K,V>` / `Set<T>` | collections (from `std/collections`) |

`any` is rejected. Object literals have checked shapes in typed contexts; open index signatures are not supported — use `Map<K,V>` for dynamic keys.

## 4.5 Operators and null handling

```ts
const len = text?.length ?? 0;
const name = maybe ?? "anonymous";
const truthy = a && b;
```

`?.` nullish optional chaining; `??` nullish coalescing (only `null`/`undefined`); `||` boolean/falsy. `??` is not a replacement for `||` on arbitrary falsey values.

Loose `==` is flavor-exact against `null`/`undefined`: `null == undefined` is `false` in CP3 (JS coerces them), and the compiler warns (CP3520) when a `null`/`undefined` literal appears in a loose comparison. Use `===` (which requires the same flavor) or compare against both `null` and `undefined` explicitly.

## 4.6 Destructuring

```ts
const [x, y] = [4, 9];
const { name: displayName, active } = obj;
```

Array and object patterns introduce bindings with their own `const`/`let`/`var` rules. Complex rest/computed patterns should be verified against the compiler's check behavior before relying on them.

## 4.7 Classes and interfaces

```ts
interface Named {
  name: string;
  display(): string;
}

class Person implements Named {
  name: string;

  constructor(name: string) {
    this.name = name;
  }

  display(): string {
    return this.name;
  }
}
```

Declared fields, constructors, methods, and `implements` are checked. Multiple class inheritance is rejected. Abstract classes and overrides are supported where implemented. Decorators are **not** supported. Class instances are reference-shaped values.

## 4.8 Enums

```ts
enum Direction {
  North, South, East, West,
}

enum Status {
  Ready = "ready",
  Failed = "failed",
}

const heading: Direction = Direction.North;
```

Numeric enums auto-number from `0` (`Direction.North === 0`); string enums use literal values. A declaration must use a single backing kind — mixing is rejected. Arithmetic initializers using earlier members or literals are supported.

## 4.9 Generics

```ts
function identity<T>(value: T): T {
  return value;
}

const s = identity<string>("ready");

function first<T>(list: T[]): T | undefined {
  return list[0];
}
```

Generic functions, classes, interfaces, and aliases are supported. Conditional/mapped/higher-kinded TypeScript type features are not. Imported generic functions keep their type parameters — an imported `cache<T>(value: T): T` still takes explicit arguments at the call site.

## 4.10 Modules and imports

Import forms differ per module: some expose a default namespace, some named exports.

```ts
import console from "std/console";        // default namespace
import { Buffer } from "std/buffer";      // named export
import { env } from "std/env";            // named export
import { Map, Set } from "std/collections";
import { fetch, WebSocket, createServer } from "std/network";
import { URL, URLSearchParams } from "std/url";
import { TextEncoder, TextDecoder } from "std/text";
```

`std/...` specifiers resolve against a closed standard-module registry. Unknown modules and unknown closed-module members are compile-time errors. Project modules use CP3's local resolver:

```ts
import double, { add } from "./math.cp3";
import { add as sum } from "./math.cp3";
```

Export side:

```ts
export function add(left: number, right: number): number {
  return left + right;
}

export default class Service { ... }
```

There is no dynamic `require`, no CommonJS, and no ambient Node/browser globals.

## 4.11 Errors

```ts
try {
  const job = json.parse<Job>(input);
  if (job.priority < 0) throw new Error("negative priority");
} catch (reason) {
  console.error("failed:", reason);
} finally {
  console.log("done");
}
```

Compiler diagnostics (from `check`) cannot be caught — they prevent the build. Runtime failures are either thrown synchronously, reject a promise, or live in a result value. A `throw` statement only accepts an `Error` object (or a subclass); anything else is CP3509. `catch (reason)` always binds an `Error`. `Error.captureStackTrace(error, fn?)` refreshes `error.stack` from its `name: message` and appends one `at <fn>` frame. Distinguish the failure mode per API.

# 5. Runtime essentials (what affects everyday code)

## 5.1 `async` / `await` and promises

```ts
async function readName(): Promise<string> {
  const line = await console.input("Name: ");
  return line;
}
```

`async` functions return a promise; `await` suspends the continuation on the event loop. It does **not** create an OS thread. A long synchronous loop still blocks the loop. `await` is allowed in an `async function` or at top level only; awaiting a non-promise yields the value itself. Async bodies start eagerly; continuations run FIFO as microtasks; there is one loop per thread.

Continuations flatten returned promises:

```ts
const p = fetch("http://127.0.0.1:8080/health")
  .then((res) => res.text())
  .then((text) => console.log(text), (err) => console.error(err))
  .catch((err) => console.error(err))
  .finally(() => console.log("done"));
```

`Promise.resolve(v)` / `Promise.reject(reason)` create settled promises; a rejection handler returning nothing fulfills the derived promise with a value-initialized value (JS parity approximation). Promise combinators: `Promise.all<T[]>(...) -> Promise<T[]>` (input order), `Promise.race(...)` (first settled), `Promise.allSettled(...) -> Promise<Settled<T>[]>` (`{ok, value, reason}`), `Promise.any(...)` (first fulfilled; rejects with `AggregateError("All promises were rejected")` when **all** inputs reject, including `[]`), `Promise.withResolvers()` (its `reject` accepts an `Error`). An unobserved rejection causes a nonzero exit at loop drain.

Core timer builtins need no import:

```ts
const id = setTimeout(() => console.log("later"), 1000);
clearTimeout(id);
await sleep(250);                 // Promise<undefined>
queueMicrotask(() => console.log("microtask"));
setImmediate(() => console.log("check"));
```

`setInterval` / `clearInterval` are also global; `sleep(ms)` is the awaitable form.

## 5.2 Children and event loop

```ts
const result = await process.exec("git", ["status", "--short"]);
console.log(result.stdout);
// a non-zero exitCode resolves normally — check it
```

Child I/O is asynchronous: `exec` resolves after exit with buffered stdout/stderr; `spawn` returns a handle and delivers `"data"`, `"stderr"`, `"exit"` events. Register listeners immediately and keep the event loop alive.

## 5.3 References and aliasing (practical)

`const` on an array does not freeze it:

```ts
const first = [1];
const second = first;     // copies the handle, not the elements
second.push(2);
// first also sees [1, 2]
```

Use `slice()` or `Array.from()` for a new container. Ordinary arrays and class instances are reference-shaped; two handles can observe the same mutation.

## 5.4 Closures

```ts
function makeCounter(): () => number {
  let count = 0;
  return () => {
    count = count + 1;
    return count;
  };
}
```

A closure captures names from its enclosing scope. Captures that are never reassigned may be copied; captured bindings that are reassigned use a shared runtime cell. Escaping closures (returned, stored, passed on) must not capture short-lived state.

## 5.5 Threads and workers

- `async`/`await` → event loop (single thread).
- Workers → separate contexts; exchange typed messages.
- Parallel APIs → native threads for work decomposition.

Reference counting protects ownership bookkeeping; it does **not** synchronize field access. Sharing mutable state across threads requires a supported protocol. `ConcurrentMap<K,V>` is the one thread-safe collection in `std/collections`.

### Workers: file and inline forms

```ts
// worker.cp3
import console from "std/console";

export default (thread: Thread<number, string>) => {
  thread.on("message", (n) => {
    thread.postMessage(`got ${n}`);
  });
};
```

```ts
const w = new Worker("./worker.cp3");   // literal path only; computed paths are rejected
w.on("message", (text) => console.log(text));
w.on("error", (err) => console.error(err));
w.postMessage(21);
await w.request(21);   // one-shot request/reply
w.terminate();
const inline = new Worker((thread: Thread<number, number>) => {
  thread.on("message", (n) => thread.postMessage(n * 2));
}); // cannot capture enclosing variables
```

The entry arrow is required and must be non-async, non-generic. The worker file is a full module: its top-level statements run on the worker thread before the entry arrow, and it may import modules (including `std/console`); state is per-worker, not shared with the main thread.

Handle surface:
- Main side: `postMessage(v)`, `on("message" | "error", cb)`, `await request(v)`, `terminate()` (idempotent; drops queued/pending work).
- Worker side: `thread.on("message" | "error", cb)`, `thread.postMessage(v)`.
- A thrown handler becomes an `"error"` event on the other side; a startup throw ends the thread after reporting `"error"`. A live worker keeps the main loop alive; destroying the handle terminates it.

**Payload:** messages are deep copies both directions. Only copyable values (numbers, strings, booleans, arrays, records, enums, unions/options thereof) may cross; functions, promises, and handles are rejected (CP3907), and a mismatched payload type is CP3908.

**Empty directions:** spell `undefined` in a direction to declare it empty — the direction never carries a value, sends into it are CP3906 (including `request` needing a non-empty `Out`), and the receive listener is nullary:

```ts
export default (thread: Thread<number, undefined>) => {
  thread.on("message", (n) => console.log(`${n}`));  // no postMessage back
};
```

Top-level `await` is also allowed in the entry file and in the worker module’s prologue.

### Parallel work (synchronous)

```ts
parallel.for(0..n, (i) => { /* disjoint writes via i */ });
const total = parallel.reduce(0..n, 0, (sum, i) => sum + i);
const results = parallel.run(f1, f2);   // -> results array
```

Parallel calls block until done; captures are non-owning borrows (synchronous call), only writes through `parallel.for`'s own index are allowed, captured-`let` reassignment is banned, and `reduce` merges per-chunk private accumulators deterministically. Prefer `parallel.reduce` over shared mutation.

### EventEmitter (builtin, no import)

```ts
const bus = new EventEmitter();
const handler = (a: object, b: object): undefined => console.log(a, b);
bus.on("job", handler);
bus.emit("job", 1, 2);   // listener values are erased: emit passes each argument; a 1-param listener sees the first; a nullary listener sees none; missing args read undefined. Max 8 args.
bus.once("job", handler);
bus.off("job", handler); // removes the most recent identical registration (keep a named handler)
```

Registration order, snapshot iteration, `once` removed before firing, no-listener emit is a no-op.

### Weak references and reference cycles

Strong reference cycles are a compile error (CP3401) — there is no tracing garbage collector. Break a parent/child cycle with a `weak` field:

```ts
class Child {
  weak parent: Parent | undefined;
}
```

A `weak` field is a `WeakRef<T>`: reading it acquires a strong handle only for as long as you hold it; an expired or cleared link reads `undefined`. Assigning `null`/`undefined` clears the link. An unbroken strong cycle is rejected; automatic weak-edge selection is report-only and not active — write the `weak` edge yourself.

Reference counting still does not synchronize field access.

# 6. Standard library — master import table

| Module | Import | Provides |
|---|---|---|
| `std/console` | `import console from "std/console"` | Output, line/key input, stdin listeners |
| `std/readline` | `import readline from "std/readline"` | `readLine/readLines/readAll`, `LineReader` |
| `std/env` | `import { env } from "std/env"` | Typed env reads, defaults, `.env` loading |
| `std/process` | `import process from "std/process"` | argv, env map, exit, exec, spawn, host info |
| `std/fs` | `import fs from "std/fs"` | Read/write/mkdir/readdir/stat/stream |
| `std/path` | `import path from "std/path"` | Platform-aware path composition |
| `std/os` | `import os from "std/os"` | Platform, CPU, memory, network interfaces |
| `std/math` | `import math from "std/math"` | Constants, random, min/max, round, trig, hyperbolic/inverse trig |
| `std/date` | `import Date from "std/date"` | Now, ISO parse/format, arithmetic, diffs |
| `std/json` | `import json from "std/json"` | `parse<T>` / runtime shape validation, `stringify` |
| `std/crypto` | `import crypto from "std/crypto"` | UUID, random bytes, SHA-256/512, AES-256-GCM |
| `std/buffer` | `import { Buffer } from "std/buffer"` | Bytes, UTF-8/hex/base64/base64url conversion |
| `std/collections` | `import { Map, Set, ... } from "std/collections"` | Map, Set, LRUCache, Queue, Stack, PriorityQueue, RingBuffer, BitSet, MultiMap, ConcurrentMap, `Map.groupBy` |
| `std/network` | `import { fetch, lookup, WebSocket, WebSocketServer, createServer, tls, Headers, Request, Response, FormData, AbortSignal, Router, static, cors, bodyLimit, cookieParser } from "std/network"` | HTTP fetch/DNS, WebSocket client/server, HTTP server/router, request/response helpers, TLS (fetch https, wss://, TLS servers) |
| `std/Object` | built-in | `Object.keys`/`values`/`entries`/`assign`/`is`/`freeze`/`seal`/`preventExtensions`/`isFrozen`/`isSealed`/`isExtensible`/`getPrototypeOf`/`setPrototypeOf`/`fromEntries`/`groupBy` |
| `std/typed arrays` | built-in | `Int8Array` … `BigUint64Array`, `ArrayBuffer`, `SharedArrayBuffer`, `DataView` |
| `std/strings` | built-in | `string` methods |
| `std/arrays` | built-in | `T[]` methods |
| `std/regex` | built-in literal | `/pattern/flags` (PCRE2-backed) |
| `std/io` | `import io from "std/io"` | `waitReadable`, `waitWritable`, `pollUntilDone` |
| `std/text` | `import { TextEncoder, TextDecoder } from "std/text"` | UTF-8 byte/text conversion |
| `std/url` | `import { URL, URLSearchParams } from "std/url"`; `import url from "std/url"` | RFC 3986 parser, percent encoding |
| `std/hash` | `import hash from "std/hash"` | crc32, crc32c, fnv1a32, fnv1a64, xxhash64 |
| `std/time` | `import time from "std/time"` | `Stopwatch`, `console.time`/`timeEnd` |
| timers / numeric / clone | built-in | `setTimeout/setInterval/clearTimeout/clearInterval/sleep/queueMicrotask/setImmediate`; `parseInt/parseFloat/parseTo/isNaN/isFinite`; `structuredClone/atob/btoa`; `Object`, `String`, `Number`, `Boolean`, `Array`, `EventEmitter` |

`std/log` was removed before v0.2: use `console.log` / `console.info` for logging output.

Arrays, strings, typed arrays, `ArrayBuffer`/`SharedArrayBuffer`/`DataView` are built-in and need no import.

# 7. `std/console` — terminal output and input

```ts
import console from "std/console";
```

| Member | Signature | Effect |
|---|---|---|
| `log(...args)` | `void` | Write to stdout, space-separated, newline |
| `info(...args)` | `void` | Same as `log`, different name |
| `warn(...args)` | `void` | Write to stderr, newline |
| `error(...args)` | `void` | Write to stderr, newline |
| `assert(condition, ...args)` | `void` | Write to stderr only when `condition` is false; does not throw |
| `input(prompt?)` | `Promise<string>` | Read one line; EOF → `""` |
| `readKey()` | `Promise<string>` | One keypress: `"Enter"`, `"ArrowUp"`, or the character |
| `on(event, listener)` | `void` | `"keypress"`, `"line"`, or `"close"` |
| `off(event, listener)` | `void` | Remove an exact listener (keep a reference) |
| `clear()` | `void` | Clear terminal screen |
| `hide()` / `show()` | `void` | Hide/restore the Windows console window (no-op otherwise) |
| `time(label?)` | `undefined` | Start a per-label stopwatch (default `default`) |
| `timeEnd(label?)` | `undefined` | Print `label: 12.345ms` and drop the label |

`console.log` is a complete call, not a stream object — there is no `console.stdout.write`. For pipes, use `process.exec`/`spawn`.

# 7.1 `std/readline` — stdin line reading

```ts
import readline from "std/readline";
```

| Member | Signature | Effect |
|---|---|---|
| `readLine()` | `(): Promise<string>` | Next line, terminator removed; `""` on EOF |
| `readLines()` | `(): Promise<string[]>` | All remaining lines to EOF |
| `readAll()` | `(): Promise<string>` | All remaining stdin text |
| `createReader()` | `LineReader` | Reader with `readLine()`, `readLines()`, `on("line" \| "close", cb)`, `close()` |

Accept `readLine`/`readLines` as separate module-level helpers, or create one reader when lines must also be observed as they arrive.

# 8. `std/env` — environment and configuration

```ts
import { env } from "std/env";
```

Environment values arrive as strings. `env.get<T>` converts and applies defaults.

| Call | Result | Missing value |
|---|---|---|
| `env.get<T>(key)` | `T` (string if `T` omitted) | throws |
| `env.get<T>(key, { default })` | `T` | returns the default |
| `env.has(key)` | `boolean` | `false` when the key is missing or the OS value is empty; `true` for values written via `env.load`/`process.env.set` (even empty) |
| `env.load(path)` | `void` | throws if the file cannot be read |

```ts
const host = env.get<string>("APP_HOST");
const port = env.get<number>("APP_PORT", { default: 8080 });
const debug = env.get<boolean>("APP_DEBUG", { default: false });

if (env.has("REGION")) {
  console.log("region:", env.get("REGION"));
}

env.load(".env");   // relative to cwd; populates process.env
```

`has` reports presence in the runtime environment map, which is seeded from the OS environment on first use: an OS variable that is unset or present-but-empty (`KEY=`) is treated as **absent**, while a value written through `env.load` (even an empty one) **is** present. `env.load` does not encrypt secrets or write values back to the file. For typed reads with defaults, use `std/env`; `std/process` exposes a separate `Map<string,string>` on `process.env`.

## 8.1 Read-once configuration boundary

```ts
interface AppConfig {
  host: string;
  port: number;
  verbose: boolean;
}

function loadConfig(): AppConfig {
  return {
    host: env.get<string>("APP_HOST", { default: "127.0.0.1" }),
    port: env.get<number>("APP_PORT", { default: 8080 }),
    verbose: env.get<boolean>("APP_VERBOSE", { default: false }),
  };
}
```

Validate at the boundary: a parse failure or an out-of-range port should be reported once, before the service starts.

# 9. `std/process` — arguments, environment, children, host info

```ts
import process from "std/process";
```

| Member | Type / result | Use |
|---|---|---|
| `argv` | `string[]` | Arguments to this program (executable name is item 0) |
| `env` | `Map<string, string>` | In-memory environment map — use `get`/`has`/`set` |
| `exit(code)` | `void` | Flush stdout and terminate with the exit code |
| `exec(cmd, args)` | `Promise<ExecResult>` | Run a child, collect stdout/stderr until exit |
| `spawn(cmd, args)` | `SpawnHandle` | Start a child and observe live events |
| `cwd()` | `string` | Current working directory |
| `chdir(path)` | `void` | Change working directory (throws on failure) |
| `pid` | `number` | Process id |
| `ppid` | `number` | Parent process id |
| `uptime()` | `number` | Seconds since start (monotonic) |
| `memoryUsage()` | `{ rss, heapTotal, heapUsed, external, arrayBuffers }` | `rss` is bytes; the four heap fields are always `0` |
| `hrtime()` | `[number, number]` | `[seconds, nanoseconds]` monotonic |

## 9.1 Arguments and environment

```ts
const mode = process.argv.length > 1 ? process.argv[1] : "serve";
const env = process.env;

if (env.has("APP_MODE")) {
  console.log("mode:", env.get("APP_MODE"));
}

env.set("APP_MODE", "production");  // in-memory only; not passed to child OS env
```

`process.env` is a CP3 `Map`; `get` returns `string | undefined`. An empty OS variable is treated as absent on map seeding. Mutating `process.env` does **not** call the OS environment API — child processes inherit the OS environment.

## 9.2 Exit codes

```ts
if (process.argv.length < 2) {
  console.error("missing command");
  process.exit(2);
}
```

`exit` is an immediate process-level termination; code after it does not run. Use `0` for success.

## 9.3 `exec` — buffer-and-collect children

```ts
const result = await process.exec("git", ["status", "--short"]);
if (result.stdout.length > 0) console.log(result.stdout);
if (result.stderr.length > 0) console.error(result.stderr);
if (result.exitCode !== 0) console.error("child exited", result.exitCode);
```

`ExecResult` has exactly `stdout: string`, `stderr: string`, `exitCode: number`. A non-zero exit code resolves normally — you must check `exitCode`. The promise **rejects** only when the runtime cannot launch the command. `exec` buffers all output, so use `spawn` for long/noisy children.

## 9.4 `spawn` — live child events

```ts
const child = process.spawn("tool", ["--watch"]);

child.on("data", (chunk: string): undefined => {
  console.log("out:", chunk);
});
child.on("stderr", (chunk: string): undefined => {
  console.error("err:", chunk);
});
child.on("exit", (code: number): undefined => {
  console.log("finished with", code);
});

child.write("input line\n");   // text to child stdin (no Buffer overload)
child.kill();                   // termination request; "exit" still fires
```

Events: `"data"` (stdout chunk), `"stderr"` (stderr chunk), `"exit"` (status `number`). Register listeners immediately — output may arrive before a later listener. Launch failure throws synchronously from `spawn`. Writing after the child closes its input is a no-op. There is no typed close-stdin or await-close method.

## 9.5 Host info and timing

```ts
console.log("cwd:", process.cwd());
console.log("pid:", process.pid, "ppid:", process.ppid);
console.log("uptime:", process.uptime(), "s");

const [sec, ns] = process.hrtime();
console.log(`elapsed: ${sec}s ${ns}ns`);

const usage = process.memoryUsage();
console.log(`rss: ${usage.rss} bytes`);
```

`hrtime` returns `[seconds, nanoseconds]`; subtract two readings for an interval. `memoryUsage.rss` is the resident-set size in bytes.

## 9.6 JS/Node porting map (process)

| Node expectation | CP3 equivalent |
|---|---|
| `process.argv` | Supported; includes executable name |
| `process.env.NAME` | `process.env.get("NAME")` or `std/env` |
| `process.stdout.write(...)` | Supported; `StdioStream.write(text: string): void` (no newline, then flush) |
| `process.stderr.write(...)` | Supported; same API on `process.stderr` |
| `process.stdin` | Supported; `readAll`/`readLines`/`readLine` promises, `on("line" \| "close", ...)`, `close()` |
| `process.on("SIGINT" \| "SIGTERM" \| "exit" \| "unhandledRejection", ...)` | Supported; lifecycle listeners; `onShutdown` is sugar for `on("exit", ...)` |
| `process.exec(...)` | CP3 `exec` → `Promise<ExecResult>` |
| `process.spawn(...)` | CP3 `spawn` → `SpawnHandle` with `on`/`write`/`kill` |

# 10. `std/fs` — files and directories

```ts
import fs from "std/fs";
```

Synchronous calls throw directly; promise-based calls reject. Both report failure — choose based on whether the program is blocking.

## 10.1 Read and write

```ts
// synchronous
const text = fs.readFileSync("config.json");
fs.writeFileSync("out.txt", "done\n");

// asynchronous
const later = await fs.readFile("config.json");
await fs.writeFile("data.bin", Buffer.from([1, 2, 3]));
await fs.appendFile("log.txt", "line\n");
```

Read methods accept an optional encoding string. Text ops when the file is text in the expected encoding; byte ops when content must stay as bytes.

## 10.2 Directories and metadata

```ts
const stats = await fs.stat("data/input.bin");
if (stats.isFile) {
  console.log("bytes:", stats.size);
}

await fs.mkdir("build/out", { recursive: true });
const names = await fs.readdir("src");
await fs.unlink("temp.txt");

const exists = fs.existsSync("file");   // point-in-time; see below
```

`FileStats`: `size` (bytes), `isFile`, `isDirectory`, `modifiedAt` (`Date`). A `stat`/`exists` result can become stale between the check and the operation — perform the operation and handle the error.

## 10.3 Streams for large files

```ts
import fs from "std/fs";

const reader = fs.readFileStream("large.log", 64 * 1024);
// read chunks, handle "data"/"end"/"error" per the stream interface
await fs.writeFileSync("out.bin", ...);
```

`readFileStream(path, chunkSize?)` returns a `ReadStream`; `writeFileStream(path)` returns a `WriteStream`. Process chunks promptly; always close/finish streams, including error paths.

## 10.4 Safety rules

Relative paths resolve from the process working directory — use `std/path` to compose them. Avoid check-then-use patterns for security-sensitive paths; perform the operation and catch. Normalize paths for display/comparison but remember normalization is lexical (symlinks not resolved). For atomic replacement writes, write to a temp file in the same directory and rename after success; the library does not promise transactionality.

# 11. `std/path` — platform-aware paths

```ts
import path from "std/path";
```

| Member | Signature | Result |
|---|---|---|
| `sep` | `string` | Platform separator |
| `join(...parts)` | `string` | Join parts with `sep` |
| `resolve(...parts)` | `string` | Absolute, normalized path |
| `normalize(p)` | `string` | Normalize `.`/`..` (no existence check) |
| `basename(p, ext?)` | `string` | Final component, optionally without `ext` |
| `dirname(p)` | `string` | Directory component |
| `extname(p)` | `string` | Extension with leading dot, or `""` |
| `isAbsolute(p)` | `boolean` | Whether the path is absolute |

```ts
path.join("build", "generated", "index.cpp");
path.resolve("build", "generated");
path.basename("src/main.cp3", ".cp3");  // "src/main" -> "main"
path.extname("archive.tar");             // ".tar"
```

Path functions do not touch the filesystem. Normalization is lexical, not symlink-aware.

# 12. `std/os` — host information

```ts
import os from "std/os";
```

| Member | Return |
|---|---|
| `platform()` | `"windows"`, `"macos"`, or `"linux"` |
| `hostname()` | Machine host name |
| `homedir()` | User's home directory |
| `tmpdir()` | System temporary directory |
| `cpus()` | Logical CPU count |
| `totalMemory()` / `totalmem()` | Total system memory in bytes |
| `freeMemory()` / `freemem()` | Free system memory in bytes (snapshot) |
| `loadavg()` | `[number, number, number]` (1/5/15 min) or zeros |
| `networkInterfaces()` | `Array<[name, address, family, internal]>` |

```ts
console.log("platform:", os.platform());
console.log("host:", os.hostname());
console.log("CPUs:", os.cpus());
console.log("free bytes:", os.freeMemory());

for (const [name, address, family, internal] of os.networkInterfaces()) {
  console.log(`${name} ${family} ${address}${internal ? " (internal)" : ""}`);
}
```

Memory readings are snapshots, not allocation guarantees. `cpus()` is a count, not a scheduling promise. In Rust/C++ projects these become exact widths (`cpus()` → `usize`, memory → `u64`); TypeScript projects keep `number`.

# 13. `std/math` — numbers

```ts
import math from "std/math";
```

`number` is a 64-bit float; a math helper may return an integer-valued `number` but the type system does not make it a fixed-width integer.

| Member | Signature | Notes |
|---|---|---|
| `PI`, `E` | `number` | Constants |
| `random()` | `(): number` | Pseudorandom in `[0, 1)` — **not** cryptographic |
| `min(...values)` | `(...number[]): number` | Variadic |
| `max(...values)` | `(...number[]): number` | Variadic |
| `pow(base, exponent)` | `(number, number): number` | Exponentiation |
| `gcd(a, b)` / `lcm(a, b)` | `(number, number): number` | Over `number`; exact only within double range |
| `clamp(x, min, max)` | `(number, number, number): number` | Caller supplies ordered bounds |
| `lerp(start, end, t)` | `(number, number, number): number` | Extrapolates outside `[0, 1]` |
| `floor(x)` | `(number): number` | Toward negative infinity |
| `ceil(x)` | `(number): number` | Toward positive infinity |
| `round(x)` | `(number): number` | Nearest integer |
| `trunc(x)` | `(number): number` | Toward zero |
| `abs(x)` | `(number): number` | Absolute value |
| `sqrt(x)` / `cbrt(x)` | `(number): number` | Square / cube root |
| `log(x)` / `log2(x)` / `log10(x)` / `log1p(x)` | `(number): number` | Natural, base-2, base-10, `ln(1+x)` |
| `sin(x)` / `cos(x)` / `tan(x)` | `(number): number` | Radians |
| `asin(x)` / `acos(x)` / `atan(x)` / `atan2(y,x)` | `(number): number` | Inverse trig, radians |
| `sinh(x)` / `cosh(x)` / `tanh(x)` | `(number): number` | Hyperbolic |
| `asinh(x)` / `acosh(x)` / `atanh(x)` | `(number): number` | Inverse hyperbolic |
| `exp(x)` | `(number): number` | `e^x` |
| `hypot(...values)` | `(...number[]): number` | sqrt of sum of squares |
| `sign(x)` | `(number): number` | `-1`, `0`, or `1` (NaN stays NaN) |
| `LN2`, `LN10`, `LOG2E`, `LOG10E`, `SQRT1_2`, `SQRT2` | `number` | Constants |

Import the module for `math.random()` etc. — bare `Math.random()` is not a global in this surface.

```ts
const half = math.random();
const rounded = math.round(2.7);      // 3
const floored = math.floor(-1.2);     // -2
const trunced = math.trunc(-1.2);     // -1
const h = math.lerp(0, 100, 0.5);     // 50
const c = math.clamp(5, 0, 10);       // 5
```

Validate inputs/outputs when the application needs a stricter policy. Use native integer types for exact fixed-width arithmetic; use typed arrays for dense fixed-width storage.

# 14. `std/date` — dates and times

```ts
import Date from "std/date";
```

```ts
const now = Date.now();
const release = Date.fromISOString("2026-10-30T08:00:00Z");

console.log(release.toISOString());
console.log(release.toString());

const tomorrow = release.addDays(1).addHours(2);
const elapsed = tomorrow.diffMs(release);   // milliseconds

const month = release.getMonth();           // 0-based: 0 = January
const day = release.getDate();
```

| Static | Instance |
|---|---|
| `now(): Date` | `toISOString(): string` |
| `fromISOString(s): Date` | `toString(): string` |
| `parse(s): i64` | `getFullYear()` … `getSeconds(): number` |
| | `getTime(): i64` (`number` in TS projects) |
| | `setTime(ms: i64): i64` |
| | `setFullYear(y)`, `setMonth(m)`, `setDate(d)`, `setHours(h)`, `setMinutes(m)`, `setSeconds(s): i64` |
| | `addDays/Hours/Minutes/Seconds(n): Date` |
| | `isBefore(other): boolean` / `isAfter(other): boolean` |
| | `diffMs(other): i64` |

`setTime` and the `set*` calendar setters mutate in place; `add*` methods are **immutable** — they return a new `Date`; the receiver is unchanged. For elapsed comparisons, use `diffMs`, not subtraction of calendar fields. `getTime()`/`diffMs()`/`setTime()`/`set*()` use `i64` in Rust/C++ projects and `number` in TypeScript projects; calendar getters stay `number`. `Date.parse(s)` returns epoch milliseconds, not a `Date`. ISO output for machine-readable interchange; `toString()` for humans.

# 15. `std/json` — serialization and checked parsing

```ts
import json from "std/json";
```

```ts
interface Job {
  id: string;
  priority: number;
  tags?: string[];
}

const text = json.stringify({ name: "Ada", active: true }, 2);   // indent optional
const job = json.parse<Job>(text);
if (job.priority < 0) throw new Error("priority cannot be negative");
```

| Call | Result | Failure |
|---|---|---|
| `json.parse<T>(text)` | `T` | Throws on malformed JSON or runtime shape mismatch |
| `json.stringify(value, indent?)` | `string` | Unsupported values follow runtime rules |

`parse<T>` requires the type argument; bare `json.parse(text)` is rejected. `parse<T>` does **runtime structural validation**, not a compile-time assertion: it checks member existence and value types. It does not validate business rules (ranges, reachable URLs). JSON's model is limited to null, booleans, numbers, strings, arrays, and objects with string keys; functions, class instances, `Map`, `Set`, `Buffer`, and closures are not auto-serialized. Replacer/reviver callbacks are not exposed.

# 16. `std/crypto` — secure primitives

```ts
import crypto from "std/crypto";
```

| Member | Signature | Result |
|---|---|---|
| `randomUUID()` | `(): string` | RFC 4122 v4 UUID, 36 chars with hyphens |
| `randomBytes(length: number): Buffer` | Buffer | Cryptographically random bytes (0–2,147,483,647; 0 → empty) |
| `sha256(data: string \| Buffer): string` | Hex digest | Lowercase, 64 chars |
| `sha512(data: string \| Buffer): string` | Hex digest | Lowercase, 128 chars |
| `AES256(key: string, data: string): string` | Base64 envelope | Nonce + ciphertext + 16-byte tag |
| `AES256Decrypt(key: string, encrypted: string): string` | Plaintext | Throws on malformed/wrong-key/tampered input |

```ts
const token = crypto.randomBytes(32);
const digest = crypto.sha256(token);                    // hash bytes as bytes
const id = crypto.randomUUID();
const encrypted = crypto.AES256(secretKey, "sensitive");
const plain = crypto.AES256Decrypt(secretKey, encrypted);
```

SHA-256/512 hash the exact input bytes; a `string` is UTF-8, a `Buffer` is raw bytes. The envelope is `nonce || ciphertext || tag`; the nonce randomizes per call, so identical inputs produce different ciphertext. `randomUUID`/`randomBytes` use `std/crypto` (libsodium). `math.random()` is **not** a substitute for secrets. Key derivation is a fixed BLAKE2b-256 hash of the key string — not a password-hardening function; do not use human passwords as encryption keys.

# 17. `std/buffer` — byte buffers

```ts
import { Buffer } from "std/buffer";
```

A `Buffer` is an immutable byte sequence. `string.length` counts bytes; `Buffer.length` counts bytes.

| Member | Signature | Result |
|---|---|---|
| `Buffer.from(text, encoding?)` | `Buffer` | Encode text; default UTF-8; hex/base64/base64url also supported |
| `Buffer.alloc(size)` | `Buffer` | Zero-filled, up to 2,147,483,647 bytes |
| `buffer.length` | `number` | Byte length |
| `buffer.toString(encoding?)` | `string` | Decode UTF-8, or render hex/base64/base64url |
| `buffer.slice(start, end?)` | `Buffer` | New buffer, range copy (indices normalized) |

```ts
const greeting = Buffer.from("Hello", "utf8");   // "utf8" default
const digest = Buffer.from("4869", "hex");
const padded = Buffer.alloc(16);
console.log(greeting.toString("hex"));           // "48656c6c6f"
```

`slice` normalizes indices (fractional → truncate toward zero, negative → count from end, out-of-range → clamp). Encodings: `utf8`, `hex`, `base64`, `base64url` (RFC 4648 §5 unpadded; `"base64Url"` is accepted as an alias). Invalid hex (odd length, non-hex digit), invalid base64 and invalid base64url throw catchable runtime errors; an unknown literal encoding is a compile-time diagnostic. `Buffer.alloc(size)` throws on negative/fractional/non-finite/excessive sizes.

`atob(text)` and `btoa(text)` are global base64 <-> byte-text helpers (no import); `atob` throws on invalid base64.

`Buffer` is immutable and has no byte-index setter; equality compares contents. Node methods (`write`, `copy`, `readUInt32LE`, `subarray`, `concat`, index assignment) are **not** implied — use typed arrays / `DataView` for numeric layout. Use `Buffer` for encoded payloads and text conversion; use typed arrays/DataView for indexed numeric records.

# 18. `std/collections` — containers

```ts
import { Map, Set, LRUCache, Queue, Stack, PriorityQueue, RingBuffer, BitSet, MultiMap, ConcurrentMap } from "std/collections";
```

All are reference values. Insertion order is preserved by iteration/`forEach` for all of them. Except `ConcurrentMap`, none are thread-safe.

## 18.1 `Map<K, V>`

| Method | Signature |
|---|---|
| `set(key, value)` | `(K, V): void` |
| `get(key)` | `(K): V \| undefined` |
| `has(key)` | `(K): boolean` |
| `delete(key)` | `(K): boolean` |
| `clear()` | `(): void` |
| `keys()` / `values()` / `entries()` | `K[]`, `V[]`, `[K, V][]` |
| `forEach(cb)` | `(value: V, key: K) => void` |
| `size` | read-only count |

```ts
const counts = new Map<string, number>();
counts.set("apples", 3);
const apples = counts.get("apples");   // number | undefined
if (counts.has("apples")) console.log(apples);
```

`get` returning `undefined` means absent — check with `has` when a stored value can also be `undefined`. Replacing a value keeps its insertion position.

## 18.2 `Set<T>`

| Method | Signature |
|---|---|
| `add(value)` | `(T): void` |
| `has(value)` | `(T): boolean` |
| `delete(value)` | `(T): boolean` |
| `clear()` | `(): void` |
| `values()` | `T[]` |
| `forEach(cb)` | `(value: T) => void` |
| `size` | read-only |

```ts
const seen = new Set<string>();
seen.add("request-42");
if (seen.has("request-42")) console.log("done");
```

## 18.3 Other collections

| Collection | Constructor | Key members | Pop |
|---|---|---|---|
| `LRUCache<K, V>` | `new LRUCache<K, V>(capacity)` | `set`, `get`, `has`, `clear`, `keys()`, `size`, `capacity` | `set`/`get` mark MRU; over `capacity` evicts LRU; `keys()` returns MRU-first |
| `Queue<T>` | `new Queue<T>()` | `enqueue`, `dequeue()`, `peek()`, `clear`, `isEmpty`, `size` | FIFO; `dequeue`/`peek` → `undefined` when empty |
| `Stack<T>` | `new Stack<T>()` | `push`, `pop()`, `peek()`, `clear`, `isEmpty`, `size` | LIFO; `pop`/`peek` → `undefined` when empty |
| `PriorityQueue<T>` | `new PriorityQueue<T>(comparator?)` | `push`, `pop()`, `peek()`, `size` | Default min-heap on `<`; `(a,b)=>a>b` gives max-heap; `pop`/`peek` **throw** `Error` when empty |
| `RingBuffer<T>` | `new RingBuffer<T>(capacity)` | `push`, `pop()`, `peek()`, `clear`, `size`, `capacity`, `isFull` | Fixed capacity; `push` overwrites oldest when full; `pop`/`peek` → `undefined` when empty |
| `BitSet` | `new BitSet()` or `new BitSet(size)` | `set(index)`, `get(index)`, `clear(index)`, `count()`, `and/or/xor(other)` | Flags addressed by `number` index; set grows as needed |
| `MultiMap<K, V>` | `new MultiMap<K, V>()` | `add(key, value)`, `get(key)`, `has(key)`, `delete(key)`, `clear`, `size` | Each key maps to a list (`get` → `V[]` in add order) |
| `ConcurrentMap<K, V>` | `new ConcurrentMap<K, V>()` | `set`, `get`, `has`, `delete`, `size` | Mutex-guarded, thread-safe; `has` is not atomic with a following `get` |

```ts
const queue = new Queue<number>();
queue.enqueue(1); queue.enqueue(2);
queue.dequeue();   // 1
queue.peek();      // 2

const maxHeap = new PriorityQueue<number>((a, b) => a > b);
maxHeap.push(5); maxHeap.push(1);
maxHeap.pop();     // 5

const ring = new RingBuffer<number>(3);
ring.push(1); ring.push(2); ring.push(3); ring.push(4);  // overwrites 1
ring.pop();    // 2

const flags = new BitSet();
flags.set(0); flags.set(2);
flags.count();       // 2
flags.and(other);

const grouped = new MultiMap<string, number>();
grouped.add("a", 1); grouped.add("a", 2);
grouped.get("a");    // [1, 2]
```

Static `Map.groupBy(list, cb: (item, index) => K): Map<K, T[]>` groups an array by a key callback (distinct from `Object.groupBy` below).

Constructor type arguments are **required** — empty collections give no element type. `size` (Map/Set/LRUCache) and LRUCache `capacity` are `usize` in Rust/C++ projects and `number` in TypeScript.

## 18.4 `Object` built-ins (no import)

`Object` is a language builtin. The dynamic-key helpers use the canonical `Record<string, V>` → `Map<string, V>` model and require statically known object fields.

| Member | Result |
|---|---|
| `Object.keys(o)` / `Object.values(o)` / `Object.entries(o)` | `string[]`, `V[]`, `[string, V][]` (static shapes only; no `for...in`) |
| `Object.assign(target, ...sources)` | target shape (known fields) | source fields not on the target are rejected; use `{ ...target, ...source }` for a merged object |
| `Object.is(a, b)` | `boolean` (Object.is semantics) |
| `Object.freeze` / `Object.seal` / `Object.preventExtensions` | same value; identity approximation — CP3 has no runtime property table |
| `Object.isFrozen` / `Object.isSealed` / `Object.isExtensible` | `boolean`; folds on a directly frozen/sealed/extensibility-controlled argument |
| `Object.getPrototypeOf(o)` | `null` (CP3 objects have no prototype chain) |
| `Object.setPrototypeOf(o, null)` | `o`; only `null` is accepted |
| `Object.fromEntries(entries)` | `Map<string, V>` (entries may be pairs or tuples) |
| `Object.groupBy(list, keyFn)` | `Map<string, T[]>`; `keyFn` must return a string key |

```ts
const o = { a: 1, b: 2 };
const r = Object.fromEntries(Object.entries(o)); // Map<string, number>
const g = Object.groupBy(["apple", "avocado"], (w) => w.charAt(0)); // Map<string, string[]>
```

Use `w.charAt(0)`, not `w[0]`: `text[i]` reads a raw UTF-8 **byte as a number**, while `charAt` returns the code point as a string.

# 19. Typed arrays, `ArrayBuffer`, `DataView` (built-in)

No import needed.

## 19.1 The classes

| Class | Width | Representation |
|---|---:|---|
| `Int8Array` | 1 byte | signed 8-bit |
| `Uint8Array` | 1 byte | unsigned 8-bit |
| `Uint8ClampedArray` | 1 byte | unsigned 8-bit, clamped writes |
| `Int16Array` | 2 bytes | signed 16-bit |
| `Uint16Array` | 2 bytes | unsigned 16-bit |
| `Int32Array` | 4 bytes | signed 32-bit |
| `Uint32Array` | 4 bytes | unsigned 32-bit |
| `Float32Array` | 4 bytes | IEEE single |
| `Float64Array` | 8 bytes | IEEE double |
| `BigInt64Array` | 8 bytes | signed 64-bit storage (read via `number`) |
| `BigUint64Array` | 8 bytes | unsigned 64-bit storage (read via `number`) |

`SharedArrayBuffer` is also a global class for shared byte storage. Element reads return `number`. The `BigInt64Array`/`BigUint64Array` classes do **not** expose JavaScript `bigint` values — values above `2^53` lose precision when read as `number`. Do not use these for exact large-integer arithmetic.

## 19.2 Construction

```ts
const samples = new Float64Array(3);
samples[0] = 1.25;

const codes = new Uint8Array([65, 66, 67]);

const storage = new ArrayBuffer(16);
const view = new DataView(storage, 4, 8);   // offset 4, length 8
```

Fixed element count — no growth. A typed view can specify an offset/length over an existing `ArrayBuffer`; multi-byte elements require aligned offsets. `BYTES_PER_ELEMENT` is a static member on each typed-array class, not an instance member.

## 19.3 Indexed access and writes

```ts
const bytes = new Uint8Array(2);
const missing: number | undefined = bytes[5];   // out of range → undefined
bytes[5] = 99;                                   // write ignored
```

Negative/fractional/out-of-bounds reads → `undefined`; out-of-bounds writes are ignored. The typed array `length`, `byteLength`, `byteOffset`, `buffer`, and `BYTES_PER_ELEMENT` describe the view. `subarray` shares storage; `slice` copies.

## 19.4 Methods

`subarray(begin?, end?)`, `slice(begin?, end?)`, `copyWithin(target, start?, end?)`, `reverse()`, `indexOf(value, fromIndex?)`, `includes(value, fromIndex?)`, `join(separator?)`, `set(source, offset?)`, `fill(value, start?, end?)`.

Writes convert values to the element representation: integer writes truncate/wrap; non-finite → zero; `Uint8ClampedArray` clamps to `[0, 255]` with ties-to-even; floats convert to their precision (`Float32Array` → single).

## 19.5 DataView — explicit endianness

```ts
const view = new DataView(new ArrayBuffer(8));
view.setUint32(1, 0x12345678, true);   // offset 1, little-endian
const value = view.getUint32(1, true);

const readInt = view.getInt16(0, true);
const readUInt = view.getUint16(0, true);
```

Multi-byte forms: `getInt8/16/32`, `getUint8/16/32`, `getFloat32/64` and matching setters. The optional boolean selects little-endian; default is big-endian. The one-byte forms have no meaningful byte order. Each access checks that the full width fits within the view. `set`/`get` offsets are view-relative.

Use `DataView` when a record mixes widths/endian or has fields at byte offsets; use a typed array for uniform fixed-width storage; use `Buffer` for byte strings with encoding conversion.

# 20. Arrays (`T[]`) — built-in

No import needed.

## 20.1 Construction and access

```ts
const scores: number[] = [8, 10, 9];
const names: string[] = [];        // empty: annotation needed
const fromArgs = Array.of(2, 4, 6);
const copy = Array.from(scores);
const direct: number[] = [2, 4, 6];
const fromTyped = Array.from(new Uint8Array(3));
```

`Array(n)` creates a length array only for supported default-initializable elements. `Array.from` currently accepts ordinary arrays and supported typed arrays.

```ts
const first = scores[0];
const count = scores.length;
```

## 20.2 Mutating methods

| Method | Signature | Effect |
|---|---|---|
| `push(...items)` | `(...T[]): number` | Append; returns new length |
| `unshift(...items)` | `(...T[]): number` | Prepend; returns new length |
| `pop()` | `(): T \| undefined` | Remove last; `undefined` when empty |
| `shift()` | `(): T \| undefined` | Remove first; `undefined` when empty |
| `sort(comparator?)` | `(T[]): T[]` | Sort in place, return receiver (stable, type-aware defaults) |
| `reverse()` | `(T[]): T[]` | Reverse in place, return receiver |
| `splice(start, deleteCount, ...items)` | `(start: number, deleteCount: number, ...T[]): T[]` | Remove/insert in place; returns removed elements |
| `fill(value, start?, end?)` | `(T[]): T[]` | Write a value over a range, return receiver |
| `copyWithin(target, start, end?)` | `(target, start, end?): T[]` | Copy slice onto target in place, return receiver |
| `unique()` | `(T[]): T[]` | Remove **adjacent** duplicates in place |
| `shuffle()` | `(T[]): T[]` | Reorder in place with seeded PRNG |

`unique` does not sort first — equal values must be adjacent. `shuffle` uses the seeded PRNG, not crypto.

## 20.3 Non-mutating methods

| Method | Result |
|---|---|
| `slice(start?, end?)` | `T[]` (copy of range) |
| `concat(...items)` | `T[]` (arrays are spread) |
| `map<U>(cb)` | `U[]` |
| `flatMap<U>(cb)` | `U[]` (map, then flatten one level) |
| `filter(cb)` | `T[]` |
| `flat(depth?)` | `T[]` (default depth 1) |
| `toSorted(comparator?)` | `T[]` (sorted copy) |
| `toReversed()` | `T[]` (reversed copy) |
| `toSpliced(start, deleteCount, ...items)` | `T[]` (new array; receiver unchanged) |
| `with(index, value)` | `T[]` (copy with one element replaced) |
| `join(separator?)` | `string` |

`forEach(cb)`, `find`, `findLast`, and predicate-style `some`/`every` are also available; callbacks receive `(element, index, array)`.

## 20.4 Search

| Method | Result |
|---|---|
| `includes(value, fromIndex?)` | `boolean` |
| `indexOf(value, fromIndex?)` | `number` (first position or `-1`) |
| `lastIndexOf(value, fromIndex?)` | `number` (last position or `-1`) |
| `find(predicate)` | `T \| undefined` |
| `findLast(predicate)` | `T \| undefined` |
| `findIndex(predicate)` | `number` (or `-1`) |
| `findLastIndex(predicate)` | `number` (or `-1`) |
| `some(predicate)` / `every(predicate)` | `boolean` |
| `binarySearch(target)` | `number` |
| `keys()` | `number[]` (indices) |
| `values()` | `T[]` |
| `entries()` | `[number, T][]` |

`binarySearch` **requires sorted input**; release builds skip ordering checks, so unsorted input is a caller error with meaningless results. Use the same sort order for search.

## 20.5 `reduce`

```ts
const total = [2, 3, 4].reduce((sum, v) => sum + v, 0);   // with initial value
const total2 = [2, 3, 4].reduce((sum, v) => sum + v);     // first element is the accumulator
const right = [2, 3, 4].reduceRight((sum, v) => sum + v, 0); // folds last -> first
```

The initial value determines the accumulator type and makes an empty-array reduction meaningful. `reduceRight` folds from the last element to the first.

## 20.6 Sorting

```ts
const descending = scores.slice().sort((left, right) => right - left);
```

Numbers sort numerically (not as strings). Provide a comparator returning negative/zero/positive. Keep comparator side-effect-free. Use `slice()` first to preserve the original order.

# 21. `string` — built-in, UTF-8 bytes

## 21.1 Length and indexing

```ts
const greeting = "hé😀";
greeting.length;       // byte count, e.g. 6
greeting.charAt(1);    // one complete UTF-8 code point
greeting[1];           // raw UTF-8 byte value
greeting.charCodeAt(1); // same raw byte as a number, or NaN
```

`.length` is byte length. `charAt`/`at` take a **code-point** index and return one code point as a string. `charCodeAt`/`[i]` are byte-offset. `String.fromCharCode(...codes)` builds a byte string (`code mod 256` per argument). `for...of` iterates code points.

```ts
const word = "A😀B";
for (const ch of word) {
  console.log(ch);
}
```

## 21.2 Searching and extracting

```ts
const i = "abcdef".indexOf("c");          // byte position, or -1
const j = "abcdef".lastIndexOf("c");
const has = "abc".includes("b");
const begins = "abc".startsWith("ab");
const ends = "abc".endsWith("c");

const sub = "abcdef".slice(1, 4);         // "bcd"
const swap = "abcdef".substring(4, 1);    // "bcd"
```

Search returns byte positions — not usable as code-point indices on multibyte text. `slice` supports negative positions relative to the end; `substring` clamps negatives to zero and swaps endpoints. Byte boundaries can split code points — use `charAt`/`for...of` for code-point work.

## 21.3 Replace, split, trim, case, pad

```ts
const one = "x y".replace(" ", "_");        // first occurrence only
const all = "x y".replaceAll(" ", "_");
const fields = "one,two,three".split(",");
const words = "red   blue".split(/\s+/);

const t = "  CP3  ".trim();                 // ASCII whitespace only
const t0 = "  CP3  ".trimStart();              // leading whitespace only
const t1 = "  CP3  ".trimEnd();                // trailing whitespace only
const upper = "AbC".toUpperCase();          // ASCII case mapping
const lower = "AbC".toLowerCase();

const id = "5".padStart(3, "0");            // "005"
const lab = "cp3".padEnd(6, ".");           // "cp3..."
const rep = "a".repeat(3);                  // "aaa"

const raw = String.raw("a\\nb");            // literal backslash-n
const grin = String.fromCodePoint(0x1f600);
```

`toUpperCase`/`toLowerCase` are ASCII-only (UTF-8 bytes pass through unchanged). `trim`/`trimStart`/`trimEnd` cover space/tab/line feed/vertical tab/form feed/carriage return only. `codePointAt(i)` returns the Unicode scalar at code-point position `i` (NaN when out of range). `matchAll(regexp)` requires a global `g` regexp and returns `[full, group1, ...]` arrays. `repeat` truncates/floors the count; negative → zero. Padding uses stored byte length, so multi-byte fill adds multiple bytes per unit.

Concatenation with `+` and template literals: the compiler lowers common chains efficiently, but do not assume a single allocation. `replaceAll`/`slice`/`split`/`replace`/`repeat`/`pad*`/case conversion all create new strings.

# 22. Regular expressions — built-in literal

```ts
const word = /cp3/i;
const digits = /[0-9]+/g;
```

A literal `/pattern/flags` creates a `RegExp` value. PCRE2-backed. `string.split(sep)` accepts a string or a `RegExp`.

| Flag | Meaning |
|---|---|
| `g` | Global: `test`/`exec` retain match position across calls |
| `i` | Case-insensitive |
| `m` | Multiline anchors |
| `s` | Dot matches line terminators |
| `u` / `v` | UTF mode |
| `y` | Sticky at current position |
| `d` | Accepted; match indices not exposed |

Supported: `dgimsuvy`. The `flags` property reports canonical order.

| Member | Signature | Result |
|---|---|---|
| `test(subject)` | `(string) => boolean` | Match? |
| `match(subject)` | `(string) => string[]` | All matches (full match strings) |
| `exec(subject)` | `(string) => string[]` | One match + groups |
| `source` | `string` | Pattern text |
| `flags` | `string` | Canonical flags |

No-match returns an **empty array**, never `null`. A non-global `exec` returns `[full, group1, ...]` — a missing group is `""`, not `undefined`. With `g`, `match` returns full matches only, no groups. `g`/`y` retain engine state internally; `lastIndex` is not exposed. A failed match resets the cursor. With `d`, indices are unavailable. Regex offsets are UTF-8 byte positions.

```ts
const parts = /(\d+)-(\w+)/.exec("42-cp3");
// parts[0] = "42-cp3"; parts[1] = "42"; parts[2] = "cp3"
const all = /a+/g.exec(str);   // repeat to process all matches
const fields = "r,g,b".split(/,/);
const ws = "one   two".split(/\s+/);
```

# 23. `std/url` — URLs and query strings

```ts
import { URL, URLSearchParams } from "std/url";
import url from "std/url";
```

| Helper | Signature |
|---|---|
| `url.encodeURI(value)` | `string` |
| `url.decodeURI(value)` | `string` |
| `url.encodeURIComponent(value)` | `string` |
| `url.decodeURIComponent(value)` | `string` |

`encodeURI` leaves URI delimiters (`; / ? : @ & = + $ , #`) intact; `encodeURIComponent` escapes everything except `A-Z a-z 0-9 - _ . ! ~ * ' ( )`. Malformed `%HH` is left as-is (deliberately unlike JS, which throws `URIError`).

## 23.1 `URL` class

```ts
const u = new URL("https://example.com:8080/a/b?q=1#top");
u.protocol;      // "https:"
u.host;          // "example.com:8080"
u.hostname;      // "example.com"
u.port;          // "8080" ("", absent or scheme default)
u.pathname;      // "/a/b"
u.search;        // "?q=1"
u.hash;          // "#top"
u.origin;        // "https://example.com:8080"
u.username;      // ""
u.password;      // ""
u.href;          // serialized URL
u.searchParams;  // URLSearchParams
```

Components are **snapshots at construction**: dot segments removed, default ports stripped, IPv6 bracketed, lowercased scheme/hostname. `new URL(relative, base)` resolves per RFC 3986 §5.2; a missing base for relative input, or an unparseable input, throws a catchable `Error` with message `TypeError: Invalid URL`. Setter-style members are **not** in the surface, but `URL.toString()` is: it returns the same serialization as `href`. `searchParams` mutations do **not** rewrite `search`/`href` (no live binding).

## 23.2 `URLSearchParams`

```ts
const sp = new URLSearchParams("a=1&b=2&a=3");
sp.get("a");         // "1" (undefined when absent)
sp.getAll("a");      // ["1", "3"]
sp.has("b");         // true
sp.append("c", "4");
sp.set("a", "9");    // replaces first, drops later duplicates
sp.delete("b");      // true
sp.toString();       // "a=9&c=4"
sp.entries();        // [["a", "9"], ["c", "4"]]
```

`+` decodes to space; malformed `%HH` parses as literal text. `set` replaces the first occurrence and drops later duplicates; appends when absent. Empty `=` yields an empty value. No leading `?` in `toString()`. Input is ASCII-whitespace-trimmed; the constructor also skips a leading `?`.

### URL snapshots and serialization

- `origin` is `""` for non-special schemes (opaque schemes and `file:`).
- A pathless authority URL reports and serializes `pathname` as `"/"`, so `href` gains the `/`.
- Userinfo splits at the **last** `@`, then at the **first** `:`.
- Scheme/hostname are ASCII-lowercased; dot segments are removed; default ports (`:80` http/ws, `:443` https/wss) are stripped; IPv6 hosts stay bracketed in `host` and unbracketed in `hostname`.

# 24. `std/text` — UTF-8 encode/decode

```ts
import { TextEncoder, TextDecoder } from "std/text";
```

| Member | Signature | Effect |
|---|---|---|
| `new TextEncoder(encoding?)` | `TextEncoder` | Label defaults `"utf-8"`; only `utf-8`/`utf8` accepted (any case, trimmed); non-literal unsupported label → catchable `Error`; literal unsupported label → compile-time error |
| `enc.encode(text)` | `Uint8Array` | UTF-8 bytes (verbatim copy of the string's bytes — no transcoding/BOM) |
| `enc.encoding` | `string` | Always `"utf-8"` |
| `new TextDecoder(encoding?)` | `TextDecoder` | Same label rules |
| `dec.decode(bytes)` | `string` | UTF-8; each invalid maximal subpart → U+FFFD; leading BOM stripped; **never throws** |
| `dec.encoding` | `string` | Always `"utf-8"` |

```ts
const enc = new TextEncoder();
const bytes = enc.encode("hello");   // Uint8Array [104,101,108,108,111]
const dec = new TextDecoder("utf8");
const text = dec.decode(bytes);
```

Instances are stateless; one per process is fine. No streaming/`decodeInto`/`stream:true`. UTF-16/Latin-1 and all other encodings are outside the surface; for hex/base64 use `std/buffer`.

# 25. `std/hash` — non-cryptographic checksums

```ts
import hash from "std/hash";
```

> **Checkum ≠ security.** These are forgeable and must never be used for passwords, signatures, tokens, or integrity against an adversary. Use `std/crypto` for adversarial cases.

| Member | Signature (Rust/C++ widths) | Result |
|---|---|---|
| `crc32(input)` | `crc32: string\|Buffer → u32` | IEEE 802.3 CRC-32 |
| `crc32c(input)` | `crc32c: string\|Buffer → u32` | Castagnoli CRC-32C |
| `fnv1a32(input)` | `string\|Buffer → u32` | FNV-1a 32-bit |
| `fnv1a64(input)` | `string\|Buffer → u64` | FNV-1a 64-bit |
| `xxhash64(input)` | `string\|Buffer → u64` | XXH64, seed 0 |

TypeScript projects return `number` for all five. `u32` results are exactly representable in `number`; `fnv1a64`/`xxhash64` above `2^53` rounds in `number` — compare those results for equality only, never expect exact decimal text. In Rust/C++ projects, `u32`/`u64` read exactly.

```ts
const cacheKey = hash.crc32(configText);
const same = hash.fnv1a64(payload) === hash.fnv1a64(payload);
```

Deterministic and stateless: same bytes → same value in every process. `crc32("") === crc32c("") === 0`; `fnv1a32("") === 2166136261`. Standard check: `crc32("123456789") === 3421780262` (`0xCBF43926`), `crc32c("123456789") === 3808858755` (`0xE3069283`), `fnv1a64("abc") === 16654208175385433931`. Header-only, no native crypto dependency. No streaming, no keyed hashing, no hex rendering — format the number at the call site.

# 26. `std/time` — monotonic clocks

```ts
import time from "std/time";
```

| Member | Signature | Effect |
|---|---|---|
| `time.stopwatch()` | `Stopwatch` | New stopped watch; `elapsed()` starts at 0 |
| `sw.start()` | `undefined` | (Re)start from now, discarding accumulated time |
| `sw.stop()` | `undefined` | Freeze; `elapsed()` repeats the frozen value |
| `sw.elapsed()` | `number` | Milliseconds since `start()`; 0 before first `start()` |
| `sw.reset()` | `undefined` | Stop and zero |
| `console.time(label?)` | `undefined` | Start per-label timer (default `default`) |
| `console.timeEnd(label?)` | `undefined` | Print `label: 12.345ms`, drop the label |
| `console.count(label?)` | `undefined` | Per-label counter (default `default`); prints `label: N`, increments before printing |
| `console.countReset(label?)` | `undefined` | Reset the counter for `label` so the next `count` prints 1 again |

```ts
const sw = time.stopwatch();
sw.start();
const live = sw.elapsed();   // live value while running
sw.stop();
const frozen = sw.elapsed(); // same on every later read
console.log(frozen === 0 ? "nothing elapsed" : frozen + "ms");
sw.reset();
```

The clock is `std::chrono::steady_clock` (`QueryPerformanceCounter` on Windows, `CLOCK_MONOTONIC` elsewhere). Monotonic only: never use for wall-clock time, scheduling at a wall instant, or cross-process comparison — use `std/date` for those. `elapsed()` is `number`; there is no integer duration width. Timers are separate global builtins (`setTimeout`/`setInterval`/`sleep`), not part of `std/time`. `console.time`/`timeEnd` share the same clock. `timeEnd` for a never-started label writes nothing to stdout and prints `Timer "label" does not exist` to stderr. Construction and all `sw` methods allocate nothing and throw nothing. `console.count`/`countReset` are also part of the surface: a per-label counter printed as `label: N` (default label `default`), incremented before printing, reset by `countReset`.

# 27. Native scalar types — quick reference

Use these at boundaries where width/signedness matter, not for ordinary application arithmetic. Three families are available: project-wide vocabulary (`typeSystem`) or explicit prefixes (`r_`, `c_`, `ts_`). CP3 still generates C++.

```ts
let width: r_'u32' = 512;       // Rust-style spelling
let count: c_'long long' = 9007199254740992;
let ratio: ts_'number' = 0.25;
```

| Project `typeSystem` | `cpp` | `rust` |
|---|---|---|
| `typeSystem: "cpp"` | `int`, `long long`, `size_t`, `uint32_t`, … | — |
| `typeSystem: "rust"` | — | `i64`, `u32`, `usize`, `isize`, … |
| `typeSystem: "typescript"` (default) | use `c_'...'` prefixes | use `r_'...'` prefixes |

`typeSystem` affects resolution of **unprefixed** supported scalar names only. The quoted spelling lets C++ names span spaces: `c_'unsigned long long'`. Prefixes are **not** casts, pointers, references, or FFI declarations — pointer/reference/array declarators are rejected. `r_'f64'`, `c_'double'`, and `ts_'number'` all resolve to ordinary CP3 `number`.

`strictNumericCasts: true` rejects implicit conversions like `let d: r_'i64' = sourceNumber;` and mixed-width arithmetic.

Native integer arithmetic is **checked in every project mode**: addition, subtraction, multiplication, unary negation, increment/decrement, and arithmetic compound assignments throw `Error` on overflow (unsigned) or undefined-behaviour territory (signed) instead of wrapping. Division/remainder throw on zero divisor; signed `INT_MIN / -1` throws. Signed division truncates toward zero; remainder takes the dividend's sign. `value.wrappingMul(other)` is the explicit per-operation opt-in to modulo-2^N arithmetic (hash/FNV-style code); other operators have no wrapping form. Signed and unsigned natives do not mix in arithmetic in **any** mode — cast explicitly (`value as r_'i64'`) or convert with the global `parseTo`.

`parseTo` is a global, checked, bidirectional converter (no import):

```ts
parseTo("123", number);                 // string -> number
parseTo("3.5", number);
parseTo(value, string);                 // number/native -> string
let u: r_'u32' = parseTo<r_'u32'>("4294967295");
```

Narrowing via `parseTo<T>(value)` / `value as T` verifies at runtime and throws `Error` on fractional or out-of-range input; it never silently truncates. Use `r_'...'` for Rust spellings, `c_'...'` for C++ spellings, and `ts_'...'` for TypeScript spellings.

# 28. `std/network` — fetch, server, WebSockets

```ts
import { fetch, lookup, WebSocket, WebSocketServer, createServer, Headers, Request, Response, FormData, AbortSignal, Router, static, cors, bodyLimit, cookieParser } from "std/network";
import network from "std/network";
```

**HTTPS runs over the same engine as `fetch` itself.** Client fetch accepts `http://` and `https://` URLs, with certificate verification on by default (embedded Mozilla CA bundle; `SSL_CERT_FILE=/path/to/bundle.pem` overrides it; TLS1.2 floor). The named registry above is closed: raw TCP (`createTcpServer`, `connectTcp`, `TcpSocket`), datagram UDP (`createUdpSocket`, `UdpSocket`), and the TLS sockets themselves (`createTlsServer`, `connectTls`, `TlsSocket`) are not exported — do not import them by name. Server-side TLS goes through `createServer({ tls })` and `new WebSocketServer({ port, tls })` with PEM **file paths** for `cert`/`key`, or through the `tls({...})` factory for reusable options objects. TLS applications never silently downgrade to plaintext: misconfiguration throws `ERR_TLS_INVALID_OPTIONS`, verification failures surface as a `TLSError` (`[CODE] message`, e.g. `[CERT_UNTRUSTED] ...`), and builds without a TLS backend fail with `ERR_TLS_BACKEND_UNAVAILABLE`.

## 28.1 Fetch and DNS

```ts
const address = await lookup("example.com");  // first getaddrinfo result; the same resolution fetch uses

const response = await fetch("http://127.0.0.1:8080/health", {
  method: "GET",
  headers: new Map<string, string>([["Accept", "application/json"]]),
  body: null,
  timeout: 5000,   // milliseconds; 0 = no timeout
  signal: AbortSignal.timeout(5000),
});

if (!response.ok) {
  throw new Error("unexpected status: " + response.status);
}
const text = await response.text();
const data = await response.json<Settings>();
```

`fetch(url, init?)` resolves to a fetch `Response` with `ok`, `status`, `statusText`, `headers`, `url`, and `text()` / `arrayBuffer()` / `json<T>()`. The body is buffered before resolution. A bad HTTP status still resolves to a response — check `ok`/`status`. DNS, connection, timeout, unsupported scheme, certificate-verification, and protocol failures reject the promise. HTTPS works out of the box: the server certificate chain is verified by default against the embedded Mozilla CA bundle (TLS1.2 floor, hostname/SNI checked against the URL host); set `SSL_CERT_FILE=/path/to/bundle.pem` to use your own CA (e.g. a corporate PKI) instead of the embedded one. Numeric fields (`status`, ports) are `u16` in Rust/C++ projects and `number` in TypeScript. `lookup(hostname)` resolves via libuv `getaddrinfo` and rejects with `Error` on failure.

Fetch value types (all named imports): `Headers.get/has/set/append/delete`; `Request(url, init?)` with `url/method/headers/signal/text/json/arrayBuffer`; `Response` adds `url`; `FormData.append/get/has/set/delete` (appended fields can be a fetch `body`); `AbortSignal.aborted` and `AbortSignal.timeout(ms)` (a timed signal `fetch(url, { signal })` applies as request timeout).

## 28.2 HTTP server

```ts
import { createServer, cors, bodyLimit, cookieParser } from "std/network";
import { Router } from "std/network";

const server = createServer();

server.get("/health", (request, response): undefined => {
  response.status(200).header("content-type", "text/plain");
  response.text("ok");
});

server.post("/users", (request, response): undefined => {
  const name = request.cookies.get("user");
  response.status(201).json({ created: true });
});

server.use(cors({ origin: "*" }));
server.use(bodyLimit(1 << 20));       // 1 MB limit → 413 on overflow
server.use(cookieParser());
server.use("/api", new Router().get("/items", handler));
server.listen(8080, () => console.log("ready"));

// later
server.close();
```

`createServer(options?)`: `threads` (integer ≥ 1), `tls` — a boolean (`true` enables TLS with default options and requires `cert`/`key` at runtime), an inline options object, or a value from the `tls({...})` factory; server-side TLS options are `{ cert, key, ca?, passphrase?, minProtocol? }` where `cert`/`key` are **PEM file paths** — inline PEM text is rejected — `cors`, `static(dir)`, `bodyLimit(n)`, `trustProxy` (honors `X-Forwarded-Proto`), `views` (render dir), `onError(err, req, res)`. Empty options object is valid. With `tls`, the server is an uWS `SSLApp` and `listen` serves HTTPS; verification floor is TLS1.2. Handlers are sync or `async () => undefined`. Routes/middleware run in registration order; calling `next()` continues the chain (returns `Promise<undefined>`). A chain ending without a response → 404.

```ts
const app = createServer({ tls: { cert: "cert.pem", key: "key.pem" } });
// or reusable options:
const creds = tls({ cert: "cert.pem", key: "key.pem" });
const app2 = createServer({ tls: creds });
```

`tls` is a named import: `import { tls, createServer } from "std/network"`.

Methods: `get`, `post`, `put`, `delete`, `patch`, `head`, `options`, `all`, `use`, plus `route(path)` chains, `param(name, fn)`, `onError(fn)` (method form), `set("secret", …)` (signed-cookie secret), and settings `enable/disable/enabled/disabled`. Request fields: `method`, `path`, `params`/`query`/`headers`/`cookies`/`signedCookies` (Maps), `baseUrl`, `originalUrl`, `ip`, `protocol` (`http` unless `trustProxy`), `body` (buffered; `text()`/`json<T>()` read it). `port` and status codes are `number` (TypeScript) / `u16` (Rust/C++), consistent with `response.status()`.

Response chain: `status(code)`, `header(name, value)`, `set/append/get/type/links/location/vary`, `cookie(name, value, { maxAge?, path?, domain?, secure?, httpOnly?, signed?, sameSite? }?)` (`maxAge` is **seconds**), `clearCookie(name, opts?)`, `redirect(url, status?)`, `sendFile(path)`, `render(view, locals?)`, `download(path, filename?)`, `attach(filename)`, `format(formats)`, then terminal `text(payload, status?)`, `json(payload, status?)`, or `send(status?)`. `write(string|Buffer)` + `end()` for chunked writes. **The first terminal response wins**; a second send does not start a new response. CORS preflights are answered by `cors` middleware only.

## 28.3 Router

```ts
import { Router } from "std/network";

const api = new Router();
api.get("/users/:id", (req, res) => res.json({ id: req.params.get("id") }));
api.any("/*", notFound);

const app = createServer();
app.use("/api", api);     // mounts at prefix; params/caseSensitive/strict options available
app.listen(8080);
```

Express-style: `:name` params via `req.params` (Map), `*` catch-all, nested `app.use("/prefix", router)`, `route()` chains, `all`, `handle/handler/mountMiddleware`, `strict`/`caseSensitive` options. Middleware factories `static(dir)`, `cors(options)`, `bodyLimit(n)`, `cookieParser(secret?)` are router-compatible; `cookieParser(secret)` additionally enables verified `signedCookies`. Body-parser factories (`json`, `urlencoded`, `text`, `raw`) and `httpError(status, message)` are namespace-corner spellings (`network.json(...)` via the default import) rather than named imports. `trustProxy` applies trusted `X-Forwarded-Proto` to `req.protocol` and trusted `X-Forwarded-For` to `req.ip`.

## 28.4 WebSocket client

```ts
import { WebSocket, AbortSignal } from "std/network";

const socket = new WebSocket("ws://127.0.0.1:9000/events");

socket.on("open", (): undefined => socket.send("hello"));
socket.on("message", (payload): undefined => {
  // string for text frames, Buffer for binary frames
  if (payload instanceof Buffer) { ... }
  else { console.log("text:", payload); }
});
socket.on("close", (code, reason): undefined => console.log("closed", code, reason));
socket.on("error", (err): undefined => console.error(err));

socket.send("ping");
socket.close(1000, "going away");
```

Wait for `"open"` before sending. Event callbacks: `open`, `message`, `close(code, reason)`, `error(err)`. One handler per event (later replaces earlier). Close when done — an open socket keeps event-loop work alive. Text frames → `string`; binary frames → `Buffer`. `ws://` and `wss://` are both supported; `wss://` verifies the server certificate like HTTPS.

## 28.5 WebSocket server

```ts
import { WebSocketServer } from "std/network";

const wss = new WebSocketServer({ port: 9000, path: "*", tls: { cert: "cert.pem", key: "key.pem" } });

wss.on("connection", (ws, request): undefined => {
  ws.on("message", (msg): undefined => {
    ws.send("echo: " + (typeof msg === "string" ? msg : "bytes"));
  });
});

// on shutdown:
wss.close();
```

`new WebSocketServer({ port, path?, tls? })`, default path `"*"`. Same WebSocket handle surface as the client. Pass `tls` (an inline `{ cert, key, ... }` object or a `tls({...})` value with PEM file paths) to serve `wss://`; without it, plain `ws://`.

## 28.6 Resource and error discipline

Close servers and sockets explicitly; do not start listeners as module-side effects — create and listen in the program's startup flow. Set timeouts on fetches that must have a deadline. Treat all network responses as untrusted input: check status codes, validate parsed JSON, validate query params. Network failures come from DNS, connection, timeout, protocol, or the remote server.

# 29. `std/io` — native non-blocking readiness (for native libraries)

```ts
import io from "std/io";
```

For libraries that own a socket fd and a non-blocking step API. `async` by itself does not make a blocking C call concurrent.

| Member | Signature | Effect |
|---|---|---|
| `io.waitReadable(fd)` | `Promise<undefined>` | Await until fd is readable |
| `io.waitWritable(fd)` | `Promise<undefined>` | Await until fd is writable |
| `io.pollUntilDone(getSocket, checkProgress)` | `Promise<undefined>` | Drive a native async operation to completion |
| `io.POLL_READING` | constant | Await `waitReadable(getSocket())` |
| `io.POLL_WRITING` | constant | Await `waitWritable(getSocket())` |
| `io.POLL_DONE` | constant | Resolve |
| `io.POLL_FAILED` | constant | Reject |

```ts
await io.pollUntilDone(
  () => nativeLib.socket(handle),
  () => nativeLib.progress(handle),   // one of the POLL_* constants
);
```

`fd` is an `int` on POSIX or the native `SOCKET` on Windows — keep it in the `number` range. The waiter suspends only the calling async function; other work continues on the loop. Rejection occurs on poll error, hangup/disconnect, invalid/closed descriptor, or descriptor closed while waiting. `getSocket()` is called every iteration (connects can change the fd). Many waiters may share one descriptor; a periodic validity check (100 ms) reports a locally closed descriptor. No cancellation token in the current surface. `std/io` and `std/network` share one libuv loop per thread (epoll on Linux, IOCP on Windows). Blocking C libraries without a pollable fd are a separate offload pool that is not yet available.

# 30. Diagnostics and debugging

## 30.1 Reading a diagnostic

Run `cp3 check` and read the **first** error first — later messages are often follow-ons from one bad inference. Diagnostics include a stable `CP3xxxx` code, severity, source path, line/column, message, and notes. An HTML help page may be written to the OS temp directory with a `file:` URL; it ranks three guide sections by text similarity. Run `cp3 docs` for the full bundled guide. Fix source/type errors before debugging a `try`/`catch` path.

## 30.2 Front end vs native stage

| Output clue | Stage | Meaning |
|---|---|---|
| `CP3xxxx` code + `.cp3` span | check (lexing/parse/resolution/sema/types) | CP3 rejected the source |
| Generated `.cpp` path + C++ diagnostic | native compile | Codegen, runtime headers, or toolchain issue |
| Undefined symbol / missing library | link | Runtime/native dependency missing |
| Stack trace / thrown value / exit code after launch | execution | Runtime condition in the built program |

`check` does not prove the C++ compile/link/execute will succeed. `run`/`build` do. For native failures, preserve both the CP3 source and the generated C++ location.

## 30.3 Inspecting generated code

```sh
cp3 build --show-runtime
```

Preserves runtime headers and translation units in `.cp3/build` for inspection. Useful when a performance claim depends on an optimization actually landing: look for the concrete loop in generated C++, then check the native compiler's vectorization remark points at that loop.

## 30.4 Test and verify paths

Use the repository runner for focused tests:

```sh
python tests/runner/run_tests.py --filter arrays-strings --no-bench
python tests/runner/run_tests.py --filter stdlib/03/process --jobs 1 --verbose
```

`mode: "check"` validates front-end behavior; `mode: "run"` validates native execution and output comparison. Negative cases pin diagnostics.

# 31. JavaScript / TypeScript → CP3 porting map

| JS/TS node pattern | CP3 equivalent |
|---|---|
| `console.log(...)` | `import console from "std/console"; console.log(...)` |
| `process.env.NAME` | `std/env.get("NAME")` or `process.env.get("NAME")` |
| `process.env` object access | `process.env` is a `Map` — use `get`/`has`/`set` |
| `fs.readFileSync(path)` | `fs.readFileSync(path)` — throws synchronously |
| `fs.readFile(path)` | `await fs.readFile(path)` — rejects on failure |
| `path.join(...)` | `import path from "std/path"; path.join(...)` |
| `path.resolve(...)` | `path.resolve(...)` |
| `os.platform()` / `os.cpus()` | `std/os` |
| `JSON.parse` / `JSON.stringify` | `import json from "std/json"` (`parse<T>`; no untyped parse) |
| `Buffer.from` / `.toString("hex")` | `import { Buffer } from "std/buffer"` |
| `crypto.randomBytes`, `crypto.createHash` | `std/crypto.randomBytes`, `std/crypto.sha256` |
| `URL` / `URLSearchParams` | `std/url` |
| `TextEncoder` / `TextDecoder` | `std/text` |
| `setInterval` / `setTimeout` / `clearTimeout` / `clearInterval` | Core global builtins, no import; `sleep(ms): Promise<undefined>` |
| `Promise.all` / `Promise.race` | supported combinators |
| `for...in` over objects | rejected — use `for...of`, `Object.keys` on static shapes, or `Map` |
| `Array.prototype.reduceRight` | `array.reduceRight(cb, initial?)` |
| `Object.keys` / `Object.entries` | `Object.keys(o)` / `Object.entries(o)` (static shapes) |
| `Object.fromEntries` / `Object.groupBy` | `Object.fromEntries` / `Object.groupBy` → `Map<string, …>` |
| `structuredClone(value)` | global `structuredClone(value)` (same type back) |
| `Map` / `Set` | `import { Map, Set } from "std/collections"` |
| `any` | rejected — use a concrete type or `object` |
| `void` return annotation | use `undefined` (CP3420) |
| Decorators on classes | not supported |
| Optional chaining on arbitrary values | supported on nullable receivers |
| `??` and `||` | both supported; they differ (`??` is nullish only) |
| `Array.of`, `Array.from` | supported |
| `Array.prototype.flat`, `unique`, `shuffle` | supported (`flat(depth?)`) |
| `Array.prototype.binarySearch` | supported (sorted input required) |
| `String.prototype.split` with regex | supported; captures included, byte offsets |
| `Intl` / `Date.parse` | `std/date` (ISO only for parsing) |
| `Math.random()` | `std/math.random` — **not** for secrets |
| `fetch` | `std/network.fetch` (HTTP only, beta) |
| DOM APIs | unavailable |
| npm `package` `require("x")` | not available — CP3 package or native interop |
| `child_process.exec` / `spawn` | `process.exec` / `process.spawn` |
| `process.stdout` stream | `process.stdout.write(text)` / `process.stderr.write(text)` / `process.stdin` |
| `require.main === module` | check `process.argv` for your entry script |
| `assert.deepEqual` / `util.promisify` | no equivalent in this surface |

Porting advice: classify each external API as a language feature, a `std/...` module, a CP3 package, or a native dependency. Replace at the module boundary, annotate exported signatures, and port failure paths deliberately. Match the failure mode (throw / reject / result object) per API.

# 32. When to use which collection

| Need | Type |
|---|---|
| Ordered, indexable sequence | `T[]` |
| Key/value lookup, dynamic keys | `Map<K, V>` |
| Distinct values | `Set<T>` |
| Bounded in-memory cache | `LRUCache<K, V>(capacity)` |
| FIFO queue | `Queue<T>` |
| LIFO stack | `Stack<T>` |
| Priority/scheduled work | `PriorityQueue<T>(comparator)` |
| Fixed-capacity rolling buffer | `RingBuffer<T>(capacity)` |
| Bit flags | `BitSet` |
| Keys with multiple values | `MultiMap<K, V>` |
| Shared mutable state across workers | `ConcurrentMap<K, V>` |
| Dense fixed-width numeric data | typed array over `ArrayBuffer` |
| Byte string / encoded payload | `std/buffer.Buffer` |

# 33. Further reading (the deeper pages)

- [Chapter 1 · The language](../01-language/00-overview.md) — bindings, types, functions, control flow, values, generics, modules, errors, native types
- [Chapter 2 · Runtime](../02-runtime/00-overview.md) — value model, escape analysis, ownership, closures, async/concurrency, threads, costs
- [Chapter 3 · Standard library](../03-standard-library/00-overview.md) — module index and import conventions
- [Chapter 4 · Compiler and tools](../04-compiler-cli/00-overview.md) — CLI, pipeline, optimization, diagnostics, toolchains, cache, tests, native interop
- [For JavaScript and TypeScript developers](03-javascript-typescript-developers.md) — this page
- [Create and run your first project](04-first-project.md) — scaffolding walkthrough
- [Performance, measured carefully](02-performance.md) — benchmark discipline
- [URL extensions](../03-standard-library/24-url-extensions.md) — WHATWG form-urlencoded and normalization details

When an API detail matters, the module page is authoritative. Compiler diagnostics, conformance tests under `tests/cases/`, and the implementation (`src/std/...`, `src/sema/...`, `src/codegen/...`) are the final word when prose and behavior disagree.
