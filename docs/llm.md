# CP3 — agent reference

Audience: AI agents writing, reading, modifying or debugging CP3 programs or the compiler. This is a compact catalog of the language and standard library as they exist. It does **not** explain implementation internals (no escape analysis, no lexer/pipeline details). Where this file and the installed compiler disagree, trust the compiler diagnostic and the corpus under `tests/cases`.

## 1. What CP3 is

- A TypeScript-syntax language compiled to native C++. No JavaScript runtime, no Node.js ABI, no DOM.
- Static typing; unsupported TypeScript forms are compile errors carrying a `CP` code (e.g. `CP3305`, `CP3401`).
- `number` is a 64-bit double. `string` is UTF-8 bytes. `boolean`, `null`, `undefined` are distinct.
- `cp3 check` shows frontend errors without building; `cp3 run` builds and executes.

## 2. CLI

Everyday: `cp3 run <file.cp3>` · `cp3 check <file.cp3>` · `cp3 build [file.cp3]` · `cp3 dev [--once]` · `cp3 init <name> [--yes]` · `cp3 fmt <file.cp3>` · `cp3 clean [--all]`.

| Command | Purpose |
| --- | --- |
| `check` | parse + resolve + type-check (no codegen) |
| `build` | generate C++ → compile → link |
| `run` | build if needed, execute, propagate exit code |
| `dev` | watch and rebuild/restart; `--once` single pass |
| `init` | scaffold an application or package |
| `fmt` | zero-config formatter |
| `clean` | delete `.cp3/build` and `.cp3/out` |

Toolchains & packages: `cp3 toolchain list|detect|use|clear` · `cp3 install [src]` · `cp3 publish [--dry-run]` · `cp3 recompile <pkg>` · `cp3 link|unlink <name>` · `cp3 exec <pkg> [args]`.

Editor & SDK: `cp3 lsp [--stdio]` · `cp3 dap [--stdio]` · `cp3 editor-plugin` · `cp3 native-install <sdk>` · `cp3 docs [--port n]`.

Exit codes: `0` success · `1` compile error · `2` usage error.

Build profiles: `--release` (default) · `--incremental` (fast dev, no LTO/PGO) · `--debug` (`-O0`) · `--no-lto` · `--no-native` · `--no-unroll` · `--opt-level <0-3|s|z>` · `--pgo <file>` · `--no-pgo` · `--toolchain <clang[@ver]|path>` · `--strict-toolchain` · `--verbose` · `--show-runtime`.

Installing the compiler from source: clone the repo; `cmake -S . -B build -G "MinGW Makefiles"` (Windows) or default generator on Linux; `cmake --build build --config Release`; resulting binary in `build/bin`. The packaged Windows installer (`cp3` / `cp3c.exe`) follows the same repo's release; check `cp3 version` for the installed build.

## 3. Project layout

```
myapp/
  package.json       # metadata only (not Node)
  src/index.cp3      # entry point
  .cp3/build/  .cp3/out/
```

`package.json` — minimal fields:

```json
{
  "name": "hello-app",
  "version": "0.1.0",
  "type": "application",        # or "package"
  "main": "src/index.cp3",
  "typeSystem": "typescript",   # "typescript" | "cpp" | "rust"
  "scripts": { "start": "cp3 run src/index.cp3" }
}
```

- `typeSystem` only chooses how unprefixed native scalar names resolve; backend is always C++.
- Explicit prefixed types override: `r_'i32'`, `c_'u64'`, `ts_'i64'`, etc.
- `cp3 install <source>` installs packages (`.cp3pak` / git URL / registry); imports then resolve from `.cp3/packages/<name>`.
- Never edit `.cp3/build/*.cpp`; fix CP3 source or the compiler.

## 4. Types

| CP3 | Meaning |
| --- | --- |
| `number` | 64-bit double |
| `string` | UTF-8 bytes |
| `boolean` | `true` / `false` |
| `null`, `undefined`, `void` | distinct; `void` = no useful return |
| `unknown` | forces narrowing; no runtime value |
| `T[]` | array |
| `[A, B]` | tuple (fixed length/types) |
| `A \| B` | union |
| `{ a: number, b: string }` | object literal type |
| interface / class / enum | user types |
| `Promise<T>` | async result |
| `r_'i32'`, `c_'i32'`, `ts_'i32'` | explicit native vocabulary flavours |

Type inference: `let x = "hi";` → `string`. Explicit annotation overrides. Native arithmetic follows CP3's own checked rules: operands narrower than `i32` promote to `i32`; mixed widths/signedness pick a common native type; `number` and native ints do not mix silently.

- Nullable class fields: `next: Node | null`, optional `next?: Node`. A member read through a nullable receiver without narrowing or `?.` is rejected (CP3401).
- Control-flow narrowing: `if (n !== null)`, `if (n)`, `while (n !== null)`, `&&`, `?:`, and early-return guards narrow identifiers and member fields (`n.next`) to non-null. Assignment to the narrowed target invalidates the narrowing.
- `expr!` asserts non-null at runtime: it throws a catchable `Error` on null.
- Definite assignment in class bodies: `value!: number`.
- `expr as Type` asserts a related type (class hierarchy / numeric); unrelated types are CP3401.

## 5. Bindings, scope, closures

```ts
const id = 1;          // never reassigned
let count = 0;         // reassignable
var legacy = "x";      // reassignable, hoisted like JS var
function add(a: number, b: number): number { return a + b; }
const mul = (a: number, b: number): number => a * b;
const double = (n: number) => n * 2;           // return inferred
async function fetch_(): Promise<number> { return 1; }
```

- Block-scoped `let`/`const`; `var` hoists and has function scope.
- Closures capture by reference where JS requires it (reassignable bindings live in a shared cell); `const` captures logically copy.
- Destructuring: `const [a, b] = arr;` · `const { x, y: z } = obj;` · `const { p = 1 } = obj;` · `const [first, ...rest] = arr;`
- Destructure tuples directly: `const [lo, hi] = os.loadavg();`
- Spread: `fn(...args)` · object/array literal spread where supported.

## 6. Operators & truthiness

`+ - * / % ** ++ -- += -= *= /=` · `< <= > >=` · `== != === !==` · `&& || !` · `& | ^ << >> >>>` · `?? ??=` · `?.` · `typeof` · `instanceof` · `in`.

- `&& || !` use JS truthiness (`0.0`, `""`, `false`, `null`, `undefined` are falsy; NaN is falsy).
- `??` triggers on `null` **or** `undefined`.
- `typeof` returns: `"number" | "string" | "boolean" | "undefined" | "object" | "function"`; `typeof null === "object"` (JS parity).
- `instanceof` compares against a statically known class hierarchy; class must appear as the right operand (identifier or `new`'ed expression handled by the checker).
- `for...in` does **not** exist; use `for...of` or typed APIs.
- `delete`, object enumeration of unknown keys, `with` — rejected.

## 7. Control flow

```ts
// conditions
if (cond) { } else if (other) { } else { }
const v = cond ? a : b;

// loops
for (let i = 0; i < n; i++) { }
for (const item of list) { }          // supported iterable shapes (arrays, collections)
while (cond) { }  //  do-while likewise

// break/continue incl. labelled loops
outer: for (const x of xs) { if (x) break outer; }

// switch: JS fallthrough with break
switch (kind) {
  case "a": one(); break;
  case "b": two(); break;             // no implicit break: fall-through allowed
  case "c": three(); break;
  default: fallback();
}

// destructuring defaults + rest
const { size = 0, ...spec } = opts;

// error handling
try { work(); } catch (e) { e.message; e.name; e.stack; e.cause; } finally { cleanup(); }
```

Only `Error` (or a subclass) may be thrown. `throw 404;` · `throw "x";` · `throw {};` — compile errors. `catch (e)` binds `Error` directly.

## 8. Classes

```ts
class Shape {
  readonly id: number;             // readonly = assign once only in ctor
  protected size: number = 0;
  static count: number = 0;        // class-level members
  #secret = "hidden";              // #private member (real privacy)

  constructor(id: number, public name: string, private label?: string) {
    this.id = id;                  // parameter properties: public/private/protected + readonly
  }

  static { Shape.count = 1; }       // static init block, runs once

  describe(): string { return this.#secret + this.name; }
  full(): string { return `${this.name}/${this.describe()}`; }
  get area(): number { return this.size * this.size; }          // getter
  set area(v: number) { this.size = sqrt(v); }                  // setter
}

class Square extends Shape {
  constructor(id: number, n: number) { super(id, "square"); this.size = n; }
  override describe(): string { return "Square(" + super.describe() + ")"; }
}

abstract class Base { abstract run(): void; }
```

- Visibility: `public` (default) · `private` · `protected`. `#name` is private at the language level.
- `override` is required to replace a base member; an override with no matching base member is a compile error.
- `extends` is native C++ inheritance; `super(...)` constructor call must precede touching `this` in a derived ctor.
- Interfaces: `interface Point { x: number; y: number }` — structural for literals, nominal-ish via declarations; class `implements` interfaces with conformance checks.
- `instanceof` works on base/derived handles (static id compare).
- `static { }` blocks run once at link/init time; `static` members are shared.
- No runtime decorators. Compile-time decorators (`@Name`) are allowed only as macros; runtime metadata tables are rejected.
- `extends expr` only when `expr` is compile-time foldable; otherwise compile error.
- Getters/setters use accessor member syntax; `get x() { }` / `set x(v) { }`.
- `weak` field modifier (manual escape hatch for reference cycles): a weak field does not keep its target alive and reads as `T | null` (narrow before use). Automatic cycle handling is not shipped: no GC, no cycle collector; the compiler's cycle-capability analysis is report-only (Stage 0 numbers: `docs/cycle-stage0.md`).

## 9. Enums

```ts
enum Level { Low = 1, High = 2 }        // number enum
enum Mood { Calm = "calm", Wild = "wild" }  // string enum
const l: Level = Level.High;
```

Backings must be uniform (number or string). Mixed numeric/string backing is a compile error.

## 10. Generics

```ts
function identity<T>(v: T): T { return v; }
class Box<T> { constructor(public value: T) {} }
function first<T>(xs: T[]): T | undefined { return xs[0]; }
```

- Angle-bracket type parameters on classes, functions, interfaces.
- Monomorphised at compile time; no runtime type erase of arguments into objects.
- Type parameters appear in signatures and bodies; inference from arguments where possible.

## 11. Modules & import rules

```ts
import console from "std/console";            // default (namespace) import
import math from "std/math";
import { env } from "std/env";                // named import
import { Map, Set } from "std/collections";
import { Buffer } from "std/buffer";
import { fetch, WebSocket, WebSocketServer, createServer } from "std/network";
import MyClass from "./my-class.cp3";         // relative default import
import { helper } from "./util.cp3";          // named import
import util from "./util.cp3";                // default + named bindings
```

Exports: `export function f() {}` · `export class C {}` · `export default class Default {}` · `export const x = 1;` · `export interface I {…}`

- std modules are part of the compiler registry; unknown modules/members are compile errors with a CP code.
- User packages install into `.cp3/packages/<name>`; `.cp3pak` archives carry a `main` and native bindings.
- Circulars: avoid; cp3 resolves modules statically.
- `.cp3pak` artifacts are not decompressed into your tree; compiler reads the archive.

## 12. Async / Promises

```ts
async function load(): Promise<string> {
  const res = await fetch("http://127.0.0.1:8080/health");
  if (!res.ok) throw new Error("unhealthy");
  return await res.text();
}
```

- `async`/`await` over the single runtime event loop (libuv-backed).
- Promise APIs: `resolve`, `reject`, `then`, `catch`, `finally`, `all`, `race`, `allSettled`, `any`, `withResolvers<T>()`.
- Coercion globals: `parseInt(s)`, `parseFloat(s)`, `Number(x)`, `String(x)`, `Boolean(x)`, `isNaN(x)`, `isFinite(x)`.
- Promise rejections are `Error` objects; `catch (e)` binds them.
- A top-level `await` is allowed in async entry points only if the entry is async; keep `await` inside async functions.
- Timers/network/fs-async suspend only the async function; other loop work continues.

## 13. Standard library (current, import form → key APIs)

### Built-in primitives (no import)
| API | Usage |
| --- | --- |
| `string` methods | `charAt`, `at`, `indexOf`, `lastIndexOf`, `startsWith`, `endsWith`, `includes`, `repeat`, `padStart`, `padEnd`, `trim`, `trimStart`, `trimEnd`, `slice`, `substring`, `replace`, `replaceAll`, `split`, `toUpperCase`, `toLowerCase`, `length`, `charCodeAt` |
| `number` statics/methods | `toFixed`, `toPrecision`, `toExponential`, `isInteger`, `isFinite`, `isNaN`, `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `EPSILON`, `MAX_VALUE`, `MIN_VALUE`, `POSITIVE_INFINITY`, `NEGATIVE_INFINITY`, `NaN` |
| `array` methods | `push`, `unshift`, `pop`, `shift`, `sort`, `fill`, `includes`, `indexOf`, `find`, `findIndex`, `findLast`, `findLastIndex`, `some`, `every`, `slice`, `map`, `filter`, `flat`, `flatMap`, `join`, `forEach`, `binarySearch`, `unique`, `shuffle`, `at`, `toSorted`, `toReversed`, `length` |
| Array callbacks | `(value, index, array)` — may declare any prefix |

### `console` — `import console from "std/console"`
`log`, `info`, `warn`, `error`, `assert`, `input(prompt?): Promise<string>`, `readKey(): Promise<string>`, `on/off(event, listener)`, `clear()`, `hide()/show()`

### `env` — `import { env } from "std/env"`
`env.get<T>(key)` · `env.get<T>(key, { default })` · `env.has(key)` · `env.load(path)`

### `process` — `import process from "std/process"`
`argv` · `env` (Map) · `exit(code)` · `exec(cmd, args): Promise<ExecResult>` · `spawn(cmd, args): SpawnHandle`
`cwd(): string` · `chdir(path)` (throws catchable) · `pid` · `ppid` · `uptime(): number` · `memoryUsage(): { rss, heapTotal, heapUsed, external, arrayBuffers }` (rss only; heap fields `0`) · `hrtime(): [number, number]`
ExecResult `{ stdout, stderr, exitCode }`; events `"data"`, `"stderr"`, `"exit"`. `SpawnHandle`: `on(...)`, `write()`, `kill()`.

### `fs` — `import fs from "std/fs"`
- Sync: `readFileSync` · `writeFileSync` · `appendFileSync` · `existsSync` · `mkdirSync` · `readdirSync` · `statSync` · `unlinkSync`
- Async (promise): `readFile` · `writeFile` · `appendFile` · `mkdir` · `readdir` · `stat` · `unlink`
- Streams: `readFileStream(path, chunkSize?)` · `writeFileStream(path)`
- `fs.stat(...)` → **FileStats** `{ size: number, isFile: boolean, isDirectory: boolean, modifiedAt: Date }`
- Sync calls throw synchronously; async calls reject promises — `await` them or attach `.catch`.
- `exists` is point-in-time; prefer performing the operation and handling its error for anything that matters.
- Paths are strings; compose with `path.join`.

### `path` — `import path from "std/path"`
`join`, `resolve`, `normalize`, `dirname`, `basename(p, ext?)`, `extname`, `isAbsolute`, `sep`, `delimiter`

### `os` — `import os from "std/os"`
`platform()` · `homedir()` · `tmpdir()` · `hostname()` · `cpus()` · `totalMemory()` · `freeMemory()` · `totalmem()` · `freemem()` · `loadavg(): [number,number,number]` · `networkInterfaces(): Array<[name, address, family, internal]>`

### `math` — `import math from "std/math"`
`PI`, `E`, `LN2`, `LN10`, `LOG2E`, `LOG10E`, `SQRT1_2`, `SQRT2` · `random()` · `min(...xs)` · `max(...xs)` · `pow(x,y)` · `gcd(a,b)` · `lcm(a,b)` · `clamp(x,min,max)` · `lerp(a,b,t)` · `sqrt`, `abs`, `floor`, `ceil`, `round`, `trunc`, `cbrt`, `log`, `log2`, `log10`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `exp`, `log1p`, `hypot(...xs)`, `sign`.

### `date` — `import date from "std/date"`
`Date.now()` · `Date.fromISOString(s)` · `toISOString()` · `toString()` · `getFullYear/getMonth/getDate/getHours/getMinutes/getSeconds()` · `addDays/addHours/addMinutes/addSeconds(n)` · `isBefore/isAfter(other)` · `diffMs(other)`

### `json` — `import JSON from "std/json"`
`JSON.parse<T>(text)` (throws catchable error on bad input) · `JSON.stringify(value, indent?)` (numeric indent pretty-prints)

### `crypto` — `import crypto from "std/crypto"`
`randomUUID()` · `randomBytes(n): Buffer` · `sha256(data)` · `sha512(data)` · `AES256(key, data)` · `AES256Decrypt(key, enc)`

### `buffer` — `import { Buffer } from "std/buffer"`
`Buffer.from(text, enc?)` · `Buffer.alloc(n)` · `.length` · `.toString(enc?)` · `.slice(a, b?)`
Encodings: `utf8`, `hex`, `base64`. Invalid hex/base64 throws catchable error.

### `collections` — `import { Map, Set } from "std/collections"`
Map: `set` `get` `has` `delete` `clear` `keys` `values` `entries` `forEach((value, key) => ...)` `size`
Set: `add` `has` `delete` `clear` `values` `forEach(value => ...)` `size`

### `network` — `import { fetch, WebSocket, WebSocketServer, createServer } from "std/network"`
- `fetch(url, init?): Promise<FetchResponse>` — `init`: `method`, `headers: Map<string,string>`, `body`, `timeout?` (0 = none). HTTP only (no TLS yet).
- Response: `ok`, `status`, `statusText`, `headers`, `text(): Promise<string>`, `arrayBuffer(): Promise<Buffer>`, `json<T>()`.
- WebSocket client: `new WebSocket(url)` — `on(event, handler)`, `send(str|Buffer)`, `close(code?, reason?)`; events: `open`, `message`, `close`, `error`.
- WebSocket server: `new WebSocketServer({ port, path? })`; `on("connection", handler)`.
- HTTP server: `createServer({ threads? })` — `get/post/put/delete/patch/head/options/any/use` — request: `method`, `path`, `params`, query Map, headers Map, `text(): Promise<string>`, `json<T>()`. Response chainable: `status(n)`, `header(k,v)`, terminal `text` / `json` / `send`; `write`, `end`.
- Route params: `:name` segments match one path segment (e.g. `/users/:id`); handlers run in registration order; middleware calls `next()` (a `Promise<void>`) to continue the chain; a chain that never responds returns 404.
- A non-2xx HTTP status still resolves to a response — check `ok` / `status`.

### `arrays` built-ins
`includes` · `indexOf` · `find` · `findIndex` · `findLast` · `findLastIndex` · `some` · `every` · `map` · `filter` · `flat` · `flatMap` · `forEach` · `slice` · `join` · `binarySearch` · `unique` · `shuffle` · `at` · `toSorted` · `toReversed`

### `strings` built-ins
UTF-8 conscious: `charAt`/`fromCodePoint` walk code points; `str[i]` is byte-oriented; `.length` is bytes.
Methods listed in section 13 first table.

### `typed-arrays` — `import "std/typed-arrays"` (built-in types)
`ArrayBuffer` · `DataView` · `Int8Array` `Uint8Array` `Uint8ClampedArray` `Int16Array` `Uint16Array` `Int32Array` `Uint32Array` `Float32Array` `Float64Array` `BigInt64Array` `BigUint64Array`. Fixed-width, raw buffer.

### `regular expressions` — built-in `RegExp`
Literal `/pat/imsuyv` or `new RegExp("pat", "i")`.
Flags: `g` global (exec/test keep position) · `i` case-insensitive · `m` multiline · `s` dotAll · `u` UTF · `v` UTF (current) · `y` sticky · `d` accepted (indices not exposed).
Members: `test` · `match` · `exec` · `source` · `flags`.

### `io` — `import io from "std/io"`
`io.waitReadable(fd)` · `io.waitWritable(fd)` · `io.pollUntilDone(getSocket, getProgress)` · `io.POLL_READING | POLL_WRITING | POLL_DONE | POLL_FAILED`

## 14. Diagnostics

- `cp3 check <file>` → list of errors with file:line:col, code `CPdddd`, message, optional `help: file:///...html`.
- `CP3305` — unknown member on a closed surface (std namespaces, object shapes…); candidates listed.
- `CP3401` — type mismatch / invalid operand / misuse.
- `CP3406 / CP3403` — arity & argument mismatch.
- `CP3704` — invalid override; `CP3707` — unsupported construct; `CP3802` — undeclared property.
- `cp3 help` shows commands; `cp3 docs` serves manuals locally.

## 15. Agent cheat-sheet: common mistakes

- No `for...in` / object enumeration — use typed APIs.
- `throw` only `Error` subclasses; `catch (e)` binds `e: Error`.
- `await` must be inside `async` functions; top-level await is limited.
- `.length` on strings is bytes; use `charAt` / `for...of` for code points.
- `for...of` over arrays/collections works; arbitrary JS iterable protocol is **not** assumed.
- `Map` / `Set` are insertion-ordered. `entries()` yields `[k, v]` tuples.
- `process.exec` buffers output; use `spawn` + events for streams.
- `JSON.parse<T>` — always pass the type argument to get checked parsing.
- Missing member on a std namespace = compile error with a code and a candidate list; check the installed compiler's completion before assuming Node parity.
- `number` prints via ECMAScript rules (`-0` prints `0`, NaN prints `NaN`).
- `instanceof` right side must be a class name, not a value.
- `async` primitives are CP3's own, not Node's — no `process.stdout.write`; use `console.log`/`console.error`.
- Unsupported TypeScript features (runtime decorators, `Reflect`, runtime `extends expr`, `keyof`/`infer`/conditional mapped types, etc.) are rejected with an explanatory diagnostic; rewrite in the supported subset instead of fighting the checker.

## 16. Quick recipes

```ts
import console from "std/console";
import fs from "std/fs";

const text = await fs.readFile("data.txt", "utf8");
console.log(text.trim());

// Run a child and capture output
const result = await process.exec("git", ["status", "--short"]);
console.log(result.exitCode, result.stdout.trim());

// Simple HTTP server
import { createServer } from "std/network";
const server = createServer();
server.get("/health", (req, res) => { res.status(200).text("ok"); });
server.listen(8080);

// fs + JSON
const config = await fs.readFile("config.json", "utf8");
const parsed = JSON.parse<{ name: string }>(config);
console.log(parsed.name);

// Env with a default
import { env } from "std/env";
console.log(env.get("HOME", { default: "." }));

// Spawn with live output
const child = process.spawn("git", ["status"]);
child.on("data", (chunk) => console.log(chunk));
child.on("exit", (code) => console.log("exit", code));
```

## 17. Checking and verifying changes

After editing CP3 source or the compiler:

```sh
cp3 check src/file.cp3
# full compiler corpus (absolute path required):
python tests/runner/run_tests.py --cp3c build/bin/cp3c.exe --cases "<abs path>" --no-bench --no-progress --jobs 4
```

Conformance cases live under `tests/cases/<area>`; negative cases pin the diagnostic. Keep this file (docs/llm.md) in sync with the supported surface whenever CP3 changes.

## 18. Diagnostics quick reference

`cp3 check` reports `<file>:<line>:<col>: error: CPdddd: <message>` (plus a `help:` URL pointing at the generated per-error page).

| Code | Meaning |
| --- | --- |
| `CP3305` | Unknown member on a closed surface; message lists the valid candidates. |
| `CP3306`/`CP3401` | Type mismatch (operand, argument, assignment, return). |
| `CP3403` | Arity mismatch; wrong number or spread placement of arguments. |
| `CP3406` | Invalid left or right operand for an operator (e.g. `instanceof`). |
| `CP3704` | `override` member has no matching base member / wrong kind. |
| `CP3705` | Duplicate member declaration. |
| `CP3708` | Computed class member name not allowed (needs literal). |
| `CP3802` | Undeclared property on a class / undeclared member. |
| `CP3801` | `delete` is not available in CP3. |
| `CP3xxx` | `help:` URL on the line; or `cp3 docs` and the diagnostics catalog page for full text. |

When a program is rejected, read the first diagnostic, not the last; later ones are usually cascades.

## 19. Failure model quick reference

| Style | Where | What happens |
| --- | --- | --- |
| Throws synchronously | parse errors in startup helpers, `fs.*Sync`, `env.get` miss, invalid native handles | `try/catch` recovers |
| Rejects the promise | `fs.readFile` (etc.), `fetch`, `console.input`, crypto misuse, network failures | `await`/`catch`; unhandled rejection reports and exits |
| Exits the process | `process.exit(code)`, fatal runtime errors in the ABI | not catchable |
| Compile error | `cp3 check` rejects invalid constructs | fix the source |
| `NaN` result | `parseFloat("abc")`, `0/0`, domain violations | no exception |
| `undefined` result | collection miss (`map.get(k)`), missing optional field | `x?.y`, `??`, `=== undefined` |
| `null` result | explicit `null` values, `catch` free variables' empty state | `== null` covers both (undefined and null) |

## 20. Native packages & `.cp3pak`

- `cp3 install <path|url>` installs a package directory or `.cp3pak` archive into the project; it records the dependency in `package.json`/`.cp3/lock.json`.
- `cp3 publish` packs a project as an application or library; `publish --dry-run` prints the planned archive contents.
- `cp3 recompile <pkg>` adds a prebuilt binary for the current platform to the archive (used by packages shipping native libraries).
- `cp3 link <dir>` / `cp3 unlink <name>` manage local development links to a package.
- `.cp3pak` carries `package.json`, source (or precompiled library), `native.json`, headers, import libraries and the recorded sources for native bindings; installing hoists them into `.cp3/native/<id>/`.
- `cp3 native-install <sdk>` (libclang required) generates the declaration index for a native SDK; the generated `index.cp3` then type-checks native calls like ordinary cp3 code.

## 21. Reproducing a minimal program

```sh
cp3 init demo --yes
cd demo
cp3 check src/index.cp3
cp3 run src/index.cp3
```

`src/index.cp3` prints `Hello from demo!`. Add `.cp3` modules under `src/` and import them relatively (`import { helper } from "./util.cp3";`) or via installed packages.

## 22. Native types in detail

`typeSystem` selects how unprefixed native scalar names resolve. It never changes the backend (always C++).

| `typeSystem` | example unprefixed names |
| --- | --- |
| `typescript` (default) | `number`, `string`, `boolean`, and the full TS surface |
| `cpp` | `int32_t`, `uint64_t`, `double`, `bool`, `string`, ... |
| `rust` | `i32`, `u64`, `f64`, `bool`, `String`, ... |

Explicit prefixed types are valid anywhere a native scalar fits:

- `r_'i32'`, `r_'f64'` — Rust vocabulary regardless of `typeSystem`
- `c_'int32_t'`, `c_'double'` — C++ vocabulary regardless of `typeSystem`
- `ts_'number'` — TypeScript vocabulary (explicit) regardless of `typeSystem`

Native ints print exactly. Arithmetic on narrower-than-`i32` operands promotes to `i32` (so `u8 + u8` is `i32`). Native values do not mix freely with nonliteral `number`: pass a fitting literal where supported, or keep producer/consumer on the same native type. With `strictNumericCasts: true` in `package.json`, assigning `number` to a native type is rejected and mixed native arithmetic is rejected; tests live in `tests/cases/types/03/native_strict`.

`expr as Type` is a compile-time-checked type assertion (related class types;
numeric conversions). Unrelated types are rejected with CP3401. `r_'i32'(x)` still does not exist. For native types without `as`, initialise from a fitting literal or another native `i64`. See `docs/content/01-language/07-native-types/03-numeric-rules.md`.

## 23. Reading the corpus for semantics

`tests/cases/<area>/<topic>/<case>/{main.cp3, output.json}` — output.json pins stdout bytes and exit code. Negative cases pin diagnostic code and (optionally) line/message. Search for any construct's semantics before assuming JavaScript behavior; if no case exists, prefer the simpler rewrite.
