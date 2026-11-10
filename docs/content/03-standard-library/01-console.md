---
title: Console
chapter: Chapter 3 · Standard library
---
# Console

`std/console` provides terminal output and a small set of input helpers. Import the default namespace:

```ts
import console from "std/console";
```

## Output

### `console.log(...args)` and `console.info(...args)`

Both write printable values to standard output, separate arguments with spaces, and append a newline. `info` is a naming convenience; it uses the same output stream.

```ts
console.log("loaded", 3, true);
console.info("Listening on", 8080);
```

Values are rendered using the runtime's printable-value rules. A class may define `toString(): string` to choose how its instances appear. Avoid assuming JSON formatting: use `std/json` when the output must be valid JSON.

### `console.warn(...args)` and `console.error(...args)`

These write to standard error and append a newline. They are useful for warnings and diagnostics that should remain separate from normal program output.

```ts
console.warn("Using the default configuration");
console.error("Could not open the input file");
```

Neither function throws or terminates the process by itself.

### `console.assert(condition, ...args)`

When `condition` is false, writes the remaining values to standard error. When true, it writes nothing. It does not throw and is not a control-flow assertion; explicitly check conditions that must stop the program.

## Terminal controls

### `console.clear()`

Clears the terminal screen using the runtime's terminal control sequence.

### `console.hide()` and `console.show()`

On Windows, `hide` hides the console window and `show` restores it. The process continues in the background while hidden. On platforms without a console window owned by the process, these calls are no-ops.

## Reading input

### `console.input(prompt?)`

Reads one complete line from standard input and returns `Promise<string>`:

```ts
const name = await console.input("Name: ");
console.log("Hello", name);
```

At end-of-file, the promise resolves to an empty string. The prompt is written before the read begins. Since the result is asynchronous, use `await` inside an async function or handle the returned promise.

### `console.readKey()`

Returns a promise for one keypress. Named keys are returned by name, such as `"Enter"` or `"ArrowUp"`; ordinary typed keys are returned as their character. The terminal is temporarily placed in raw mode while the read is pending.

```ts
const key = await console.readKey();
console.log("Pressed", key);
```

## Stdin events

### `console.on(event, listener)`

Registers a listener for `"keypress"`, `"line"`, or `"close"`:

```ts
const reportLine = (line: string): undefined => {
  console.log("input:", line);
};

console.on("line", reportLine);
```

`keypress` delivers individual key values. `line` delivers completed lines without their line terminator. `close` fires once when stdin reaches end-of-file and passes an empty string. A listener remains registered until removed.

### `console.off(event, listener)`

Removes the exact listener registered for that event. Keep a reference to the callback if it needs to be removed later:

```ts
console.off("line", reportLine);
```

Registering a different function with the same body does not remove the original callback. The listener's declared parameter may be omitted if the event value is not needed.

## Counting calls

### `console.count(label?)`

Prints `label: N` for a per-label counter (default label `default`) to standard output. Each call increments the counter before printing:

```ts
console.count("hits"); // hits: 1
console.count("hits"); // hits: 2
console.count();       // default: 1
```

### `console.countReset(label?)`

Resets the counter for `label` (default label `default`), so the next `count` prints `1` again:

```ts
console.count("hits");      // hits: 1
console.countReset("hits");
console.count("hits");      // hits: 1
```

`count` and `countReset` are real members of the `std/console` namespace (verified run-mode cases live under `tests/cases/stdlib/01/console/helpers`).

## Debugging helpers

### `console.dir(value)`

Prints the value the way `log` does (objects in field order). Inspection depth options are ignored in v0.2.

### `console.trace(...args)`

Prints a `Trace:` line with the arguments. No stack trace is printed in v0.2.

### `console.group(...args)` and `console.groupEnd()`

`group` prints the arguments, then indents every following stdout line by two spaces. `groupEnd` closes the innermost open group and never reduces the indent below zero. Nesting works:

```ts
console.group("outer");
console.group("inner");
console.log("deep");
console.groupEnd();
console.log("less deep");
console.groupEnd();
console.log("back to normal");
```

### `console.table(rows)`

Prints an `(index) (value)` header followed by one `i value` line per array element; given an object literal instead of an array, it prints one `key value` line per field, in insertion order. Only formattable elements are supported (the same values `log` can print).

## Output streams and compatibility limits

`console.log` and its related methods are complete calls, not writable stream objects. The console surface does not define `console.stdout.write`, ANSI styling helpers, or a general terminal event emitter. For writable/readable process streams (`process.stdout` / `process.stderr` / `process.stdin`), see [Process and child processes](03-process.md).

## Method summary

| Member | Result | Effect |
| --- | --- | --- |
| `log(...args)` | `void` | Write to stdout with newline |
| `info(...args)` | `void` | Write to stdout with newline |
| `warn(...args)` | `void` | Write to stderr with newline |
| `error(...args)` | `void` | Write to stderr with newline |
| `assert(condition, ...args)` | `void` | Write to stderr only when false |
| `count(label?)` | `void` | Print `label: N`, increment the per-label counter (default `default`) before printing |
| `countReset(label?)` | `void` | Reset the counter for `label` (default `default`) |
| `dir(value)` | `void` | Print a value like `log` (objects in field order) |
| `trace(...args)` | `void` | Print a `Trace:` line with the arguments (no stack in v0.2) |
| `group(...args)` | `void` | Print the arguments, then indent following stdout lines by two spaces |
| `groupEnd()` | `void` | Close the innermost open group (never below zero indent) |
| `table(rows)` | `void` | Print `(index) (value)` listing for an array, or `key value` lines for an object literal |
| `input(prompt?)` | `Promise<string>` | Read one line; EOF gives empty string |
| `readKey()` | `Promise<string>` | Read one keypress |
| `on(event, listener)` | `void` | Register stdin listener |
| `off(event, listener)` | `void` | Remove stdin listener |
| `clear()` | `void` | Clear terminal |
| `hide()` / `show()` | `void` | Hide or restore Windows console window |
| `time(label?)` / `timeEnd(label?)` | `void` | Per-label monotonic timers — see [Time and stopwatches](21-time.md) |
