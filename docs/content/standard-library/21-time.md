---
title: Time and stopwatches
chapter: Chapter 3 · Standard library
---
# Time and stopwatches

`std/time` measures elapsed time with a monotonic clock, and `console.time` /
`console.timeEnd` offer the same clock as per-label timers. Neither uses the
wall clock, so a system-clock adjustment never moves a measurement.

```ts
import time from "std/time";

const sw = time.stopwatch();
sw.start();
// ... work ...
sw.stop();
console.log(sw.elapsed());
```

## `time.stopwatch()`

Creates a new stopped `Stopwatch`. A fresh watch reports an elapsed time of
`0`.

## Stopwatch methods

- `start()` (re)starts the interval from now, discarding any previously
  accumulated time. Calling `start()` on a running watch restarts it.
- `stop()` freezes the interval: afterwards `elapsed()` returns the same
  value every time. Stopping a stopped watch does nothing.
- `elapsed()` returns milliseconds since `start()` as a fractional number:
  the live interval while running, the frozen value after `stop()`, and `0`
  before the first `start()`. It is monotonic, so it is never negative.
- `reset()` stops the watch and zeroes the elapsed time.

```ts
const sw = time.stopwatch();
sw.start();
const during = sw.elapsed(); // live value, >= 0
sw.stop();
const frozen = sw.elapsed(); // same value on every later read
sw.reset();
console.log(sw.elapsed() === 0); // true
```

The watch is two clock readings plus a flag: no method allocates or throws.
The clock is `std::chrono::steady_clock` (`QueryPerformanceCounter` on
Windows, `CLOCK_MONOTONIC` elsewhere) — the same monotonic-now primitive as
the `std/process` `hrtime` family, but as a reusable object.

## `console.time(label?)` and `console.timeEnd(label?)`

Per-label timers on the same monotonic clock (the `console.count` sibling):

```ts
console.time("load");
// ... work ...
console.timeEnd("load"); // prints: load: 12.345ms
```

- The label defaults to `default` when omitted.
- `time` records the start instant; calling it again with the same label
  restarts that timer.
- `timeEnd` prints `label: <ms>ms` (milliseconds, three decimals) to stdout
  and removes the label.
- `timeEnd` for a label that was never started prints nothing to stdout;
  it writes `Timer "label" does not exist` to stderr instead.

## API summary

| Member | Signature (exact types) | Semantics |
| --- | --- | --- |
| `time.stopwatch` | `stopwatch(): Stopwatch` | Creates a new stopped `Stopwatch` whose `elapsed()` is `0`. Construction is through this factory; `new Stopwatch()` is not part of the v0.2 surface. |
| `sw.start` | `start(): undefined` | (Re)starts the interval from now, discarding any previously accumulated time. Calling `start()` on a running watch restarts it. |
| `sw.stop` | `stop(): undefined` | Freezes the interval: afterwards `elapsed()` returns the same value every time. Stopping a stopped watch does nothing. |
| `sw.elapsed` | `elapsed(): number` | Milliseconds since `start()` as a fractional `number` (`f64`); the live interval while running, the frozen value after `stop()`, and `0` before the first `start()`. Monotonic, so never negative. There is no exact integer width for a duration. |
| `sw.reset` | `reset(): undefined` | Stops the watch and zeroes the elapsed time. |
| `console.time` | `time(label?: string): undefined` | Records the start instant for `label` (default `default`); calling again with the same label restarts that timer. |
| `console.timeEnd` | `timeEnd(label?: string): undefined` | Prints `label: <ms>ms` (three decimals) to stdout and removes the label. For a label never started, prints nothing to stdout and writes `Timer "label" does not exist` to stderr. |

The namespace object (`time.stopwatch`) and `Stopwatch` values are closed: a member outside the tables is a compile-time error with the documented names as candidates.

## Semantics

The clock is `std::chrono::steady_clock` (`QueryPerformanceCounter` on Windows, `CLOCK_MONOTONIC` elsewhere) — the same monotonic-now primitive as the `std/process` `hrtime` family, but as a reusable object. A watch is two clock readings plus a flag: no method allocates or throws. `elapsed()` on a running watch reads the live interval; on a stopped watch it returns the frozen value. The unit is milliseconds as a fractional `double` (nanosecond resolution). `console.time` / `console.timeEnd` are per-label timers on the same monotonic clock.

## Limits

- Monotonic only, never wall clock: a system-clock adjustment (NTP step, manual set, DST) never moves a measurement. Do not use `std/time` to read the current wall time, to schedule at a wall instant, or to compare across processes; use [Date](./08-date.md) for wall-clock time.
- No durations as integers: `elapsed()` is always `number`; there is no exact integer width for a duration.
- No timeouts or scheduling here: `sleep`, `setTimeout`, and `setInterval` are separate timer APIs, not stopwatch methods.
