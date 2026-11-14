---
title: Standard library overview
---
# Standard library overview

CP3 ships a versioned standard library with the compiler and runtime. Imported modules are explicit: use the module specifier and import form listed below. A familiar API name does not imply that CP3 implements all of Node.js, the browser platform, or a JavaScript package.

## Import conventions

Most modules expose a default namespace object:

```ts
import console from "std/console";
import math from "std/math";

console.log(math.sqrt(81));
```

Some modules use named exports because their values are types or singleton objects:

```ts
import { env } from "std/env";
import { Buffer } from "std/buffer";
```

The collections family is TypeScript-style ambient: `Map`, `Set`, `LRUCache`, `MultiMap`, `ConcurrentMap`, `Queue`, `Stack`, `PriorityQueue`, `RingBuffer` and `BitSet` resolve with no import, mirroring TypeScript. `import { Map, Set } from "std/collections"` still works and narrows identically.

For annotation positions, `std/<module>/types` modules re-export the type names of `std/network`, `std/buffer`, `std/url`, `std/date` and `std/text` so they can be imported as types without pulling in the runtime surface:

```ts
import { URL, URLSearchParams } from "std/url/types";
import { Buffer } from "std/buffer/types";
```

Type-only imports exercise no runtime loader. There is no `std/collections/types` by design.

The compiler recognizes the `std/*` module specifiers; unknown module names and unknown members of a closed surface are compile-time errors. See [Modules and imports](../language/06-modules-and-errors/01-modules.md).

Strings and arrays are language builtins, not stdlib modules: see
[Built-in strings](../language/04-values-and-objects/05-strings.md) and
[Built-in array methods](../language/04-values-and-objects/04-arrays.md).

## Module index

| Module | What it provides |
| --- | --- |
| [Console](./01-console.md) | Terminal output, line and key input, and stdin listeners |
| [Environment](./02-env.md) | Environment lookup and `.env` loading |
| [Process](./03-process.md) | Arguments, environment map, child processes, exit, stdio streams, and lifecycle signals |
| [File system](./04-fs.md) | Synchronous and promise-based file and directory operations |
| [Path](./05-path.md) | Platform-aware path manipulation |
| [Operating system](./06-os.md) | Host and machine information |
| [Math](./07-math.md) | Numeric constants and common functions |
| [Date](./08-date.md) | Current time, parsing, formatting, and date arithmetic |
| [JSON](./09-json.md) | Serialization and checked parsing |
| [Cryptography](./10-crypto.md) | Random bytes, hashes, UUIDs, and authenticated encryption |
| [Buffer](./11-buffer.md) | Byte buffers and text encodings |
| [Collections](./12-collections.md) | Map, Set, LRUCache, Queue, Stack, PriorityQueue, RingBuffer, BitSet, MultiMap, ConcurrentMap |
| [Network](./13-network.md) | Fetch, HTTP server and TLS, WebSockets, raw TCP/UDP/TLS sockets |
| [Typed arrays](./16-typed-arrays.md) | Fixed-width numeric arrays, buffers, and DataView |
| [Regular expressions](./17-regular-expressions.md) | Core regex literals, flags, match results, and string splitting |
| [I/O readiness](./18-io.md) | Non-blocking fd readiness and `pollUntilDone` for native socket libraries |
| [URL and query strings](./19-url.md) | Percent-encoding, `URL`, `URLSearchParams` |
| [Text encoding](./20-text.md) | UTF-8 `TextEncoder`/`TextDecoder` |
| [Time and stopwatches](./21-time.md) | Monotonic `Stopwatch`, `console.time`/`timeEnd` |
| [Readline](./22-readline.md) | Line reading over stdin: `readLine()`/`readLines()`/`readAll()` promises and `createReader()` |
| [Checksums](./23-hash.md) | Non-crypto `crc32`/`crc32c`/`fnv1a32`/`fnv1a64`/`xxhash64` |
| [URL extensions](./24-url-extensions.md) | Base resolution, normalization, form-urlencoded details |

## Error and asynchronous behavior

The exact failure behavior differs by API. Some operations throw synchronously; file, network, and console input APIs may return `Promise` values that reject. Read the page for the operation you use and handle the rejection path where the API documents one. CP3 exceptions and `try`/`catch` syntax are covered in [Errors and exceptions](../language/06-modules-and-errors/02-errors.md).

## Surface versus compatibility

The standard library is deliberately small. For example, `std/process` exposes `argv`, `env`, `exit`, `exec`, and `spawn`, plus the `stdout`/`stderr` write streams, `stdin`, and the `process.on` lifecycle events (`SIGINT`, `SIGTERM`, `exit`, `unhandledRejection`); it does not expose every Node process property. The `exec` result provides captured `stdout` and `stderr` after completion. Use `SpawnHandle` when output must be processed while a child is running. See [Process](./03-process.md) for the precise distinction.
