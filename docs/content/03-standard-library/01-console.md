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

## Output streams and compatibility limits

`console.log` and its related methods are complete calls, not writable stream objects. The current API does not define `console.stdout.write`, Node's `process.stdout`, ANSI styling helpers, or a general terminal event emitter. For process pipes, see [Process and child processes](03-process.md).

## Method summary

| Member | Result | Effect |
| --- | --- | --- |
| `log(...args)` | `void` | Write to stdout with newline |
| `info(...args)` | `void` | Write to stdout with newline |
| `warn(...args)` | `void` | Write to stderr with newline |
| `error(...args)` | `void` | Write to stderr with newline |
| `assert(condition, ...args)` | `void` | Write to stderr only when false |
| `input(prompt?)` | `Promise<string>` | Read one line; EOF gives empty string |
| `readKey()` | `Promise<string>` | Read one keypress |
| `on(event, listener)` | `void` | Register stdin listener |
| `off(event, listener)` | `void` | Remove stdin listener |
| `clear()` | `void` | Clear terminal |
| `hide()` / `show()` | `void` | Hide or restore Windows console window |
