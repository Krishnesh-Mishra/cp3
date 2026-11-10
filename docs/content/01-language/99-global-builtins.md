---
title: Global builtins
chapter: Chapter 1 · The language
---
# Global builtins

These names are always in scope: no import is needed, and a user binding with the same name shadows the builtin. Signatures below are the checked surface; the standard library chapters cover the imported modules. Closed surfaces (such as `Promise` and `EventEmitter`) diagnose an unknown member; see each table.

## `Object` statics

`Object.*` operates on object literals and class instances with statically known shapes (CP3 has no runtime property table). `Record<string, V>` canonicalizes to `Map<string, V>` (see [Collections](../03-standard-library/12-collections.md)).

| Member | Signature | Semantics |
| --- | --- | --- |
| `Object.keys` | `Object.keys(object: object): string[]` | The field names of a statically known shape. |
| `Object.values` | `Object.values(object: object): V[]` | The field values; `V` is the unified field type. |
| `Object.entries` | `Object.entries(object: object): [string, V][]` | `[name, value]` pairs, in field order. |
| `Object.assign` | `Object.assign(target, ...sources): target` | Copies fields from every source into `target` and returns it. `null`/`undefined` sources are ignored. A fresh literal target is turned into a new merged object (JS key order, later sources win); a reused binding target is mutated in place — its declared shape is unchanged, so a source with a new field is an error. Use the object spread `{...t, ...u}` to build a new shape. |
| `Object.is` | `Object.is(a: unknown, b: unknown): boolean` | Same-value equality: `NaN` equals `NaN`, `-0` is not `+0`, reference handles compare by identity; strings and booleans compare by value. |
| `Object.freeze` | `Object.freeze(o: T): T` | Marks `o` frozen (frozen implies sealed and non-extensible); returns `o`. Freeze state is tracked syntactically: the form `Object.assign(Object.freeze(o), ...)` throws a catchable TypeError at runtime; a frozen object reached through a variable is not tracked. |
| `Object.seal` | `Object.seal(o: T): T` | Marks `o` non-extensible and non-configurable; writable fields stay writable. Returns `o`. |
| `Object.preventExtensions` | `Object.preventExtensions(o: T): T` | Marks `o` non-extensible; existing fields stay writable and configurable. Returns `o`. |
| `Object.isFrozen` | `Object.isFrozen(o: T): boolean` | True when `o` is frozen (frozen implies sealed and non-extensible). |
| `Object.isSealed` | `Object.isSealed(o: T): boolean` | True when `o` is sealed or frozen. |
| `Object.isExtensible` | `Object.isExtensible(o: T): boolean` | False when `o` is frozen, sealed, or prevented. |
| `Object.getPrototypeOf` | `Object.getPrototypeOf(o: T): null` | CP3 objects have no prototype; always `null`. |
| `Object.setPrototypeOf` | `Object.setPrototypeOf(o: T, proto: null): T` | Only `null` is accepted as the prototype (CP3 has no prototype chain); returns `o`. |
| `Object.fromEntries` | `Object.fromEntries(entries: [string, V][]): Map<string, V>` | Builds the canonical `Map<string, V>` (the `Record<string, V>` type) from `[key, value]` entries. Keys must be strings. |
| `Object.groupBy` | `Object.groupBy(list: T[], callback: (item: T, index: number) => string): Map<string, T[]>` | Groups the items by the callback's string key (the canonical `Record<string, T[]>` type). For non-string keys use `Map.groupBy` (see [Collections](../03-standard-library/12-collections.md)). |

Freeze is tracked only for the syntactic forms codegen can see — a frozen object reached through a variable is not tracked — and `Object.freeze` is not a deep freeze of nested values. Pass values explicitly where the guarantee matters.

## `Error.captureStackTrace`

```ts
Error.captureStackTrace(error, fn?)
```

Refreshes `error.stack` from its current representation. CP3 captures no JavaScript stack in v0.2: `stack` is `name: message` at construction, and the captured text gains one `at <fn>` frame when the optional `fn` is given. The first argument must be an `Error`. Returns `void`. See [Errors and exceptions](06-modules-and-errors/02-errors.md).

## `EventEmitter`

`EventEmitter` is a language builtin (no import, unlike `std/events`, which does not exist). Construct one with `new EventEmitter()` (no constructor arguments).

| Member | Signature | Semantics |
| --- | --- | --- |
| `on` | `on(event: string, listener: (...args: unknown[]) => void): void` | Registers `listener`; fires on every matching `emit`. |
| `once` | `once(event: string, listener: (...args: unknown[]) => void): void` | Registers `listener`; fires at most once, then is removed. |
| `off` | `off(event: string, listener: (...args: unknown[]) => void): void` | Removes the listener that is the same box registered for the event. |
| `emit` | `emit(event: string, ...args: unknown[]): void` | Calls every listener registered for `event`, in order. |

An emit carries a list of values (up to 8); a listener takes the values it declares — a nullary listener takes none, and a listener that declares more parameters than were emitted sees `undefined` for the missing ones (JS parity). Listener identity is by box: two inline arrows are two different listeners, so `off("go", () => { ... })` never removes an earlier `on("go", () => { ... })`. Keep a named reference to the listener you want to remove.

## Timers and the event loop

No import is needed; the runtime drives them from the event loop. Timer callbacks take no cp3-level arguments.

| Member | Signature | Semantics |
| --- | --- | --- |
| `setTimeout` | `setTimeout(callback: () => void, delay: number): number` | Runs `callback` after `delay` milliseconds; returns a handle for `clearTimeout`. |
| `setInterval` | `setInterval(callback: () => void, interval: number): number` | Runs `callback` every `interval` milliseconds until cleared; returns a handle for `clearInterval`. |
| `clearTimeout` | `clearTimeout(handle: number): void` | Cancels a pending timeout. |
| `clearInterval` | `clearInterval(handle: number): void` | Stops an interval. |
| `sleep` | `sleep(ms: number): Promise<void>` | Resolves after `ms` milliseconds; `await sleep(ms)` suspends the current async context. |
| `queueMicrotask` | `queueMicrotask(callback: () => void): void` | Enqueues the callback on the microtask queue (drained before timers). |
| `setImmediate` | `setImmediate(callback: () => void): void` | Runs the callback on the next loop turn, via a zero-delay timer. |

Timer handles are plain `number`s. Pending timers keep the loop alive; a program with no pending work exits.

## `atob` and `btoa`

Base64 text ⇄ byte text, exactly one argument each:

| Member | Signature | Semantics |
| --- | --- | --- |
| `atob` | `atob(text: string): string` | Decodes base64 to byte text. Rejects malformed input with a catchable error. |
| `btoa` | `btoa(text: string): string` | Encodes byte text as base64. |

A CP3 string is UTF-8; for byte-oriented encoding beyond base64 use the [buffer codec](../03-standard-library/11-buffer.md).

## `structuredClone`

```ts
structuredClone(value)
```

Deep-copies the value and returns it; the result has the argument's type. It reuses the same deep copy a worker boundary performs (`cp3::runtime::clone`).

## `parseTo`

```ts
parseTo(value, keyword)   // parseTo("123", number)
parseTo<T>(value)         // parseTo<u16>(port)
```

Bidirectional scalar converter: the target is a static type — `number`/`string`/`boolean` in the two-argument form, or any scalar type (including a native numeric type) in the type-argument form — and the source may be any scalar. A literal source is checked against the target at compile time; a narrowing conversion to a native numeric type range-checks at runtime and throws on an out-of-range value. The target must be a scalar type, not an array or object.

## `parseInt` and `parseFloat`

Legacy JS-compatibility globals, always `number`:

| Member | Signature | Semantics |
| --- | --- | --- |
| `parseInt` | `parseInt(text: string): number` | Leading whitespace and an optional sign are skipped; a `0x`/`0X` prefix selects base 16, otherwise base 10. Stops at the first character that is not a digit; `NaN` when there are no digits. Takes exactly one argument — there is no radix parameter (a second argument is a program error). |
| `parseFloat` | `parseFloat(text: string): number` | Accepts the longest decimal prefix (digits, optional fraction and exponent); `NaN` when none. One argument. |

Use `parseTo<T>` for exact native widths; these two exist so JS code keeps the habits it arrives with.

## `isNaN` and `isFinite`

Both take one argument and coerce it with `Number(x)` semantics before testing (JS behavior, not strict `Number.isNaN`):

| Member | Signature | Semantics |
| --- | --- | --- |
| `isNaN` | `isNaN(value: unknown): boolean` | True when the coerced value is `NaN`. |
| `isFinite` | `isFinite(value: unknown): boolean` | True when the coerced value is finite. |

## Promise statics

`Promise` is the language's async primitive; the statics below combine or build promises. The operand of a combinator is an array of promises; plain values count as already-resolved promises.

| Member | Signature | Semantics |
| --- | --- | --- |
| `Promise.resolve` | `Promise.resolve(value): Promise<V>` | The promise carries the argument's type; passing an existing promise through unchanged (JS identity). |
| `Promise.reject` | `Promise.reject(reason): Promise<void>` | A rejected promise; CP3 types a rejection as carrying no value. |
| `Promise.all` | `Promise.all(array: Promise<V>[]): Promise<V[]>` | Resolves with every value in input order; rejects with the first rejection. |
| `Promise.race` | `Promise.race(array: Promise<V>[]): Promise<V>` | Settles with the first input to settle, fulfilled or rejected. |
| `Promise.any` | `Promise.any(array: Promise<V>[]): Promise<V>` | Resolves with the first fulfilled value; rejects with an `AggregateError` when every input rejected (or the array is empty). |
| `Promise.allSettled` | `Promise.allSettled(array: Promise<V>[]): Promise<{ ok: boolean, value: V, reason: Error }[]>` | Always resolves; each input is recorded as `{ ok, value, reason }` — `ok` distinguishes fulfilment from rejection, `reason` carries the rejection error. |
| `Promise.withResolvers` | `Promise.withResolvers<T>(): { promise: Promise<T>, resolve: (value: T) => void, reject: (reason: Error) => void }` | Returns the resolver pair alongside the promise, for bridging callbacks into async code. The type argument names the carried value; omitting it leaves the value type `unknown`. |

## Parallel closures and ranges

`parallel` is a bare builtin (no import) for CPU-bound work split across native threads. The first argument of `parallel.for` and `parallel.reduce` is a `start..end` range; the range is half-open (`start` included, `end` excluded), and `parallel.for` binds the induction variable to `number`:

```ts
parallel.for(0..n, (i: number) => {
  // body runs on pool threads
});

const sum = parallel.reduce(0..n, 0, (acc: number, i: number) => acc + i * i);
```

| Member | Signature | Semantics |
| --- | --- | --- |
| `parallel.for` | `parallel.for(range: Range, body: (i: number) => void): void` | Runs `body(i)` for each `i` in the half-open range `start..end`. |
| `parallel.reduce` | `parallel.reduce(range: Range, initial: U, body: (acc: U, i: number) => U): U` | Folds the range into one value; the accumulator starts at `initial` and matches its widened type. The first argument must be a `start..end` range. Codegen folds the range chunked with an ordered combine only when it proves the body pure and the arithmetic exact; otherwise the fold runs exactly left-to-right. |
| `parallel.run` | `parallel.run(...closures: (() => T)[]): T[]` | Runs each nullary closure and yields an array of their results, in argument order. A closure here takes no parameters and cannot be generic or async. |

The closures given to `parallel.*` are synchronous by contract: they cannot be `async` (their pool threads have no event loop) and `parallel.run` closures take no parameters and may not be generic. The checker rejects writes to captured state and unsynchronized shared mutation inside parallel closures (diagnostics in the `CP3901`–`CP39xx` family). Prefer a reduction value over mutation of a shared object, and measure before assuming speedup; see [Async and concurrency](../02-runtime/07-async-and-concurrency.md).

## Related pages

- [Errors and exceptions](06-modules-and-errors/02-errors.md)
- [Async and concurrency](../02-runtime/07-async-and-concurrency.md)
- [Standard library overview](../03-standard-library/00-overview.md)
