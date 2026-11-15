---
title: Promise
---
# Promise

`Promise<T>` is CP3's language-level async primitive; no import. One event loop per thread, eager async semantics: calling an `async` function starts it immediately, `await` always suspends to a queued microtask, and continuations run FIFO. See the [async guide](../../internal-engineering/04-async-non-blocking-io.md) and [global builtins](../../language/99-global-builtins.md).

## Instance members

Handlers are checked against the promise's value type. `.then` receives the value (nullary for a promise that carries nothing), `.catch`/`.catchError` receives the rejection (the handler parameter is not type-checked; rejection reasons are `Error` handles — the throw-only-`Error` law applies), and `.finally`'s handler is nullary. `catch` is accepted as a spelling and lowers to `catchError` (`catch` is a keyword in C++); both are the same operation.

| Member | Signature | Notes |
| --- | --- | --- |
| `then` | `then(onFulfilled): Promise<…>` | Returns a **new** promise for the handler's result; the handler's own parameter is contextually typed. A returned promise is adopted (see below). |
| `catch` / `catchError` | `catch(onRejected): Promise<…>` | Observes a rejection; forwards the same value type downstream. Rejection reason is JS `any`; at runtime it is an `Error`. |
| `finally` | `finally(onFinally): Promise<…>` | Nullary handler; the rejection/value passes through unchanged. |

## Static members

| Member | Signature | Notes |
| --- | --- | --- |
| `Promise.resolve` | `Promise.resolve(value): Promise<V>` | Carries the argument's type; passing an existing promise through returns it unchanged (JS identity). |
| `Promise.reject` | `Promise.reject(reason): Promise<undefined>` | A rejected promise; rejection carries no value. Pass an `Error` as the reason. |
| `Promise.all` | `Promise.all(array: Promise<V>[]): Promise<V[]>` | Resolves with every value in input order; rejects with the first rejection. |
| `Promise.race` | `Promise.race(array: Promise<V>[]): Promise<V>` | Settles with the first input to settle, fulfilled or rejected. |
| `Promise.any` | `Promise.any(array: Promise<V>[]): Promise<V>` | Resolves with the first fulfilled value; rejects with an `AggregateError` when every input rejects (or the array is empty). |
| `Promise.allSettled` | `Promise.allSettled(array: Promise<V>[]): Promise<{ ok: boolean, value: V, reason: Error }[]>` | Always resolves; each input recorded as `{ ok, value, reason }`. The record destructures like a plain object record. |
| `Promise.withResolvers` | `Promise.withResolvers<T>(): { promise: Promise<T>, resolve: (value: T) => undefined, reject: (reason: Error) => undefined }` | Bridges callbacks into async code. Always supply the type argument — omitting it leaves the value type unresolved. |
| `new Promise<T>(executor)` | executor runs synchronously; `resolve: (value: T) => undefined` (nullary for `Promise<undefined>`/`Promise<void>`), `reject: (reason: Error) => undefined` | Rejection reasons are `Error`-typed. |

Combinators treat plain values as already-resolved promises, including mixed literals (`[1, Promise.resolve(2), 3]`); a mixed non-literal source is a compile error. Awaiting a non-promise yields the value itself.

## Adoption semantics

An ordinary `return p` where `p: Promise<T>` from an async function makes the returned promise adopt `p` — consumers observe `p`'s settlement, not a promise-of-a-promise. `await p` likewise unwraps: `await` on a promise yields its value (and on a non-promise, the value itself). Returning a promise from `.then` flattens the same way.

## `for await`

`for await (const x of xs)` binds the awaited value instead of the raw element: a promise-typed element resolves to its value (a promise of nothing resolves to `undefined`); anything else is its own value. Legal only inside an `async` function or at the top level. It is the ordinary `for...of` shape with the bound value awaited — not an async-iterator protocol. See [loops](../../language/03-control-flow/01-conditions-and-loops.md).

## Rejection behavior

A rejected promise that nobody observes is **fatal at loop drain** (an unhandled-rejection abort, not a warning). Caught rejections must be handled with `.catch`/`.catchError` or `try`/`catch` around an `await`; unobserved rejections cannot be silently dropped.

## Related pages

- [Async guide (internal)](../../internal-engineering/04-async-non-blocking-io.md) — event loop, microtasks, coroutine model
- [Global functions](./global-functions.md) — `sleep`, `setTimeout`, `queueMicrotask`
- [Errors](../../language/06-modules-and-errors/02-errors.md)
