---
title: Process and child processes
chapter: Chapter 3 · Standard library
---
# Process and child processes

`std/process` is a default namespace import:

```ts
import process from "std/process";
```

The supported surface is intentionally small and explicit. It provides process arguments, an environment map, process exit, and child-process helpers. It does not currently implement the Node.js `process.stdout.write(string | Buffer)` or `process.stderr` stream APIs.

## Arguments and environment

### `process.argv`

An array of strings containing the command-line arguments passed to the program. The exact launcher and platform determine whether the executable path appears in the array; inspect the values in the target packaging mode rather than relying on Node's index conventions.

### `process.env`

A `Map<string, string>` of the process environment:

```ts
const configured = process.env.has("APP_MODE");
const mode = process.env.get("APP_MODE"); // string | undefined
```

Use `std/env` for typed reads, defaults, and `.env` loading. See [Environment variables](#03-standard-library/02-env).

## `process.exit(code)`

Terminates the program with the supplied numeric exit code. Use zero for success and non-zero values for failure according to the command-line contract of your program. This is an immediate process-level action; code after the call is not a cleanup guarantee.

## Buffered child execution: `process.exec`

`process.exec(cmd, args)` starts a child with an explicit argument array and returns `Promise<ExecResult>`:

```ts
const result = await process.exec("git", ["status", "--short"]);
console.log(result.stdout);
if (result.exitCode !== 0) {
  console.error(result.stderr);
}
```

The result has `stdout: string`, `stderr: string`, and `exitCode: number`. Both streams are captured until the child exits. A non-zero exit code is a resolved result, not a rejected promise. Rejection is reserved for a launch failure. Check `exitCode` when command success matters.

Pass executable and arguments separately. Do not build a shell command string from untrusted input: this API's documented form is executable plus argument vector, not an implicit shell parser.

Because output is buffered, `exec` is convenient for short commands with bounded output. A command that generates a very large amount of output can consume substantial memory before the promise resolves. Prefer `spawn` when output should be handled incrementally.

## Live child process: `process.spawn`

`process.spawn(cmd, args)` returns a `SpawnHandle` immediately. Its `on` method receives `"data"` (stdout chunks), `"stderr"` (stderr chunks), or `"exit"` (the exit code); `write` sends text to child stdin; `kill` ends the child.

```ts
const child = process.spawn("tool", ["--watch"]);
child.on("data", (chunk: string): void => console.log(chunk));
child.on("stderr", (chunk: string): void => console.error(chunk));
child.on("exit", (code: string): void => console.log("exit", code));
```

The event callback currently exposes its value as a string, including the exit event in the semantic signature. The runtime event's content for `"exit"` is the numeric code rendered through that callback boundary; verify the exact behavior before using it as a numeric value. `SpawnHandle` has no documented `stdout` stream property or promise-based close method.

Install handlers immediately after spawning so early output is not missed. `write` accepts text for child stdin; it is not a Buffer-writing overload. `kill` ends the child, but the current handle surface does not provide a typed wait-for-exit method. Use the `"exit"` event for completion notification.

Do not assume that killing a process also terminates every descendant it launched. Process-tree behavior depends on the platform and the child program. If the parent process starts long-lived work, design an explicit shutdown protocol for that program.

## Node.js compatibility boundary

These APIs are not drop-in Node process APIs. In particular:

- `process.stdout.write` and `process.stderr.write` are not available.
- There is no `process.stdin` stream object.
- `process.exec` returns captured output after completion; use `spawn` for live events.
- `process.env` is a typed CP3 Map, not Node's mutable string-indexed object.

## Choosing `exec` or `spawn`

| Need | API |
| --- | --- |
| Wait for a short command and inspect complete stdout/stderr | `exec` |
| Consume stdout or stderr while the program is running | `spawn` plus `on` |
| Send text to the child's stdin | `spawn` plus `write` |
| Stop a running child | `spawn` plus `kill` |
| Print a complete line from CP3 | `console.log` or `console.error` |

Child-process execution is a security boundary. Keep the executable selection controlled, pass arguments separately, and validate any argument derived from user input. Do not assume shell quoting rules apply because the API does not document an implicit shell.

Use `console.log` / `console.error` for complete terminal lines. The compiler's current API table is the source of truth for supported members.
