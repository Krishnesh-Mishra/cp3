---
title: Readline
chapter: Chapter 3 · Standard library
---
# Readline

`std/readline` reads lines from standard input. It is a default namespace import with no named exports:

```ts
import readline from "std/readline";

const line = await readline.readLine();
```

Every read resolves rather than rejecting: end-of-file is the normal end of stdin, not an error. `\r\n` counts as one line terminator (a trailing `\r` is stripped). All reads delegate to the same stdin pump `process.stdin` uses, so a program that also reads `process.stdin` shares one buffered line queue.

## `readline.readLine()`

Resolves with the next line from stdin, terminator removed. On EOF the promise resolves with `""` instead of rejecting. Successive calls never lose bytes.

## `readline.readLines()`

Reads stdin to EOF and resolves with every remaining line, terminators removed. Resolves only at EOF; the event loop is retained while the read is pending, so awaiting this keeps the program running until stdin closes.

## `readline.readAll()`

Reads stdin to EOF and resolves with every byte as one string, never rejecting. EOF is the normal end.

## `readline.createReader()`

Creates a `LineReader` over stdin for programs that need lines as they arrive:

```ts
const reader = readline.createReader();
reader.on("line", (line) => {
  console.log("got:", line);
});
const all = await reader.readLines();
```

The reader's `on` is a pure observation tap: it never consumes a line that a pending `readLine` also sees. Calling `close()` detaches the reader — its listeners stop firing and its later reads resolve immediately — but stdin stays open for other consumers. The reader itself is copyable (the closed flag is shared).

### `reader.on(event, listener)`

`event` is one of the string literals `"line"` or `"close"`:

- `"line"` fires once per completed line; the listener receives the line without its terminator.
- `"close"` fires once when stdin reaches EOF, with `""`.

The listener may declare the string payload or take no parameter at all; omitting the parameter is the nullary form. `"close"` fires exactly once per open reader.

### `reader.readLine()` and `reader.readLines()`

Same contract as the module-level helpers, but scoped to this reader: after `close()`, `readLine()` resolves with `""` and `readLines()` resolves with an empty array.

### `reader.close()`

Detaches this reader. Listeners stop firing; later reads resolve immediately. Does not close stdin for other consumers.

## Example

```ts
import readline from "std/readline";

async function main() {
  const name = await readline.readLine();
  console.log("hello", name);
  const rest = await readline.readLines();
  console.log("remaining lines:", rest.length);
}

await main();
```

## API summary

| Member | Signature (exact types) | Semantics |
| --- | --- | --- |
| `readline.readLine` | `readLine(): Promise<string>` | Next line of stdin, terminator removed; `""` on EOF instead of rejecting. Successive calls never lose bytes. |
| `readline.readLines` | `readLines(): Promise<string[]>` | All remaining lines to EOF, terminators removed (`\r\n` is one terminator). |
| `readline.readAll` | `readAll(): Promise<string>` | All remaining stdin bytes as one string; never rejects. |
| `readline.createReader` | `createReader(): LineReader` | A line reader over stdin (see below). |
| `reader.readLine` | `readLine(): Promise<string>` | Next line; `""` on EOF or once this reader is closed. |
| `reader.readLines` | `readLines(): Promise<string[]>` | All remaining lines to EOF; empty once this reader is closed. |
| `reader.on` | `on(event: "line" \| "close", listener: (value: string) => void): void` | Observes every completed line (`"line"`) or stdin EOF (`"close"`, once, with `""`). A tap: it never consumes a line a pending `readLine` also sees. |
| `reader.close` | `close(): void` | Detaches this reader: its listeners stop firing and its later reads resolve immediately. Stdin stays open for other consumers. |

The namespace object is closed: a member outside the table is a compile-time error with the documented names as candidates.

## Semantics

All reads delegate to the process family's stdin pump (`process.stdin`): one sibling reader thread and one buffered line queue shared with it. Lines are split on `\n` with a trailing `\r` stripped. The module has a default export only; `import { readLine } from "std/readline"` is rejected, use the default import form above.

## Limits

- One stdin consumer per program: `std/readline` and `process.stdin` share one pump and one buffered line queue, but `console.input()` / `console.readKey()` run their own stdin pump — mixing the console family with readline in one program races. Use one stdin consumer per program.
- No backpressure story: `readLines()` resolves only at EOF, and the loop is retained while the pump is alive — a program that pipes a never-ending stream into stdin does not reach the point of use.
- No readline-style editing, history, or completion: this module reads lines, it is not an interactive line editor.

## Related pages

- [Console](./01-console.md) — `console.input`, `console.readKey`, and the `keypress` / `line` / `close` stdin listeners
- [Process](./03-process.md) — `process.stdin`, the shared pump behind every read here
- [Time and stopwatches](./21-time.md) — monotonic timers for the same event loop
