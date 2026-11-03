---
title: Process arguments, environment, and child processes
chapter: Chapter 3 · Standard library
---
# Process arguments, environment, and child processes

Import `std/process` to access the running program's arguments and environment, choose an exit code, or launch another program:

```ts
import process from "std/process";
```

The module has a deliberately small, explicit API. It is not a Node.js compatibility layer: CP3 does not expose `process.stdout`, `process.stderr`,
`process.stdin`, signals, or arbitrary Node process properties. Use `std/console` for terminal output and `std/env` for typed environment reads and
`.env` file loading.

## API at a glance

| Member | Type / result | Use |
| --- | --- | --- |
| `process.argv` | `string[]` | Read the arguments passed to this program. |
| `process.env` | `Map<string, string>` | Read or update the program's in-memory environment map. |
| `process.exit(code)` | `void` | Flush standard output and terminate with a numeric exit code. |
| `process.exec(cmd, args)` | `Promise<ExecResult>` | Run a child and collect both output streams until it exits. |
| `process.spawn(cmd, args)` | `SpawnHandle` | Start a child and observe output/exit events while it runs. |

The compiler treats this namespace as a closed surface. An unsupported member is a compile-time diagnostic rather than a dynamic property lookup.

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
from a `.env` file. See [Environment variables](02-env.md).

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

child.on("data", (chunk: string): void => {
  console.log("out:", chunk);
});

child.on("stderr", (chunk: string): void => {
  console.error("err:", chunk);
});

child.on("exit", (code: number): void => {
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

## Event loop and execution model

Child I/O work runs away from the calling thread and posts results or events back to the thread's CP3 event loop. `exec` does not synchronously block
the language event loop while waiting for a child. `spawn` callbacks are also delivered as loop tasks; they are not synchronous callbacks from the OS
pipe reader. Keep the event loop alive while your program expects child results or events.

This is distinct from ordinary `async` syntax: `await` coordinates promise completion, while workers and parallel APIs represent separate concurrency
boundaries. For those distinctions, see [Async and concurrency](../02-runtime/07-async-and-concurrency.md) and [Thread safety](../02-runtime/10-thread-safety.md).

## Node.js compatibility boundary

| Node-style expectation | CP3 equivalent |
| --- | --- |
| `process.argv` | Supported as `string[]`; includes the executable name in the runtime convention. |
| `process.env.NAME` | Use `process.env.get("NAME")` or `std/env`. |
| `process.stdout.write(...)` | Not supported; use `console.log` / `console.error` for complete terminal output. |
| `process.stderr.write(...)` | Not supported as a process stream; use `console.error`. |
| `process.stdin` | Not exposed. `SpawnHandle.write` targets a child process's stdin only. |
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

- [Environment variables](02-env.md)
- [Console output and input](01-console.md)
- [Async and concurrency](../02-runtime/07-async-and-concurrency.md)
- [Thread safety and ownership boundaries](../02-runtime/10-thread-safety.md)
- [Native toolchains](../04-compiler-cli/05-native-toolchains.md)

The semantic API table is in `src/std/process/sema.cpp`; C++ runtime behavior is in `src/std/process/include/cp3/std/process.hpp` and
`src/std/process/child.cpp`; generated calls are lowered in `src/std/process/codegen.cpp`. The focused source tests live under
`tests/cases/stdlib/03/process/`.
