---
title: Console
---
# Console

`std/console` provides terminal output and input helpers. Import the default namespace:

```ts
import console from "std/console";
```

The surface is closed: a member outside the table is a compile-time error. Console methods are complete calls, not writable stream objects.

## Output

| Member | Signature | Notes |
| --- | --- | --- |
| `log` | `log(...args): void` | Writes printable values to stdout, space-separated, with a trailing newline |
| `info` | `info(...args): void` | Same stream and format as `log` (naming convenience) |
| `warn` | `warn(...args): void` | Writes to stderr with a trailing newline |
| `error` | `error(...args): void` | Writes to stderr with a trailing newline |
| `assert` | `assert(condition, ...args): void` | Writes the remaining values to stderr only when `condition` is false; never throws, not control flow |
| `dir` | `dir(value): void` | Prints the value the way `log` does; inspection-depth options are ignored in v0.2 |
| `trace` | `trace(...args): void` | Prints a `Trace:` line with the arguments; no stack trace in v0.2 |
| `group` | `group(...args): void` | Prints the arguments, then indents following stdout lines by two spaces |
| `groupEnd` | `groupEnd(): void` | Closes the innermost open group; never reduces indent below zero |
| `table` | `table(rows): void` | `(index) (value)` listing for an array, `key value` lines for an object literal (insertion order) |

Values print using the runtime's printable-value rules; a class may define `toString(): string` to control its appearance. Use `std/json` when output must be valid JSON.

## Reading input and stdin events

| Member | Signature | Notes |
| --- | --- | --- |
| `input` | `input(prompt?: string): Promise<string>` | Reads one line from stdin; resolves to `""` at EOF; writes the prompt before reading |
| `readKey` | `readKey(): Promise<string>` | One keypress; named keys (`"Enter"`, `"ArrowUp"`) by name, else the character; raw mode while pending |
| `on` | `on(event: "keypress" \| "line" \| "close", listener: (value: string) => void): void` | `keypress` per key; `line` per line (terminator removed); `close` once at EOF with `""` |
| `off` | `off(event: "keypress" \| "line" \| "close", listener): void` | Removes the exact registered listener; the parameter may be omitted |

## Counting and timers

| Member | Signature | Notes |
| --- | --- | --- |
| `count` | `count(label?: string): void` | Prints `label: N` (default label `default`), incrementing before printing |
| `countReset` | `countReset(label?: string): void` | Resets the counter for `label` (default `default`) |
| `time` | `time(label?: string): void` | Records the start instant for `label` (default `default`); re-calling restarts that timer |
| `timeEnd` | `timeEnd(label?: string): void` | Prints `label: <ms>ms` (three decimals) to stdout and removes the label; for a label never started, prints nothing and writes `Timer "label" does not exist` to stderr |

Timers read the same monotonic clock as `std/time`; see [Time](./time.md).

## Terminal controls

| Member | Signature | Notes |
| --- | --- | --- |
| `clear` | `clear(): void` | Clears the terminal screen |
| `hide` | `hide(): void` | Windows only: hides the owning console window; process continues in the background; no-op elsewhere |
| `show` | `show(): void` | Windows only: restores the hidden console window; no-op elsewhere |

There is no `console.stdout`/`console.write` surface and no styling helpers; use `process.stdout` / `process.stderr` for streams (see [Process](./process.md)). Mixing `console.input()`/`readKey()`/`on` with `process.stdin` in one program races on fd 0 — use one stdin consumer per program.

Behavior details: [guide](../../standard-library/01-console.md)
