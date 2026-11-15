---
title: Global functions
---
# Global functions

These names are always in scope — no import. A user binding with the same name shadows the builtin. See the [global builtins guide](../../language/99-global-builtins.md) for the full checked surface. (`console` is **not** a language builtin; it is [`std/console`](../../standard-library/01-console.md). `process.nextTick` is [`std/process`](../../standard-library/03-process.md), not a language global.)

## Parsing

| Member | Signature | Notes |
| --- | --- | --- |
| `parseInt` | `parseInt(text: string): number` | Skips leading whitespace and an optional sign; a `0x`/`0X` prefix selects base 16, otherwise base 10. Stops at the first non-digit character; `NaN` when there are no digits. Exactly one argument — **no radix parameter**; a second argument is a program error. |
| `parseFloat` | `parseFloat(text: string): number` | Accepts the longest decimal prefix (digits, optional fraction and exponent); `NaN` when none. One argument. |
| `parseTo` | `parseTo(value, number)` / `parseTo(value, string)` / `parseTo(value, boolean)` · `parseTo<T>(value): T` | Bidirectional scalar converter. A literal source is checked at compile time; a narrowing conversion to a native numeric type range-checks at runtime and throws `Error` on out-of-range or fractional values. The target must be scalar, not an array or object. See [Number](./number.md) for the scalar vocabulary and conversion rules. |

`parseInt`/`parseFloat` are legacy JS-compatibility globals and always return `number`, in every project `typeSystem`; use `parseTo<T>` when the native width matters.

## Testing numbers

| Member | Signature | Notes |
| --- | --- | --- |
| `isNaN` | `isNaN(value): boolean` | True when the coerced value is `NaN`. Argument is unconstrained; coerced with `Number(x)` semantics (JS behavior, **not** the strict `Number.isNaN`). |
| `isFinite` | `isFinite(value): boolean` | True when the coerced value is finite. Same coercion rule. |

## Coercions

| Member | Signature | Notes |
| --- | --- | --- |
| `Number` | `Number(value): number` | JS-style conversion of any argument. |
| `String` | `String(value): string` | Unconstrained argument; lowers onto `toString`. |
| `Boolean` | `Boolean(value): boolean` | JS-style truthiness conversion. |

Extra arguments to `Number`/`Boolean`/`isNaN`/`isFinite` are ignored, and a spread contributes its first element (JS parity).

## Cloning and base64

| Member | Signature | Notes |
| --- | --- | --- |
| `structuredClone` | `structuredClone(value): <same type>` | Deep-copies the value; the result has the argument's type. Works on any supported value shape: primitives, strings, arrays, records, class instances, and structures built from them. |
| `atob` | `atob(text: string): string` | Decodes base64 to byte text; one argument; rejects malformed input with a catchable error. |
| `btoa` | `btoa(text: string): string` | Encodes byte text as base64; one argument. |

A CP3 string is UTF-8; for byte-oriented encoding beyond base64 use the [buffer codec](../../standard-library/11-buffer.md).

## Timers and the microtask queue

Timer callbacks take no CP3-level arguments. Timer handles are plain `number`s. Pending timers keep the loop alive; a program with no pending work exits.

| Member | Signature | Notes |
| --- | --- | --- |
| `setTimeout` | `setTimeout(callback: () => undefined, delay: number): number` | Runs `callback` after `delay` milliseconds; returns a handle for `clearTimeout`. |
| `setInterval` | `setInterval(callback: () => undefined, interval: number): number` | Runs `callback` every `interval` milliseconds until cleared; returns a handle for `clearInterval`. |
| `clearTimeout` | `clearTimeout(handle: number): undefined` | Cancels a pending timeout. |
| `clearInterval` | `clearInterval(handle: number): undefined` | Stops an interval. |
| `sleep` | `sleep(ms: number): Promise<undefined>` | Resolves after `ms` milliseconds; `await sleep(ms)` suspends the current async context. |
| `queueMicrotask` | `queueMicrotask(callback: () => undefined): undefined` | Enqueues the callback on the microtask queue. |
| `setImmediate` | `setImmediate(callback: () => undefined): undefined` | Runs the callback on the next loop turn, after queued microtasks. |

`process.nextTick(callback)` exists on [`std/process`](../../standard-library/03-process.md) (queues before timers and promise continuations, FIFO) — it is not a language builtin.

## `Error.captureStackTrace`

```ts
Error.captureStackTrace(error, fn?)
```

Refreshes `error.stack` from its current representation. `stack` is `name: message` at construction; the captured text gains one `at <fn>` frame when `fn` is given. The first argument must be an `Error`. Returns nothing. See [Errors](../../language/06-modules-and-errors/02-errors.md).

## Related pages

- [Global builtins guide](../../language/99-global-builtins.md) — the checked surface this page summarizes
- [Number](./number.md) — `parseTo`, `Number.*` statics and methods
- [Promise](./promise.md) — `Promise.withResolvers`, combinators
- [Errors](../../language/06-modules-and-errors/02-errors.md)
