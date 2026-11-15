---
title: Readline
---
# Readline

`std/readline` reads lines from standard input; default namespace import, no named exports (`import { readLine } from "std/readline"` is rejected):

```ts
import readline from "std/readline";
```

Every read resolves rather than rejecting — EOF is the normal end of stdin. `\r\n` counts as one terminator (a trailing `\r` is stripped). All reads delegate to the same stdin pump `process.stdin` uses, so the two share one buffered line queue.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `readline.readLine` | `readLine(): Promise<string>` | Next line, terminator removed; `""` on EOF instead of rejecting; successive calls never lose bytes |
| `readline.readLines` | `readLines(): Promise<string[]>` | Every remaining line to EOF; resolves only at EOF (keeps the event loop alive while pending) |
| `readline.readAll` | `readAll(): Promise<string>` | All remaining stdin bytes as one string; never rejects |
| `readline.createReader` | `createReader(): LineReader` | A reader over stdin for lines as they arrive |

## `LineReader`

| Member | Signature | Notes |
| --- | --- | --- |
| `reader.readLine` | `readLine(): Promise<string>` | "" on EOF or once this reader is closed |
| `reader.readLines` | `readLines(): Promise<string[]>` | Empty once this reader is closed |
| `reader.on` | `on(event: "line" \| "close", listener: (value: string) => void): void` | `"line"` per completed line (terminator removed); `"close"` once at EOF with `""`; a tap — never consumes a line a pending `readLine` also sees; the listener may take no parameter |
| `reader.close` | `close(): void` | Detaches this reader — its listeners stop firing and its later reads resolve immediately; stdin stays open for other consumers; the reader handle is copyable (the closed flag is shared) |

One stdin consumer per program: `console.input()`/`readKey()` run their own pump and racing with readline on the same fd is undefined for your output. No editing, history, completion or backpressure; `readLines()` only resolves at EOF.

Behavior details: [guide](../../standard-library/22-readline.md)
