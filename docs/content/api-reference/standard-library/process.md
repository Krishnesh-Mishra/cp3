---
title: Process
---
# Process

`std/process` exposes the running program's arguments, environment, lifecycle and stdio. Import the default namespace:

```ts
import process from "std/process";
```

The namespace is closed — an unsupported member is a compile-time diagnostic. Numeric widths: a `typescript` project keeps `number`; a `rust`/`cpp` project uses exact widths (`pid`/`ppid`/`exit`/`exitCode`/`ExecResult.exitCode` and the `"exit"` payload are `i32`).

## API at a glance

| Member | Signature / type | Notes |
| --- | --- | --- |
| `process.argv` | `string[]` | Arguments; executable name is the first item |
| `process.env` | `Map<string, string>` | In-memory environment map (CP3 `Map`; not bracket-indexed); empty OS values seed as absent |
| `process.exit` | `exit(code: number): void` | Flushes stdio, then terminates; code after it does not run |
| `process.exitCode` | `number` | Stored exit code (default `0`); assigning does not exit |
| `process.cwd` | `cwd(): string` | Current working directory |
| `process.chdir` | `chdir(path: string): void` | Changes the working directory; throws on failure |
| `process.pid` / `process.ppid` | `number` (`i32`) | Process id / parent process id |
| `process.uptime` | `uptime(): number` | Seconds since start, monotonic |
| `process.memoryUsage` | `memoryUsage(): MemoryUsage` | `{ rss, heapTotal, heapUsed, external, arrayBuffers }`; `rss` is real bytes, the four heap fields are always `0` |
| `process.hrtime` | `hrtime(): [number, number]` | `[seconds, nanoseconds]` since an unspecified monotonic origin |
| `process.platform` | `string` | `"win32"`, `"darwin"` or `"linux"` |
| `process.arch` | `string` | `"x64"` or `"arm64"` |
| `process.version` | `string` | CP3 runtime version string |
| `process.nextTick` | `nextTick(callback: () => void): void` | Queues `callback` before timers and promise continuations, FIFO |
| `process.onShutdown` | `onShutdown(listener: (code: number) => void): void` | Sugar for `on("exit", ...)`; runs once, in registration order |

## Child processes

| Member | Signature | Notes |
| --- | --- | --- |
| `process.exec` | `exec(cmd: string, args: string[]): Promise<ExecResult>` | Buffered child; resolves after exit with `{ stdout: string, stderr: string, exitCode: number }`; rejects only when the command cannot be launched — check `exitCode`; buffers all output |
| `process.spawn` | `spawn(cmd: string, args: string[]): SpawnHandle` | Returns immediately; a child that cannot be started throws synchronously; no shell parsing — pass `args` separately |

`SpawnHandle` events (`on(event, listener)`; event name must be a string literal):

| Event | Payload | Notes |
| --- | --- | --- |
| `"data"` | `string` | Chunk read from the child's stdout |
| `"stderr"` | `string` | Chunk read from the child's stderr |
| `"exit"` | `number` (`i32` in Rust/C++) | Exit status when the child terminates |

`child.write(text: string): void` writes to the child's stdin (no `Buffer` overload; text after the child closed its input is a no-op). `child.kill(): void` requests termination; the child reports `"exit"` with a platform-dependent non-zero code; it does not guarantee descendant processes stop. Register listeners immediately; callbacks arrive as event-loop tasks.

## Lifecycle events and signals

`process.on(event, listener)` — the runtime dispatches exactly these names (unknown names are ignored):

| Event | Payload | Notes |
| --- | --- | --- |
| `"SIGINT"` | `string` (the signal name) | Delivered on the event-loop thread once the OS signal arrives |
| `"SIGTERM"` | `string` (the signal name) | Delivered like `"SIGINT"` |
| `"exit"` | `number` (`i32` in Rust/C++) | `process.exitCode`, or `128+signum` (`130`/`143`) on the signal path |
| `"unhandledRejection"` | `string` | The rejection reason text |

Listeners are synchronous, run in registration order; an event name must be a string literal when the listener takes a payload. On `SIGINT`/`SIGTERM`, listeners run, then the exit hooks run, and the process ends with `128+signum`. `"exit"` listeners run exactly once, from an `atexit` handler, on the plain `exit` path, on signals, and on normal drain; a throwing hook does not stop the remaining hooks.

## Standard streams

| Member | Signature | Notes |
| --- | --- | --- |
| `process.stdout` / `process.stderr` | `StdioStream` | `.write(text: string): void` — writes with no newline, then flushes |
| `process.stdin` | `StdinStream` | See below |

`process.stdin` members (all settle at EOF; never reject):

| Member | Signature | Notes |
| --- | --- | --- |
| `readAll` | `readAll(): Promise<string>` | All bytes to EOF as one string |
| `readLines` | `readLines(): Promise<string[]>` | Every line, terminators removed (`\r\n` is one terminator) |
| `readLine` | `readLine(): Promise<string>` | The next line, terminator removed; `""` on EOF; successive calls never lose bytes |
| `on` | `on(event: "line" \| "close", listener: (value: string) => void): void` | `"line"` per completed line; `"close"` once at EOF with `""`; a tap — never consumes a line a pending `readLine` also sees |
| `close` | `close(): void` | Stops accepting new reads; already-pending reads settle normally |

The console family (`console.input()`/`readKey()`) runs its own stdin pump; mixing it with `process.stdin` races on fd 0.

Behavior details: [guide](../../standard-library/03-process.md)
