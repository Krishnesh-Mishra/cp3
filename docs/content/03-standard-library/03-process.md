---
title: Process arguments, environment, and child processes
chapter: Chapter 3 · Standard library
---
# Process arguments, environment, and child processes

Import `std/process` to access the running program's arguments and environment, choose an exit code, or launch another program:

```ts
import process from "std/process";
```

The module has a deliberately small, explicit API. It is not a Node.js compatibility layer, but the standard process streams and lifecycle signals are exposed: `process.stdout`, `process.stderr`, `process.stdin`, and `process.on("SIGINT" | "SIGTERM" | "exit" | "unhandledRejection", ...)`. Use `std/console` for terminal output and `std/env` for typed environment reads and `.env` file loading. Arbitrary Node process properties are not exposed.

## API at a glance

| Member | Type / result | Use |
| --- | --- | --- |
| `process.argv` | `string[]` | Read the arguments passed to this program. |
| `process.env` | `Map<string, string>` | Read or update the program's in-memory environment map. |
| `process.exit(code)` | `void` | Flush standard output and terminate with a numeric exit code. |
| `process.exec(cmd, args)` | `Promise<ExecResult>` | Run a child and collect both output streams until it exits. |
| `process.spawn(cmd, args)` | `SpawnHandle` | Start a child and observe output/exit events while it runs. |
| `process.cwd()` | `string` | The current working directory of the process. |
| `process.chdir(path)` | `void` | Change the working directory; throws on failure. |
| `process.pid` | `number` | The process id of the current process. |
| `process.ppid` | `number` | The process id of the parent process. |
| `process.uptime()` | `number` | Seconds since the program started (monotonic). |
| `process.memoryUsage()` | `MemoryUsage` | Process resident-set size; the heap fields are always `0`. |
| `process.hrtime()` | `[number, number]` | `[seconds, nanoseconds]` since an unspecified monotonic origin. |
| `process.platform` | `string` | The OS platform: `"win32"`, `"darwin"` or `"linux"`. |
| `process.arch` | `string` | The CPU architecture: `"x64"` or `"arm64"`. |
| `process.version` | `string` | The CP3 runtime version string. |
| `process.exitCode` | `number` | The stored process exit code (default `0`); assigning it does not exit. |
| `process.nextTick(callback)` | `void` | Queues `callback` before timers and promise continuations, in FIFO order. |
| `process.stdout` | `StdioStream` | Writable stream for standard output; `.write(s)` emits `s` with no newline, then flushes. |
| `process.stderr` | `StdioStream` | Writable stream for standard error; `.write(s)` emits `s` with no newline, then flushes. |
| `process.stdin` | `StdinStream` | Readable stream for standard input: `readAll` / `readLines` / `readLine` / `on("line" \| "close", ...)` / `close()`. |
| `process.on(event, listener)` | `void` | Registers a lifecycle listener for `"SIGINT"`, `"SIGTERM"`, `"exit"` or `"unhandledRejection"`. |
| `process.onShutdown(listener)` | `void` | Sugar for `on("exit", ...)`; runs once, before the process ends. |

The compiler treats this namespace as a closed surface. An unsupported member is a compile-time diagnostic rather than a dynamic property lookup.

Numeric process values follow the project vocabulary. A `typescript` project keeps `number`; a `rust` or `cpp` project uses the exact standard-library width: `process.pid`, `process.ppid`, `process.exit`/`process.exitCode`, every `ExecResult.exitCode`, and the `"exit"` event payload are `i32` (`int32_t` in C++), so an exit status never passes through a floating-point value.

## Reading command-line arguments

`process.argv` is a `string[]` populated by the generated program entry point:

```ts
const args = process.argv;
console.log("argument count:", args.length);

let i = 0;
while (i < args.length) {
  console.log(i, args[i]);
  i = i + 1;
}
```

The executable name is included as the first item, matching the runtime's Node-like convention. A normal CP3 entry point initializes the argument
vector before user statements run. If a program never imports `std/process`, the runtime fallback is a one-element vector containing `"cp3"`; do not
use that fallback to infer how a launcher packages a deployed program.

Arguments are strings. Parse and validate numeric or structured values explicitly before using them. Treat input as untrusted even when it came from
a shell command or a build script.

```ts
const mode = process.argv.length > 1 ? process.argv[1] : "serve";
console.log("mode:", mode);
```

The exact argument values depend on how the executable is launched, especially when another tool or wrapper is involved. The `process_argv_is_array`
and `process_args_not_array` standard-library cases pin the array contract and rejection of an invalid child-argument shape.

## The process environment map

`process.env` is a `Map<string, string>`, not Node's mutable object indexed with `process.env.NAME`:

```ts
const env = process.env;
if (env.has("APP_MODE")) {
  const mode = env.get("APP_MODE"); // string | undefined
  console.log("mode:", mode);
}
```

The map is created on first use and seeded from the operating-system environment. It is a CP3 runtime map, so use its collection methods (`get`,
`has`, `set`, and other supported `Map` members) rather than JavaScript bracket syntax.

`get` returns `string | undefined` when a key is absent. A present-but-empty operating-system variable is treated as absent when the initial map is
seeded. This behavior is covered by `process_env_get_unset`; code that must distinguish an empty value from a missing value should account for this
runtime rule.

### Updating the map

You can set a value in the in-memory map:

```ts
process.env.set("APP_MODE", "production");
```

This changes the CP3 process map. It does **not** call the operating system's environment mutation API. Child processes inherit the operating-system
environment, not later mutations made only to this CP3 map. Use `std/env` if you need typed reads, defaults, required-value checks, or to load values
from a `.env` file. See [Environment variables](./02-env.md).

## Choosing an exit code

`process.exit(code)` flushes the standard output streams and terminates the process:

```ts
if (process.argv.length < 2) {
  console.error("missing command");
  process.exit(2);
}
```

Use `0` for success and a non-zero number for a failure according to your command-line interface. The value is converted to the process exit status.
This is an immediate process-level operation, not a structured return from `main`; code after it does not run, and it is not a general guarantee that
application-level cleanup callbacks will execute. Prefer returning normally when ordinary scope cleanup is required.

The `process_exit_zero` and `process_exit_nonzero` cases check that the chosen status reaches the runner. The CLI also propagates a program's exit code
when invoked through `cp3 run`.

## Run a short child command with `exec`

`process.exec(cmd, args)` starts the executable with a separate argument array and returns a promise that resolves after the process exits:

```ts
const result = await process.exec("git", ["status", "--short"]);
console.log(result.stdout);

if (result.stderr.length > 0) {
  console.error(result.stderr);
}

if (result.exitCode !== 0) {
  console.error("git exited with", result.exitCode);
}
```

The resolved `ExecResult` has exactly these documented fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `stdout` | `string` | Complete captured standard output. |
| `stderr` | `string` | Complete captured standard error. |
| `exitCode` | `number` | The child's exit status. |

A command that starts and returns a non-zero status still resolves normally. Check `exitCode` yourself. The promise rejects only when the runtime cannot
launch the command. `exec` captures output until completion, so it is best for commands with bounded output; a noisy long-running command can consume
memory while the output is buffered.

The command and arguments are passed separately. This API does not document a shell command-string parser, so do not concatenate untrusted input into
`cmd`. Select the executable deliberately and validate any user-controlled arguments. The `process_exec_signature` and
`process_exec_spread_args` cases cover the accepted signatures and argument-array form.

## Observe a live child with `spawn`

Use `process.spawn(cmd, args)` when output should be observed while the child is running:

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
```

`spawn` returns a handle immediately. The supported events are:

| Event | Callback value | Meaning |
| --- | --- | --- |
| `"data"` | `string` | A chunk read from the child's standard output. |
| `"stderr"` | `string` | A chunk read from the child's standard error. |
| `"exit"` | `number` | The child's exit status when it terminates. |

Register listeners immediately. Output may arrive before a listener registered later, and event callbacks are dispatched through the CP3 event loop.
The event loop must be allowed to drain for callbacks to run. A child that cannot be started throws synchronously from `spawn`; it does not return a
handle that later emits an ordinary exit event.

The event name should be a string literal when the callback accepts a value, because the payload type depends on the event. A callback with no
parameters can be used when it does not need the payload. The exact event signatures are checked by the compiler and exercised by the
`process_spawn_signature`, `process_spawn_computed_event`, and `process_spawn_computed_event_payload` cases.

### Writing to child stdin

`child.write(text)` writes a string to the child's standard input:

```ts
const child = process.spawn("tool", []);
child.write("input line\n");
```

This is a text API; it does not accept a `Buffer` overload. Writing after the child has closed its input is currently a no-op. There is no separate
typed close-stdin or await-close method on the CP3 handle. Coordinate the child's input protocol so it can finish without relying on an unavailable
stream API.

### Stopping a child

`child.kill()` requests termination. The child still reports an `"exit"` event; the resulting code can be non-zero and is platform-dependent. Do not
assume the request terminates every descendant process that the child launched. If a child creates its own process tree, define a shutdown protocol
for the child application.

## Writing to standard output and error

`process.stdout` and `process.stderr` are `StdioStream` values, one per stream:

| Member | Signature | Meaning |
| --- | --- | --- |
| `.write(text)` | `(text: string) => void` | Writes `text` with no newline appended, then flushes. |

```ts
process.stdout.write("hello");
process.stdout.write(" world\n");
process.stderr.write("warning: outdated config\n");
```

`write` takes a `string`; there is no `Buffer` overload. A flush follows every `write`, so output reaches the stream without waiting for process exit. `console.log` remains the simplest way to print complete lines; reach for the streams when you need to write without a trailing newline.

## Reading standard input

`process.stdin` is a `StdinStream`. All of its read methods settle at EOF (never reject; EOF is the normal end):

| Member | Signature | Meaning |
| --- | --- | --- |
| `readAll()` | `() => Promise<string>` | Resolves with every byte of stdin read to EOF. |
| `readLines()` | `() => Promise<string[]>` | Resolves with every line read to EOF, terminators removed (`\r\n` counts as one terminator). |
| `readLine()` | `() => Promise<string>` | Resolves with the next line, terminator removed; `""` on EOF. Successive calls never lose bytes. |
| `on(event, listener)` | `on(event: "line" \| "close", listener: (value: string) => void) => void` | Observes every completed line (`"line"`) or stdin EOF (`"close"`, once, with `""`). A tap: it never consumes a line a pending `readLine` also sees. |
| `close()` | `() => void` | Stops accepting new stdin reads; already-pending reads settle normally. |

```ts
const line = await process.stdin.readLine();
console.log("got:", line);
```

One sibling reader thread pumps the OS-level standard input and posts drain tasks to the event loop; `std/readline` shares this pump and line queue (its reads delegate to it). The console family (`console.input()` / `console.readKey()` / `console.on`) runs its own separate stdin pump, so mixing the console family with `process.stdin` in one program races on fd 0.

## Lifecycle events and signals

`process.on(event, listener)` registers a lifecycle listener. The documented event names are `"SIGINT"`, `"SIGTERM"`, `"exit"`, and `"unhandledRejection"` (the runtime dispatches exactly these; an unknown event name is ignored). An event name must be a string literal when the listener takes a payload parameter — otherwise the compiler cannot pick the parameter type and reports a diagnostic:

| Event | Listener payload | Meaning |
| --- | --- | --- |
| `"SIGINT"` | `string` (the signal name) | Delivered on the event-loop thread once the OS signal arrives. |
| `"SIGTERM"` | `string` (the signal name) | Delivered like `"SIGINT"`. |
| `"exit"` | `number` | The exit code: the stored `process.exitCode` (`0` by default), or `128+signum` (`130` / `143`) on the signal path. |
| `"unhandledRejection"` | `string` | The rejection reason text. |

In a Rust/C++ project the `"exit"` payload is `i32`; in a TypeScript project it is `number`, the same width rule as `process.exitCode`. A listener may take the payload parameter or no parameter at all. Listeners are synchronous and run in registration order.

```ts
process.on("SIGINT", (signal: string): undefined => {
  console.error("caught", signal);
});
```

On `"SIGINT"`/`"SIGTERM"`, the listeners run on the event-loop thread once the signal arrives; then the exit hooks run and the process ends with `128+signum` (`130` for SIGINT, `143` for SIGTERM). Listeners are synchronous: start any async cleanup before the signal, not inside the handler.

`"exit"` listeners (and `process.onShutdown(listener)`, sugar for `on("exit", listener)`) run exactly once, in registration order, from an `atexit` handler. They fire on the plain `process.exit` path, on a SIGINT/SIGTERM, and on normal drain; each receives the stored exit code. A throwing hook does not stop the remaining hooks or the exit.

```ts
process.onShutdown((code: number): undefined => {
  console.log(`leaving with ${code}`);
});
```

`process.exitCode` stores a plain exit code (default `0`); assigning it does **not** exit. `process.exit(code)` flushes the standard output streams and terminates with `code` regardless.

## Event loop and execution model

Child I/O work runs away from the calling thread and posts results or events back to the thread's CP3 event loop. `exec` does not synchronously block
the language event loop while waiting for a child. `spawn` callbacks are also delivered as loop tasks; they are not synchronous callbacks from the OS
pipe reader. Keep the event loop alive while your program expects child results or events.

This is distinct from ordinary `async` syntax: `await` coordinates promise completion, while workers and parallel APIs represent separate concurrency
boundaries. For those distinctions, see [Async and concurrency](../02-runtime/07-async-and-concurrency.md) and [Thread safety](../02-runtime/10-thread-safety.md).

## Process information and high-resolution timing

`process.cwd()` returns the current working directory as a string. `process.chdir(path)` changes it; the call throws an ordinary catchable error when the directory cannot be entered. `process.pid` and `process.ppid` report the process id and its parent. `process.uptime()` reports the seconds since the process started on a monotonic clock.

`process.memoryUsage()` returns `{ rss, heapTotal, heapUsed, external, arrayBuffers }`. `rss` is the real resident set size in bytes. The four heap fields are always `0` — CP3 has no managed heap; they are kept so the result matches the Node shape.

`process.hrtime()` returns `[seconds, nanoseconds]` relative to an unspecified monotonic origin. Subtract two readings to measure an interval:

```ts
const start = process.hrtime();
doWork();
const [sec, nanoseconds] = process.hrtime();
console.log(`elapsed: ${sec}s ${nanoseconds}ns`);
```

```ts
const usage = process.memoryUsage();
console.log(`rss: ${usage.rss} bytes`);
```

## Node.js compatibility boundary

| Node-style expectation | CP3 equivalent |
| --- | --- |
| `process.argv` | Supported as `string[]`; includes the executable name in the runtime convention. |
| `process.env.NAME` | Use `process.env.get("NAME")` or `std/env`. |
| `process.stdout.write(...)` | Supported; `StdioStream.write(text: string): void` emits `text` with no newline, then flushes. |
| `process.stderr.write(...)` | Supported; the stderr twin of `process.stdout.write`. |
| `process.stdin` | Supported; `StdinStream` with `readAll()` / `readLines()` / `readLine()` promises, `on("line" \| "close", ...)` and `close()`. |
| `process.on("SIGINT" \| "SIGTERM" \| "exit" \| "unhandledRejection", ...)` | Supported; see [Lifecycle events and signals](#lifecycle-events-and-signals). |
| `process.exec(...)` | CP3-specific buffered child API returning `Promise<ExecResult>`. |
| `process.spawn(...)` | CP3-specific handle with `on`, `write`, and `kill`; not Node's `ChildProcess` type. |

Do not port Node code by replacing only the import path. Identify which behavior the original program relies on, then choose a supported CP3 API or
provide an explicit native integration where the project needs functionality outside this surface.

## Security and reliability checklist

- Keep executable selection under application control.
- Pass arguments as a `string[]`; do not assume an implicit shell interprets or escapes them.
- Validate arguments derived from users, environment variables, or files.
- Check `ExecResult.exitCode`; a non-zero exit status is not a rejected promise.
- Bound or redirect output for commands that may produce large data, because `exec` buffers it.
- Attach `spawn` listeners immediately and keep the event loop running until expected events arrive.
- Treat `kill` as a termination request, not proof that a whole process tree has stopped.
- Do not assume shared CP3 references make data accessed by native worker threads safe to mutate.

## Related pages and implementation

- [Environment variables](./02-env.md)
- [Console output and input](./01-console.md)
- [Async and concurrency](../02-runtime/07-async-and-concurrency.md)
- [Thread safety and ownership boundaries](../02-runtime/10-thread-safety.md)
- [Native toolchains](../04-compiler-cli/05-native-toolchains.md)

The semantic API table is in `src/std/process/sema.cpp`; C++ runtime behavior is in `src/std/process/include/cp3/std/process.hpp` and
`src/std/process/child.cpp`; generated calls are lowered in `src/std/process/codegen.cpp`. The focused source tests live under
`tests/cases/stdlib/03/process/`.
