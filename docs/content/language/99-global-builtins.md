---
title: Global builtins
---
# Global builtins

These names are always in scope: no import is needed, and a user binding with the same name shadows the builtin. Signatures below are the checked surface; the standard-library chapters cover the imported modules. Closed surfaces (the collection classes, `EventEmitter`, the `std/*` modules) report an unknown member (`CP3305`) with candidate names; see each table.

## Collection constructors (ambient)

`Map`, `Set`, `LRUCache`, `MultiMap`, `ConcurrentMap`, `Queue`, `Stack`, `PriorityQueue`, `RingBuffer`, and `BitSet` are ambient builtins: values and type annotations resolve with no import, mirroring TypeScript. `import { Map } from "std/collections"` remains valid for source compatibility and narrows identically:

```ts
const map = new Map<string, number>();
map.set("a", 1);
const set = new Set<string>(["x", "y"]);
console.log(map.get("a"));
```

`Promise`, `EventEmitter`, and the typed-array constructors (`Int32Array`, …) are ambient the same way; `std/*/types` modules (for example `std/url/types`, `std/buffer/types`, `std/date/types`) exist for annotation-only imports of standard-library type names. Type-only imports exercise no runtime loader; they cannot be used as values.

A `Map` can be a class field with a declared type, and its `entries()` array (pairs spelled `[K, V]`) is storable, iterable, and spreadable:

```ts
class Holder {
  items: Map<string, number> = new Map<string, number>();
}
const h = new Holder();
for (const [k, v] of h.items.entries()) {
  console.log(`${k}=${v}`);
}
```

## `Object` statics

`Object.*` operates on object literals and class instances with statically known shapes (CP3 has no runtime property table). `Record<string, V>` canonicalizes to `Map<string, V>` (see [Collections](../standard-library/12-collections.md)).

| Member | Signature | Semantics |
| --- | --- | --- |
| `Object.keys` | `Object.keys(object: object): string[]` | The field names of a statically known shape. |
| `Object.values` | `Object.values(object: object): V[]` | The field values; `V` is the unified field type. |
| `Object.entries` | `Object.entries(object: object): [string, V][]` | `[name, value]` pairs, in field order. |
| `Object.assign` | `Object.assign(target, ...sources): target` | Copies fields from every source into `target` and returns it. `null`/`undefined` sources are ignored. A fresh literal target is turned into a new merged object (JS key order, later sources win); a reused binding target is mutated in place — its declared shape is unchanged, so a source with a new field is an error. Use the object spread `{...t, ...u}` to build a new shape. |
| `Object.is` | `Object.is(a, b): boolean` | Same-value equality: `NaN` equals `NaN`, `-0` is not `+0`, reference handles compare by identity; strings and booleans compare by value. |
| `Object.freeze` | `Object.freeze(o: T): T` | Marks `o` frozen (frozen implies sealed and non-extensible); returns `o`. Freeze state is tracked syntactically: the form `Object.assign(Object.freeze(o), ...)` throws a catchable TypeError at runtime; a frozen object reached through a variable is not tracked. |
| `Object.seal` | `Object.seal(o: T): T` | Marks `o` non-extensible and non-configurable; writable fields stay writable. Returns `o`. |
| `Object.preventExtensions` | `Object.preventExtensions(o: T): T` | Marks `o` non-extensible; existing fields stay writable and configurable. Returns `o`. |
| `Object.isFrozen` | `Object.isFrozen(o: T): boolean` | True when `o` is frozen (frozen implies sealed and non-extensible). |
| `Object.isSealed` | `Object.isSealed(o: T): boolean` | True when `o` is sealed or frozen. |
| `Object.isExtensible` | `Object.isExtensible(o: T): boolean` | False when `o` is frozen, sealed, or prevented. |
| `Object.getPrototypeOf` | `Object.getPrototypeOf(o: T): null` | CP3 objects have no prototype; always `null`. |
| `Object.setPrototypeOf` | `Object.setPrototypeOf(o: T, proto: null): T` | Only `null` is accepted as the prototype (CP3 has no prototype chain); returns `o`. |
| `Object.fromEntries` | `Object.fromEntries(entries: [string, V][]): Map<string, V>` | Builds the canonical `Map<string, V>` (the `Record<string, V>` type) from `[key, value]` entries. Keys must be strings. |
| `Object.groupBy` | `Object.groupBy(list: T[], callback: (item: T, index: number) => string): Map<string, T[]>` | Groups the items by the callback's string key (the canonical `Record<string, T[]>` type). For non-string keys use `Map.groupBy` (see [Collections](../standard-library/12-collections.md)). |

Freeze is tracked only where the checker can see the freeze form directly — a frozen object reached through a variable is not tracked — and `Object.freeze` is not a deep freeze of nested values. Pass values explicitly where the guarantee matters.

## `Error.captureStackTrace`

```ts
Error.captureStackTrace(error, fn?)
```

Refreshes `error.stack` from its current representation. CP3 captures no JavaScript stack in v0.2: `stack` is `name: message` at construction, and the captured text gains one `at <fn>` frame when the optional `fn` is given. The first argument must be an `Error`. Returns nothing. See [Errors and exceptions](./06-modules-and-errors/02-errors.md).

## `EventEmitter`

`EventEmitter` is a language builtin (no import). Construct one with `new EventEmitter()` — or `new EventEmitter<Events>()` for a typed bus. The untyped form's members:

| Member | Signature | Semantics |
| --- | --- | --- |
| `on` | `on(event: string, listener): undefined` | Registers `listener`; fires on every matching `emit`. |
| `once` | `once(event: string, listener): undefined` | Registers `listener`; fires at most once, then is removed. |
| `off` | `off(event: string, listener): undefined` | Removes the listener that is the same box registered for the event. |
| `emit` | `emit(event: string, ...args): undefined` | Calls every listener registered for `event`, in order. |
| `onAny` | `onAny(listener: (name: string, ...args) => undefined): undefined` | Wildcard channel; only legal on a map with an index signature; fires for events outside the declared member set. |
| `offAny` | `offAny(listener): undefined` | Removes a wildcard listener. |
| `isValidEventName` | `isValidEventName(name: string): boolean` | True when `name` is declared in the Events map (or any name, with an index signature); narrows `name` inside the guarded branch. |

In the untyped form, payloads are not checked; annotate the listener parameters where you need them. Listener and payload checking comes from the typed form below.

An emit carries a list of values (up to 8); a listener takes the values it declares — a nullary listener takes none, and a listener that declares more parameters than were emitted sees `undefined` for the missing ones (JS parity). Listener identity is by box: two inline arrows are two different listeners, so `off("go", () => { ... })` never removes an earlier `on("go", () => { ... })`. Keep a named reference to the listener you want to remove.

```ts
const bus = new EventEmitter();
bus.on("go", (a: string, b: number) => {
  console.log(`${a} ${b}`);
});
bus.emit("go", "x", 1);
```

### Typed buses: `EventEmitter<Events>`

With an interface `Events` map, the bus checks `on`/`once`/`off`/`emit` against the map at compile time:

```ts
interface Msg {
  id: number;
  text: string;
}

interface Events {
  hello: (m: Msg) => undefined;
  login: (userId: string) => undefined;
  logout: () => undefined;
}

const bus = new EventEmitter<Events>();
bus.on("hello", (m) => console.log(m.text)); // m is Msg
bus.emit("hello", { id: 1, text: "hi" });
bus.on("the-event", (ev) => console.log(ev)); // rejected: CP3305 (no such event)
bus.emit("hello", "wrong payload");          // rejected: CP3401 (payload type mismatch)
```

- The event name must name a member of the map (a string literal, a literal-typed variable, or an all-literal union), or the map must declare an index signature — `[name: string]: (m: Msg) => undefined` — which accepts any dynamic name.
- Listener parameters are contextually typed from the map: `(m) => ...` resolves `m` to the map's listener parameter type. Annotations must agree with the map (mutual assignability) and a handler may not declare more parameters than the map provides.
- Unknown event names are rejected with `CP3305`; a wrong payload count with `CP3403`; a payload or listener shape that does not fit the map's listener signature with `CP3401`.
- A dynamic name on a member-only map is rejected at the `emit` site with `CP3401` ("the Events map has concrete event names"); guard it with `if (bus.isValidEventName(name))` (the guarded branch narrows `name`) or wrap the emit in `try`/`catch`.
- A map with an index signature enables the wildcard listeners `onAny`/`offAny`; on a member-only map they are rejected (all events are named, so a wildcard can never fire).
- `bus.emit("hello", { ... })` with a fresh literal is checked against the map's parameter types, field by field.
- `bus.isValidEventName("hello")` returns `boolean` for dynamic names and narrows the argument in the guarded branch.

## Timers and the event loop

No import is needed; the runtime drives them from the event loop. Timer callbacks take no cp3-level arguments.

| Member | Signature | Semantics |
| --- | --- | --- |
| `setTimeout` | `setTimeout(callback: () => undefined, delay: number): number` | Runs `callback` after `delay` milliseconds; returns a handle for `clearTimeout`. |
| `setInterval` | `setInterval(callback: () => undefined, interval: number): number` | Runs `callback` every `interval` milliseconds until cleared; returns a handle for `clearInterval`. |
| `clearTimeout` | `clearTimeout(handle: number): undefined` | Cancels a pending timeout. |
| `clearInterval` | `clearInterval(handle: number): undefined` | Stops an interval. |
| `sleep` | `sleep(ms: number): Promise<undefined>` | Resolves after `ms` milliseconds; `await sleep(ms)` suspends the current async context. |
| `queueMicrotask` | `queueMicrotask(callback: () => undefined): undefined` | Enqueues the callback on the microtask queue. |
| `setImmediate` | `setImmediate(callback: () => undefined): undefined` | Runs the callback on the next loop turn, after queued microtasks. |

Timer handles are plain `number`s. Pending timers keep the loop alive; a program with no pending work exits.

## `atob` and `btoa`

Base64 text ⇄ byte text, exactly one argument each:

| Member | Signature | Semantics |
| --- | --- | --- |
| `atob` | `atob(text: string): string` | Decodes base64 to byte text. Rejects malformed input with a catchable error. |
| `btoa` | `btoa(text: string): string` | Encodes byte text as base64. |

A CP3 string is UTF-8; for byte-oriented encoding beyond base64 use the [buffer codec](../standard-library/11-buffer.md).

## `structuredClone`

```ts
structuredClone(value)
```

Deep-copies the value and returns it; the result has the argument's type. Works on any supported value shape — primitives, strings, arrays, records, class instances, and structures built from them.

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
| `isNaN` | `isNaN(value): boolean` | True when the coerced value is `NaN`. |
| `isFinite` | `isFinite(value): boolean` | True when the coerced value is finite. |

## Promise statics

`Promise` is the language's async primitive; the statics below combine or build promises. The operand of a combinator is an array of promises; plain values count as already-resolved promises, including mixed literals (`[1, Promise.resolve(2), 3]`). A mixed non-literal source is rejected with a clear diagnostic. Handlers are checked against the promise's value type: `.then` receives the value (nullary for a promise that carries nothing), `.catch` receives the rejection (its rejection carry is `Error` at runtime; the handler parameter itself is not type-checked), and `.finally`'s handler is nullary. Rejection reasons are `Error` handles — the throw-only-`Error` law applies there too.

| Member | Signature | Semantics |
| --- | --- | --- |
| `Promise.resolve` | `Promise.resolve(value): Promise<V>` | The promise carries the argument's type; passing an existing promise through unchanged (JS identity). |
| `Promise.reject` | `Promise.reject(reason): Promise<undefined>` | A rejected promise; a rejection carries no value. Pass an `Error` as the reason — only `Error` handles flow into the rejection path. |
| `Promise.all` | `Promise.all(array: Promise<V>[]): Promise<V[]>` | Resolves with every value in input order; rejects with the first rejection. |
| `Promise.race` | `Promise.race(array: Promise<V>[]): Promise<V>` | Settles with the first input to settle, fulfilled or rejected. |
| `Promise.any` | `Promise.any(array: Promise<V>[]): Promise<V>` | Resolves with the first fulfilled value; rejects with an `AggregateError` when every input rejected (or the array is empty). |
| `Promise.allSettled` | `Promise.allSettled(array: Promise<V>[]): Promise<{ ok: boolean, value: V, reason: Error }[]>` | Always resolves; each input is recorded as `{ ok, value, reason }` — `ok` distinguishes fulfilment from rejection, `reason` carries the rejection error. The record destructures like a plain object record. |
| `Promise.withResolvers` | `Promise.withResolvers<T>(): { promise: Promise<T>, resolve: (value: T) => undefined, reject: (reason: Error) => undefined }` | Returns the resolver pair alongside the promise, for bridging callbacks into async code; the record destructures like a plain object record. The type argument names the carried value — always supply it; omitting it leaves the value type unresolved. |

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
| `parallel.for` | `parallel.for(range: Range, body: (i: number) => undefined): undefined` | Runs `body(i)` for each `i` in the half-open range `start..end`. The induction variable is `number`. |
| `parallel.reduce` | `parallel.reduce(range: Range, initial: U, body: (acc: U, i: number) => U): U` | Folds the range into one value; the accumulator starts at `initial` and matches its widened type. The first argument must be a `start..end` range. |
| `parallel.run` | `parallel.run(...closures: (() => T)[]): T[]` | Runs each nullary closure and yields an array of their results, in argument order. All closures must return the same type (`CP3904`); a closure here takes no parameters and cannot be generic or async. |

The closures given to `parallel.*` are synchronous by contract: they cannot be `async` (`CP3912`; their pool threads have no event loop) and `parallel.run` closures take no parameters and may not be generic. The checker rejects writes to captured state (`CP3902`) and unsynchronized shared or indexed mutation of captured state (`CP3901`) inside parallel closures; unknown `parallel` members are rejected (`CP3305`), as are arity mismatches (`CP3403`) and a missing or ill-typed range (`CP3401`). Prefer a reduction value over mutation of a shared object, and measure before assuming speedup; see [Async and concurrency](../internal-engineering/04-async-non-blocking-io.md).

## Related pages

- [Errors and exceptions](./06-modules-and-errors/02-errors.md)
- [Async and concurrency](../internal-engineering/04-async-non-blocking-io.md)
- [Standard library overview](../standard-library/00-overview.md)
