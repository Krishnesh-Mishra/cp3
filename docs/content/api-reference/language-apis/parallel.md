---
title: parallel
---
# parallel

`parallel` is a bare builtin — no import — for CPU-bound work split across native threads. It is not `async`: `parallel.*` closures run on pool threads with no event loop. Ranges are half-open (`start` included, `end` excluded). See [Global builtins](../../language/99-global-builtins.md) for the checked signatures and [the async guide](../../internal-engineering/04-async-non-blocking-io.md) for the loop model.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `parallel.for` | `parallel.for(range: Range, body: (i: number) => undefined): undefined` | Runs `body(i)` for each `i` in `start..end`; the induction variable is `number`. |
| `parallel.reduce` | `parallel.reduce(range: Range, initial: U, body: (acc: U, i: number) => U): U` | Folds the range into one value; `acc` starts at `initial` and takes its widened type. The first argument must be a `start..end` range. |
| `parallel.run` | `parallel.run(...closures: (() => T)[]): T[]` | Runs each nullary closure on the pool; results are yielded as a `T[]` in argument order. All closures must return the same type (`CP3904`); closures take no parameters and may not be generic or `async`. |

```ts
parallel.for(0..n, (i: number) => {
  // body runs on pool threads
});

const sum = parallel.reduce(0..n, 0, (acc: number, i: number) => acc + i * i);
const results = parallel.run(() => computeA(), () => computeB());
```

## The no-async contract

`parallel.*` closures are synchronous by contract:

- They cannot be `async` — `CP3912` (pool threads have no event loop; an `await` would never be driven).
- The checker rejects writes to captured state (`CP3902`) and unsynchronized shared or indexed mutation of captured state (`CP3901`) inside parallel closures.
- Unknown `parallel` members are rejected (`CP3305`); arity mismatches (`CP3403`); a missing or ill-typed range (`CP3401`).
- `parallel.run` closures take no parameters and may not be generic.

Prefer returning a reduction value over mutating captured state. When the accumulator is not one of the provable combine shapes (integer-exact `acc + rhs` with `initial: 0`, `acc * rhs` with `initial: 1`, or the max/min conditional shape) the reduction falls back to sequential evaluation — an array-accumulator `(acc, i) => { acc.push(x); return acc; }` body does not parallelize.

`parallel.for` returns `undefined`; use `parallel.run` to collect per-closure results, or `parallel.reduce` for a single folded value.

## Diagnostics

| Code | Trigger |
| --- | --- |
| `CP3305` | Unknown `parallel` member. |
| `CP3401` | Missing or ill-typed range (first argument). |
| `CP3403` | Arity mismatch for the member. |
| `CP3901` | Unsynchronized shared or indexed mutation of captured state. |
| `CP3902` | Write to captured state. |
| `CP3904` | `parallel.run` closures return different types. |
| `CP3912` | Closure given to `parallel.*` is `async` (or generic where not allowed). |

## Related pages

- [Global builtins guide](../../language/99-global-builtins.md) — worked examples
- [Async guide (internal)](../../internal-engineering/04-async-non-blocking-io.md) — `parallel.*` vs the event loop and workers
- [Concurrency diagnostics](../../api-reference/error-codes/00-overview.md) — code list overview
