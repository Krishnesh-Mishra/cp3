# C+++ (.cp3) - Standard Library

Companion spec to the core language README. Tracked separately so the core language spec isn't blocked on every std function being named.

## Status

**Beta One.** Every module below now has a nailed-down function surface - not just shape/examples. Build against the signatures in this document directly. **As of the beta-one sign-off there is no open item left at the bottom** - the last one (`Buffer` encodings) was decided - so the standard library is implementation-ready.

**A note on notation:** every module below (`console`, `Math`, `fs`, etc.) is shown as `const moduleName: { ...methods... }` - this is describing the *shape of the plain object* that `import x from "std/x"` hands you, exactly like real JS's `console`/`Math`/`JSON` are just objects with methods on them. It is documentation notation only, not new cp3 syntax - cp3 has no `namespace` keyword and this doc doesn't introduce one. Where a signature line still says e.g. `randomUUID(): string;` inside one of these blocks, read it the same way you'd read a method on any other object type.

## Architecture (see `README.md` §20 for full detail)

The standard library is authored in cp3 itself, not hand-written C++. Where a module genuinely needs to reach real OS/C++ functionality underneath, it does so through the same import native bridge used for wrapping any external C++ library.

Bundled with the compiler - std/* modules are never a package to cp3 install. No package.json entry, no registry fetch, no .packages/ folder involved. Always available, same as std:: needs no setup in C++.

**`std/network` - reinstated for beta one, evolved from an initial `std/server`.** An earlier draft removed server support entirely on the reasoning that it would mean wrapping Drogon (large, template-heavy, a bad fit for beta-one's native-interop scope, which excludes templates - see `README.md` §19). That reasoning held against Drogon specifically, but the capability itself is back, built differently - and consolidated further than the first reinstatement: `fetch`, an HTTP server, and WebSocket support now live together in one `std/network` module, backed by libuv (already required the moment `async`/`await` is used at all) for the client side and **uWebSockets** for the server/WebSocket side, with routing/request/response logic written in ordinary cp3. Full module spec below.

`std/webview` remains dropped, unaffected by the above - could still exist later as a third-party `.cp3pak` package.

## Built into the language - no import needed

These are core language features, not standard library modules, because they operate on core types (array, string) or the core runtime (the event loop) that every program uses constantly:

- Array methods - push, pop, map, filter, reduce, sort, find, includes, flat, etc.
- String methods - split, trim, replace, padStart/padEnd, toUpperCase/toLowerCase, etc.
- Regex - native pattern literal syntax (/pattern/flags), like JS - not an imported module.
- setTimeout / setInterval / clearTimeout / clearInterval - global builtins, no import, tied directly to the language's own event loop (`README.md` §14), same as JS.
- sleep(ms) - a global builtin returning `Promise<void>`, a thin convenience wrapper over `setTimeout` for the extremely common `await sleep(1000)` pattern. Not its own mechanism - same event loop, same guarantees.
- parseInt(str) / parseFloat(str) - global builtins (matching JS's own placement as globals, not string methods), backed directly by `std::strtol`/`std::strtod`. Return `number` (always a 64-bit double, `README.md` §3), same as every other numeric value in the language.
- Object.keys(obj) / Object.values(obj) / Object.entries(obj) - compile-time macros expanding directly from a plain object's known field list, not runtime reflection (`README.md` §8 - this is the direct resolution of "how do these work given `for...in` on plain objects is banned").

## Imported std modules

Everything below requires an explicit import, since each one reaches beyond core language types into OS/runtime functionality.

### console

JS-style console object - the single core I/O surface.

```cp3
const console: {
  log(...args: Printable[]): void;    // → stdout
  info(...args: Printable[]): void;   // → stdout
  warn(...args: Printable[]): void;   // → stderr
  error(...args: Printable[]): void;  // → stderr
  input(prompt?: string): Promise<string>;   // async read of one line from stdin
  clear(): void;                              // clears the terminal screen
  assert(condition: boolean, ...args: Printable[]): void;  // logs to stderr only if condition is false; does not throw or exit
  readKey(): Promise<string>;                 // async read of a single keypress, including arrow keys, for interactive CLI menus
};
```

```cp3
import console from "std/console";

console.log("hello");
console.warn("careful");
console.info("fyi");
console.error("failed:", err);
let name = await console.input("Your name: ");
console.clear();
console.assert(value > 0, "value should be positive, got:", value);
let key = await console.readKey();   // e.g. "ArrowUp", "ArrowDown", "Enter", or a plain character
```

**Routing, decided:** `.log` / `.info` → stdout. `.warn` / `.error` → stderr. Matches Node exactly - no ambiguity left here.

`console.log(...args)`'s variadic parameter list is not a runtime heterogeneous array - it compiles to a zero-cost C++ variadic template, monomorphized per call site (the same mechanism generics already use, `README.md` §16), since the concrete argument types are always known at compile time. This matters for how `Printable` actually gets resolved, split two ways:

- **Built-in primitive types** (`number`, `string`, `boolean`, arrays of them) are formatted by routing directly into `fmt` (the library already backing `console.log`, `CLI-README.md` §3) - no `toString()` call, no synthesized method, no interface check at all. This is the common case, and it's genuinely free.
- **User-defined classes** *may* implement `toString(): string` - an ordinary structural interface (`README.md` §15) - to control their own printed form. If a class never implements it, printing it falls back to a plain default representation, generated **only for the specific classes that are actually ever passed to `console.log` (or `.warn`/`.info`/`.error`) somewhere in the program** - the same usage-based reification approach already used for `JSON.parse<T>` (`README.md` §16), not a `toString()` proactively synthesized for every class in the codebase whether it's ever printed or not. `Printable` is this interface, used as the compile-time constraint on the variadic template - not a special compiler-only case, and not a reintroduction of `any`.

- `.input` returns a `Promise<string>` that resolves once a line (up to `\n`) has been read; on EOF, it resolves with an empty string rather than rejecting. It must not be a blocking/synchronous call - cp3's async model is a single event loop (`README.md` §14), so a truly blocking stdin read would stall every other pending async task in the program, not just the input call. This keeps `console.input` consistent with every other I/O primitive in std (`fs.readFile`, `process.exec`), which are already Promise-based for the same reason.
- `.assert` never throws and never exits the program - it's a diagnostic aid (log-if-false), not a control-flow mechanism; reach for `throw` if a failed condition should actually stop execution.
- `.readKey` is async for the same reason `.input` is - it puts the terminal into raw mode via libuv's TTY handling and resolves with the next keypress (a named key like `"ArrowUp"`/`"Enter"`, or the literal character typed), without blocking the event loop while waiting.

### JSON

```cp3
const JSON: {
  stringify<T>(value: T, indent?: number): string;
  parse(text: string): unknown;   // dynamic value; narrow before use
  parse<T>(text: string): T;      // validated against T's shape at runtime; throws on mismatch
};
```

```cp3
import JSON from "std/json";

let s = JSON.stringify(obj);
let obj = JSON.parse(s);
```

`unknown` (not `any`) is the return type of the untyped `.parse` - it must be narrowed (via `typeof`, a type guard, or an explicit cast) before its fields can be accessed, consistent with the "no `any`" design goal (§1 of the core README). Reaching for `.parse` without `<T>` should be the exception, not the default path - prefer `.parse<T>` wherever the expected shape is known.

Generic `.parse<T>` with runtime shape validation, TS-flavored but with a real runtime check:

```cp3
try {
  let user = JSON.parse<User>(`{ "id": 1, "name": "John" }`);
} catch (e) {
  console.log("Bad JSON shape:", e);
}
```

If the parsed JSON's actual shape doesn't match `T`, `.parse<T>` throws - an ordinary catchable error, same as any other throw/catch (`README.md` §7), not a new error mechanism. Real engineering requirement this creates: validating against `T` at runtime means `T`'s shape has to exist at runtime at all - unlike C++ templates, which are normally fully erased at compile time. This means classes/interfaces used with `.parse<T>` need reified type metadata kept around by the compiler. **Decided (`README.md` §23):** reification is *usage-based*, not universal - the compiler emits a shape descriptor (field names + type tags) only for the specific classes/interfaces that actually appear as `T` in a `JSON.parse<T>` call somewhere in the compiled program. A type never used with `.parse<T>` stays fully compile-time-erased, exactly like a plain C++ template - there is no blanket "all generics become reified" cost.

### Math

Node/JS-style.

```cp3
const Math: {
  PI: number;
  E: number;

  random(): number;                    // [0, 1)
  sqrt(x: number): number;
  pow(base: number, exponent: number): number;
  abs(x: number): number;
  floor(x: number): number;
  ceil(x: number): number;
  round(x: number): number;
  trunc(x: number): number;
  min(...values: number[]): number;
  max(...values: number[]): number;
  log(x: number): number;              // natural log
  log2(x: number): number;
  log10(x: number): number;
  sin(x: number): number;
  cos(x: number): number;
  tan(x: number): number;
  cbrt(x: number): number;                        // cube root
  gcd(a: number, b: number): number;
  lcm(a: number, b: number): number;
  clamp(x: number, min: number, max: number): number;
  lerp(start: number, end: number, t: number): number;   // linear interpolation
};
```

```cp3
import Math from "std/math";

Math.random();
Math.sqrt(25);
Math.pow(2, 8);
Math.min(1, 2);
Math.max(1, 2);
Math.PI;
Math.clamp(150, 0, 100);   // 100
Math.lerp(0, 10, 0.5);     // 5
```

Backed directly by `<numeric>`/`<cmath>` - plain standard C++, not a linear-algebra library like Eigen (which is for matrices/vectors, a different problem than these scalar helpers). `gcd`/`lcm` operate on `number` (a 64-bit double, `README.md` §3) rather than a true integer type, matching every other `Math` function's type - reaching for an exact-integer `gcd` on very large values means using the `r_'...'`/`c_'...'` integer escape hatches directly (`README.md` §3), same as any other case needing real integer semantics.

### fs

Node-style naming, with sync, async, and streaming variants:

```cp3
interface FileStats {
  size: number;
  isFile: boolean;
  isDirectory: boolean;
  modifiedAt: Date;
}

interface ReadStream {
  on(event: "data", listener: (chunk: Buffer) => void): void;
  on(event: "end", listener: () => void): void;
  on(event: "error", listener: (err: unknown) => void): void;
}

interface WriteStream {
  write(chunk: string | Buffer): void;
  end(): void;
  on(event: "error", listener: (err: unknown) => void): void;
}

const fs: {
  // sync - throws on failure
  readFileSync(path: string, encoding?: string): string;
  writeFileSync(path: string, data: string): void;
  appendFileSync(path: string, data: string): void;
  existsSync(path: string): boolean;
  mkdirSync(path: string, recursive?: boolean): void;
  readdirSync(path: string): string[];
  statSync(path: string): FileStats;
  unlinkSync(path: string): void;

  // async - Promise-based, rejects on failure
  readFile(path: string, encoding?: string): Promise<string>;
  writeFile(path: string, data: string): Promise<void>;
  appendFile(path: string, data: string): Promise<void>;
  mkdir(path: string, recursive?: boolean): Promise<void>;
  readdir(path: string): Promise<string[]>;
  stat(path: string): Promise<FileStats>;
  unlink(path: string): Promise<void>;

  // streaming
  readFileStream(path: string, chunkSize?: number): ReadStream;
  writeFileStream(path: string): WriteStream;
};
```

```cp3
import fs from "std/fs";

let contents = fs.readFileSync("file.txt");   // sync
fs.writeFileSync("file.txt", "data");
fs.existsSync("file.txt");

let contents = await fs.readFile("file.txt"); // async, promise-based
let stream = fs.readFileStream("bigfile.txt"); // streaming reads
```

All `*Sync` functions throw a catchable error on failure (`README.md` §7 - plain `try`/`catch`, no separate exception hierarchy). All async functions reject their promise the same way, so `await` inside `try`/`catch` works exactly as a JS developer would expect.

### network

Unified networking module - JS-native `fetch`, WebSockets, and an HTTP server, all under one import, backed internally by **uWebSockets** (built on uSockets) for the server/WebSocket side. Replaces what earlier drafts of this doc had as two separate pieces (an axios-style `std/fetch` module, and a `std/server` module with no WebSocket support) - consolidated here because "everything network-related" being one import removes a question an implementer would otherwise have to guess at (why is `fetch` a separate module from the HTTP server it talks to?).

#### `fetch` - exact JS shape, not an axios-style wrapper

An earlier draft gave `fetch` an axios-style `.get()`/`.post()`/`.custom()` shape. That's replaced here with the real JS `fetch(url, options)` signature directly - a JS developer's existing `fetch` snippets should paste into cp3 with zero adaptation:

```cp3
interface RequestInit {
  method?: string;                 // "GET", "POST", etc. - default "GET"
  headers?: Map<string, string>;
  body?: string | Buffer;
  timeout?: number;                // milliseconds
}

interface FetchResponse {
  ok: boolean;                     // true for 2xx
  status: number;
  statusText: string;              // "OK", "Not Found", etc.
  headers: Map<string, string>;

  text(): Promise<string>;
  json<T>(): Promise<T>;           // same runtime shape validation as JSON.parse<T>
  arrayBuffer(): Promise<Buffer>;  // for binary downloads
}

function fetch(url: string, init?: RequestInit): Promise<FetchResponse>;
```

```cp3
import { fetch } from "std/network";

let res = await fetch("https://api.example.com/data", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ user: "alice" })
});

if (res.ok) {
  let data = await res.json<User>();
}
```

A non-2xx response does **not** throw - check `.ok`/`.status` yourself, same as real JS `fetch`. Only a network-level failure (DNS, timeout, connection refused) rejects the promise. `fetch` hooks directly into the existing libuv event loop for DNS/TCP (`README.md` §14) - it doesn't need uWebSockets at all, since that library's real value is on the server side; a plain client request is just libuv.

#### WebSocket / WebSocketServer - backed by uWebSockets

```cp3
class WebSocket {
  constructor(url: string);

  send(data: string | Buffer): void;
  close(code?: number, reason?: string): void;

  on(event: "open", listener: () => void): void;
  on(event: "message", listener: (data: string | Buffer) => void): void;
  on(event: "close", listener: (code: number, reason: string) => void): void;
  on(event: "error", listener: (err: unknown) => void): void;
}

class WebSocketServer {
  constructor(options: { port: number, path?: string });

  on(event: "connection", listener: (ws: WebSocket, req: HttpRequest) => void): void;
  close(): void;
}
```

```cp3
import { WebSocket, WebSocketServer } from "std/network";

// Client:
let ws = new WebSocket("ws://localhost:8080");
ws.on("open", () => ws.send("Hello Server!"));
ws.on("message", (msg) => console.log("Got:", msg));

// Server:
let wss = new WebSocketServer({ port: 8080 });
wss.on("connection", (ws) => {
  ws.on("message", (msg) => {
    ws.send("echo: " + msg);
  });
});
```

#### HTTP server - Express-like API, with middleware and streaming

```cp3
interface HttpRequest {
  method: string;
  path: string;
  params: Map<string, string>;    // route params, e.g. :id
  query: Map<string, string>;     // query string, e.g. ?key=value
  headers: Map<string, string>;
  body: ReadStream;                // the raw request body as a stream - same interface as fs.readFileStream, for large uploads without buffering the whole thing into RAM

  json<T>(): Promise<T>;           // buffers and parses the body as JSON; fine for typical API payloads, avoid for large uploads - use .body directly instead
  text(): Promise<string>;         // buffers the body as text; same caveat as .json<T>()
}

interface HttpResponse {
  status(code: number): HttpResponse;              // chainable
  header(key: string, value: string): HttpResponse; // chainable

  json<T>(data: T, status?: number): void;
  text(data: string, status?: number): void;

  write(chunk: string | Buffer): void;   // stream data out progressively, e.g. for large files
  end(): void;                           // close a streamed response

  send(status?: number): void;
}

type NextFunction = () => Promise<void>;
type Middleware = (req: HttpRequest, res: HttpResponse, next: NextFunction) => void | Promise<void>;
type RouteHandler = (req: HttpRequest, res: HttpResponse) => void | Promise<void>;

const network: {
  createServer(): HttpServer;
};

interface HttpServer {
  use(handler: Middleware): void;   // global middleware - auth, logging, CORS, etc.

  get(path: string, handler: RouteHandler): void;
  post(path: string, handler: RouteHandler): void;
  put(path: string, handler: RouteHandler): void;
  delete(path: string, handler: RouteHandler): void;

  listen(port: number, callback?: () => void): void;
}
```

```cp3
import { createServer } from "std/network";

const app = createServer();

app.use(async (req, res, next) => {
  let token = req.headers.get("authorization");
  if (!token) {
    res.status(401).json({ error: "Unauthorized" });
    return;   // stops the chain - next() is never called
  }
  await next();   // continues to the route
});

app.get("/users/:id", async (req, res) => {
  let user = await getUserById(req.params.get("id"));
  if (!user) {
    res.json({ error: "not found" }, 404);
  } else {
    res.json(user);
  }
});

app.get("/video", async (req, res) => {
  res.header("Content-Type", "video/mp4");
  let stream = fs.readFileStream("movie.mp4");
  stream.on("data", (chunk) => res.write(chunk));
  stream.on("end", () => res.end());
});

app.listen(3000, () => {
  console.log("Server running on http://localhost:3000");
});
```

**Every request handler's errors are caught by `std/network` itself, never left to become a program-level unhandled rejection.** A handler that throws (sync or via a rejected `await`) gets turned into a plain `500` response for that one request - the server keeps running and every other in-flight request, and every open WebSocket connection, is unaffected. This is a deliberate, documented exception to `README.md`'s "unhandled rejection kills the process" default (§14) - the right boundary for a module explicitly built to keep running across many independent, unrelated pieces of work. A middleware calling `next()` propagates the same way - a rejected `next()` is caught at the same boundary.

**Included in beta one:** `fetch` (exact JS shape), `WebSocket`/`WebSocketServer`, `.get`/`.post`/`.put`/`.delete`, route params (`:id`), query string parsing, JSON/text/streaming request and response bodies, basic middleware (`app.use`), status codes and headers, `.listen(port)`, async handlers, and the per-request 500-on-throw behavior above.

**Explicitly out of scope for beta one** (not started, not partially done - don't build against these): nested/sub-routers, regex path matching, HTTPS/TLS (would mean wrapping OpenSSL/mbedTLS - a real, separate scope of work and security liability), server-side template engines, `multipart/form-data` file-upload parsing, response compression (gzip). Every one of these is a realistic third-party `.cp3pak` addition post-beta-one (an eventual `@cp3/tls`, `@cp3/multer`, `@cp3/compression`, etc.), not forgotten - matching how Node ships a barebones `http` module and lets the community build Express, or how Go ships barebones `net/http` and lets the community build Gin.

### Date

Immutable - every "change" operation returns a new Date rather than mutating in place, deliberately avoiding JS's well-known confusing/mutable Date behavior (similar in spirit to Rust's chrono or JS's newer Temporal proposal).

```cp3
class Date {
  static now(): Date;
  static fromISOString(s: string): Date;

  toISOString(): string;
  toString(): string;

  getFullYear(): number;
  getMonth(): number;     // 0-indexed, matches JS
  getDate(): number;
  getHours(): number;
  getMinutes(): number;
  getSeconds(): number;

  addDays(n: number): Date;
  addHours(n: number): Date;
  addMinutes(n: number): Date;
  addSeconds(n: number): Date;

  isBefore(other: Date): boolean;
  isAfter(other: Date): boolean;
  diffMs(other: Date): number;   // this - other, in milliseconds
}
```

```cp3
import Date from "std/date";

let now = Date.now();
let tomorrow = now.addDays(1);   // returns a NEW Date; 'now' itself is untouched
```

### process

Node-style, with the buffered-vs-streaming split matched exactly to Node's own exec/spawn distinction:

```cp3
interface ExecResult {
  stdout: string;
  stderr: string;
  exitCode: number;
}

interface SpawnHandle {
  on(event: "data", listener: (chunk: string) => void): void;    // stdout chunks
  on(event: "stderr", listener: (chunk: string) => void): void;  // stderr chunks
  on(event: "exit", listener: (code: number) => void): void;
  write(input: string): void;   // write to the child's stdin
  kill(): void;
}

const process: {
  argv: string[];
  env: Map<string, string>;
  exit(code: number): void;

  // buffered - resolves once, after the process finishes; rejects if the process can't be started at all
  exec(cmd: string, args: string[]): Promise<ExecResult>;

  // streaming - returns immediately with a live handle
  spawn(cmd: string, args: string[]): SpawnHandle;
};
```

```cp3
import process from "std/process";

process.argv;
process.env;
process.exit(0);

// buffered - resolves once, with the full result, after the process finishes
let result = await process.exec("git", ["status", "--porcelain"]);

// streaming - returns an event-emitting handle for long-running/streaming output
let p = process.spawn("git", ["log", "--follow"]);
p.on("data", (chunk) => console.log(chunk));
p.on("exit", (code) => console.log("done:", code));
```

`exec` deliberately does **not** reject on a non-zero exit code - check `result.exitCode` yourself. A rejection is reserved for the command failing to launch at all (bad path, permissions, etc.), not for the command running and simply returning failure.

### path

Node-style path utilities.

```cp3
const path: {
  sep: string;   // "/" or "\" depending on platform

  join(...parts: string[]): string;
  resolve(...parts: string[]): string;
  extname(p: string): string;
  basename(p: string, ext?: string): string;
  dirname(p: string): string;
  isAbsolute(p: string): boolean;
  normalize(p: string): string;
};
```

```cp3
import path from "std/path";

path.join("a", "b", "c.txt");
path.resolve("./relative/path");
path.extname("file.txt");   // ".txt"
```

### os

Node-style OS info, thin wrapper over OS calls.

```cp3
const os: {
  platform(): string;   // "windows" | "macos" | "linux"
  homedir(): string;
  tmpdir(): string;
  hostname(): string;
  cpus(): number;         // logical core count
  totalMemory(): number;  // bytes
  freeMemory(): number;   // bytes
};
```

```cp3
import os from "std/os";

os.platform();
os.homedir();
os.cpus();
os.tmpdir();
```

### events

JS-style EventEmitter. Pure cp3 - no native dependency at all, since this is just a data structure (event name → list of listener callbacks), not something that needs to reach the OS.

```cp3
class EventEmitter {
  function on(event: string, listener: (...args: unknown[]) => void): void;
  function once(event: string, listener: (...args: unknown[]) => void): void;
  function off(event: string, listener: (...args: unknown[]) => void): void;
  function emit(event: string, ...args: unknown[]): void;
}
```

```cp3
import { EventEmitter } from "std/events";

let emitter = new EventEmitter();
emitter.on("greet", (name) => console.log("hello", name));
emitter.emit("greet", "world");
emitter.off("greet", handler);
```

Every other std module's event-emitting handle (`fs`'s `ReadStream`/`WriteStream`, `process`'s `SpawnHandle`, `worker`'s error surface) follows this same `.on`/`.off`/`.emit` shape for consistency, even where it isn't literally built on top of this class internally.

### crypto

libsodium-backed - chosen over raw OpenSSL for a smaller footprint and a "hard to misuse" API, consistent with the project's broader safety-by-default stance.

```cp3
const crypto: {
  randomUUID(): string;                              // dependency-free, no libsodium needed
  randomBytes(length: number): Buffer;
  sha256(data: string | Buffer): string;              // hex-encoded
  sha512(data: string | Buffer): string;              // hex-encoded
  AES256(key: string, data: string): string;           // base64: nonce + ciphertext + auth tag bundled
  AES256Decrypt(key: string, encrypted: string): string;
};
```

```cp3
import crypto from "std/crypto";

crypto.randomUUID();          // dependency-free, no libsodium needed for this one
crypto.sha256(data);
crypto.sha512(data);

let encrypted = crypto.AES256("somekey", "somedatatoencrypt");
let plain = crypto.AES256Decrypt("somekey", encrypted);
```

`AES256Decrypt` throws (catchable, `README.md` §7) if the auth tag doesn't verify - a tampered or wrong-key ciphertext is a decrypt failure, never silently-wrong plaintext.

AES256 defaults to authenticated encryption (AES-256-GCM), with the nonce and auth tag bundled into the returned value automatically - nobody has to think about IV/nonce management themselves; that's handled safely by default.

### env

Built on top of process (a std module composed from another std module, same pattern as any other layered package) - a convenience wrapper for .env-style config files.

```cp3
const env: {
  load(path: string): void;       // parses a .env-style file into process.env; throws if the file doesn't exist
  get<T>(key: string, options?: { default?: T }): T;   // throws if missing and no default given
  has(key: string): boolean;
};
```

```cp3
import { env } from "std/env";

env.load("./somelocation/.env");
let port = env.get<number>("PORT", default: 3000);
```

### Map / Set

Included. 1:1 mapping to standard C++ containers - `Map<K, V>` wraps `std::unordered_map<K, V>` directly, `Set<T>` wraps `std::unordered_set<T>` directly, same pattern as array→vector. Method names match JS's Map/Set API closely enough that there's no meaningful design gap here.

```cp3
class Map<K, V> {
  function set(key: K, value: V): void;
  function get(key: K): V | undefined;
  function has(key: K): boolean;
  function delete(key: K): boolean;   // returns whether a key was actually removed
  function clear(): void;
  function keys(): K[];
  function values(): V[];
  function entries(): [K, V][];
  function forEach(callback: (value: V, key: K) => void): void;
  size: number;
}

class Set<T> {
  function add(value: T): void;
  function has(value: T): boolean;
  function delete(value: T): boolean;
  function clear(): void;
  function values(): T[];
  function forEach(callback: (value: T) => void): void;
  size: number;
}
```

```cp3
import { Map, Set } from "std/collections";

let m = new Map<string, number>();
m.set("a", 1);
m.get("a");
m.has("a");
m.delete("a");

let s = new Set<number>();
s.add(1);
s.has(1);
```

### buffer

Included as its own module - raw binary data handling, used internally by fs/crypto/fetch regardless, but exposed as a user-facing module too rather than staying purely internal, since leaving it out risks each module inventing its own separate way to handle raw bytes.

```cp3
class Buffer {
  static from(input: string, encoding?: string): Buffer;   // encoding default: "utf8"
  static alloc(size: number): Buffer;                       // zero-filled

  toString(encoding?: string): string;
  slice(start: number, end?: number): Buffer;
  length: number;
}
```

```cp3
import { Buffer } from "std/buffer";

let b = Buffer.from("hello");
b.toString();
```

## Explicitly deferred / out of scope

- Testing - no built-in test runner in std. Left to third-party/external packages, not part of core std.
- std/webview - considered, then deliberately dropped from the core plan entirely (see Architecture note above). Not "deferred" in the sense of "coming later officially" - just not part of the roadmap; anyone can build it as a third-party package whenever the need arises. (`std/server` is no longer a separate module at all - its capability lives in `std/network` now, alongside `fetch` and WebSocket support; see the `network` module section above.)
- Raw TCP/UDP socket networking beyond HTTP/WebSocket (`std/network`) - **decided: deferred past beta one.** Not included in beta one at all; don't build against it.

## Streaming: fs and process stay separate implementations

`fs.readFileStream` and `process.spawn`'s event-based output are backed by genuinely different OS mechanisms underneath (file I/O vs. process pipes) and belong to different std modules, so they are implemented independently, not sharing one literal underlying stream type. The two do follow the same API convention by design (`.on("data", ...)`, then a final `.on("end")`/`.on("exit")`), so usage looks near-identical even though the internals differ.

## Resolved for beta one

- Exact function signatures within each module - specified inline above per module.
- `console.log`/`.info` → stdout, `.warn`/`.error` → stderr - see the `console` section above.
- Raw socket/TCP networking - deferred past beta one, not included.
- Generics runtime reification mechanism - usage-based per-type shape descriptors, only for types that actually appear as `T` in `JSON.parse<T>`; full detail in `README.md` §23.
- **`fetch`, `server`, and WebSocket support consolidated into a single `std/network` module**, replacing the earlier separate axios-style `std/fetch` and `std/server` (no WebSocket) modules - `fetch` switched to the exact native JS `fetch(url, options)` shape (no more `.get()`/`.post()`/`.custom()`), backed internally by uWebSockets for the server/WebSocket side. Full detail in the `network` section above.
- **`console.clear()`, `console.assert()`, and `console.readKey(): Promise<string>` added** - found missing while writing real example programs (an interactive CLI menu needs `readKey`; a screen-clearing TUI needs `clear`), matching the project's own principle that writing real code is the best way to surface spec gaps.
- **`Math.gcd`/`.lcm`/`.clamp`/`.lerp`/`.cbrt` added** - plain `<numeric>`/`<cmath>`, not a linear-algebra dependency (Eigen solves a different problem: matrices, not these scalar helpers).
- **Global `sleep(ms)`, `parseInt(str)`, and `parseFloat(str)` added** as core-language builtins (`README.md`'s "built into the language" list) - `sleep` is a thin `Promise` wrapper over `setTimeout`, no new mechanism; `parseInt`/`parseFloat` match JS's own placement as globals rather than string methods.

## Resolved at beta-one sign-off (nothing left open)

- **`Buffer` encodings - decided: `"utf8"` (the default), base64, and hex, and nothing more for beta one.** Those three cover the real early use cases (text, crypto digests, binary-over-JSON). No further encodings (`utf16le`, `latin1`, `ascii`, `base64url`, …) are added speculatively - adding one later is an additive change, not a decision that blocks anything.

Nothing else is flagged here for beta one.
