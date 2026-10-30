---
title: Standard library overview
chapter: Chapter 3 · Standard library
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
import { Map, Set } from "std/collections";
```

Imports are resolved by the compiler's standard module registry. Unknown module names and unknown closed-surface members are compile-time errors. See [Modules and imports](#01-language/06-modules-and-errors/01-modules).

## Module index

| Module | What it provides |
| --- | --- |
| [Console](#03-standard-library/01-console) | Terminal output, line and key input, and stdin listeners |
| [Environment](#03-standard-library/02-env) | Environment lookup and `.env` loading |
| [Process](#03-standard-library/03-process) | Arguments, environment map, child processes, and exit |
| [File system](#03-standard-library/04-fs) | Synchronous and promise-based file and directory operations |
| [Path](#03-standard-library/05-path) | Platform-aware path manipulation |
| [Operating system](#03-standard-library/06-os) | Host and machine information |
| [Math](#03-standard-library/07-math) | Numeric constants and common functions |
| [Date](#03-standard-library/08-date) | Current time, parsing, formatting, and date arithmetic |
| [JSON](#03-standard-library/09-json) | Serialization and checked parsing |
| [Cryptography](#03-standard-library/10-crypto) | Random bytes, hashes, UUIDs, and authenticated encryption |
| [Buffer](#03-standard-library/11-buffer) | Byte buffers and text encodings |
| [Collections](#03-standard-library/12-collections) | Insertion-ordered Map and Set |
| [Network](#03-standard-library/13-network) | Fetch, WebSockets, and HTTP server APIs |
| [Arrays](#03-standard-library/14-arrays) | Built-in array operations |
| [Strings](#03-standard-library/15-strings) | Built-in UTF-8 string operations |
| [Typed arrays](#03-standard-library/16-typed-arrays) | Fixed-width numeric arrays, buffers, and DataView |
| [Regular expressions](#03-standard-library/17-regular-expressions) | Core regex literals, flags, match results, and string splitting |

## Error and asynchronous behavior

The exact failure behavior differs by API. Some operations throw synchronously; file, network, and console input APIs may return `Promise` values that reject. Read the page for the operation you use and handle the rejection path where the API documents one. CP3 exceptions and `try`/`catch` syntax are covered in [Errors and exceptions](#01-language/06-modules-and-errors/02-errors).

## Surface versus compatibility

The compiler's semantic tables define the supported call surface. For example, `std/process` exposes `argv`, `env`, `exit`, `exec`, and `spawn`; it does not expose Node's `process.stdout.write` stream API. The `exec` result provides captured `stdout` and `stderr` after completion. Use `SpawnHandle` when output must be processed while a child is running. See [Process](#03-standard-library/03-process) for the precise distinction.
