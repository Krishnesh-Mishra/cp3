---
title: File system
---
# File system

`std/fs` provides local file and directory operations. Import the default namespace:

```ts
import fs from "std/fs";
```

Paths are strings; build them with `std/path`. Synchronous calls throw on failure; the promise-based calls reject (and take no thread-pool hop — the work runs inline). The optional `encoding` argument on read calls is accepted and ignored; contents are always returned as `string`. There is no `Buffer`-returning whole-file read, and no `rm`/`rmdir`/`rename` — directories cannot be removed through `std/fs`; `unlink` removes files only. There is no `fs.promises` namespace; the promise API is directly on the default import.

## Synchronous API

| Member | Signature | Notes |
| --- | --- | --- |
| `readFileSync` | `readFileSync(path: string, encoding?: string): string` | Whole file as text; throws on failure |
| `writeFileSync` | `writeFileSync(path: string, data: string): void` | Writes, replacing any existing file; throws on failure |
| `appendFileSync` | `appendFileSync(path: string, data: string): void` | Appends |
| `existsSync` | `existsSync(path: string): boolean` | True when a file or directory exists at `path` |
| `mkdirSync` | `mkdirSync(path: string, recursive?: boolean): void` | Creates the directory; `recursive` also creates missing parents |
| `readdirSync` | `readdirSync(path: string): string[]` | Entry names as strings |
| `statSync` | `statSync(path: string): FileStats` | Metadata at the time of the call |
| `unlinkSync` | `unlinkSync(path: string): void` | Removes the file at `path` (files only) |

## Promise API

| Member | Signature | Notes |
| --- | --- | --- |
| `readFile` | `readFile(path: string, encoding?: string): Promise<string>` | Rejects on failure; `encoding` accepted and ignored |
| `writeFile` | `writeFile(path: string, data: string): Promise<void>` | Writes, replacing any existing file |
| `appendFile` | `appendFile(path: string, data: string): Promise<void>` | Appends |
| `mkdir` | `mkdir(path: string, recursive?: boolean): Promise<void>` | |
| `readdir` | `readdir(path: string): Promise<string[]>` | Entry names as strings |
| `stat` | `stat(path: string): Promise<FileStats>` | |
| `unlink` | `unlink(path: string): Promise<void>` | Files only |

There is no promise-based `exists`; use `existsSync` or handle the operation's own error.

`FileStats` fields: `size` (bytes; `u64` in a Rust/C++ project, `number` in TypeScript), `isFile` (`boolean`), `isDirectory` (`boolean`), `modifiedAt` (`Date`).

## Streams

| Receiver | Member | Signature | Notes |
| --- | --- | --- | --- |
| `readFileStream` | `readFileStream(path, chunkSize?)` | `readFileStream(path: string, chunkSize?: number): ReadStream` | File bytes in chunks; 64 KiB default; chunk size uses the file-size width |
| `ReadStream` | `on("data", listener)` | `(chunk: Buffer) => void` | Nominal `Buffer` chunks; pump starts at the first `on` registration, one read per microtask |
| `ReadStream` | `on("end", listener)` | `() => void` | EOF reached |
| `ReadStream` | `on("error", listener)` | `(message: string) => void` | With no `"error"` listener, the pump throws from its microtask, terminating the program |
| `writeFileStream` | `writeFileStream(path)` | `writeFileStream(path: string): WriteStream` | Incremental output; destination opened (truncated) lazily at first write/`end` |
| `WriteStream` | `write(chunk)` | `write(chunk: string \| Buffer): void` | Appends bytes |
| `WriteStream` | `end` | `end(): void` | Closes the file |
| `WriteStream` | `on("error", listener)` | `(message: string) => void` | With no listener, failure throws from the call site |

## Watching

| Member | Signature | Notes |
| --- | --- | --- |
| `watch` | `watch(path: string, callback: (eventType: string, filename: string) => void): FsWatcher` | `eventType` is `"change"` or `"rename"`; the returned watcher's `close()` stops delivery; the watcher keeps the program alive until then |

Behavior details: [guide](../../standard-library/04-fs.md)
