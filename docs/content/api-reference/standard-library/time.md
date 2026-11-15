---
title: Time
---
# Time

`std/time` measures elapsed time with a monotonic clock; neither it nor `console.time` uses the wall clock, so a system-clock adjustment never moves a measurement. Import the default namespace:

```ts
import time from "std/time";
```

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `time.stopwatch` | `stopwatch(): Stopwatch` | Creates a stopped `Stopwatch` whose `elapsed()` is `0`; `new Stopwatch()` is not part of the surface |
| `sw.start` | `start(): undefined` | (Re)starts from now, discarding previously accumulated time; calling on a running watch restarts it |
| `sw.stop` | `stop(): undefined` | Freezes the interval; later `elapsed()` reads return the frozen value every time; stopping a stopped watch does nothing |
| `sw.elapsed` | `elapsed(): number` | Milliseconds since `start()` as a fractional `number` (`f64`): live interval while running, frozen value after `stop()`, `0` before the first `start()`; monotonic, never negative |
| `sw.reset` | `reset(): undefined` | Stops the watch and zeroes the elapsed time |

`setTimeout`/`setInterval`/`setImmediate` are separate timer APIs, not stopwatch methods; `std/time` has no timeouts or scheduling. The namespace and `Stopwatch` values are closed — an undocumented member is a compile-time error. No method allocates or throws.

## `console.time` / `console.timeEnd`

Per-label timers on the same monotonic clock:

| Member | Signature | Notes |
| --- | --- | --- |
| `console.time` | `time(label?: string): void` | Records the start instant for `label` (default `default`); re-calling with the same label restarts the timer |
| `console.timeEnd` | `timeEnd(label?: string): void` | Prints `label: <ms>ms` (milliseconds, three decimals) to stdout and removes the label; for a label never started, prints nothing to stdout and writes `Timer "label" does not exist` to stderr |

Neither clock is ever moved by the wall clock; use [Date](./date.md) for wall-clock time and `process.hrtime()` (see [Process](./process.md)) for raw monotonic readings.

Behavior details: [guide](../../standard-library/21-time.md)
