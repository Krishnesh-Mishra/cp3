---
title: Date
---
# Date

CP3's `Date` is a built-in nominal class family. The `std/date` default namespace provides the static operations; instance methods operate on `Date` values:

```ts
import Date from "std/date";

const started = Date.now();
```

All CP3 Dates are UTC — the `getUTC*` family returns exactly the same values as the non-UTC getters. A Date is **not** mutated by the `add*` family (those return new Date values), while the `set*` family rewrites the receiver in place and returns the new timestamp. Unknown static or instance members are not the JavaScript Date API. `Date.parse(s)`, `getTime()`, `setTime(ms)`, `diffMs(other)` and the setters' returned milliseconds are `i64` in Rust/C++ projects and `number` in TypeScript projects; the calendar parameters and calendar getters stay `number`.

There is no `new Date(...)` constructor form in the v0.2 surface; create values with `Date.now()` or `Date.fromISOString(s)`. `Date.parse(s)` yields a raw millisecond timestamp, not a Date.

## Static members

| Member | Signature | Notes |
| --- | --- | --- |
| `now` | `now(): Date` | Current date and time |
| `fromISOString` | `fromISOString(s: string): Date` | Parses an ISO-8601 string; invalid input is not an optional value — handle parse failure |
| `parse` | `parse(s: string): i64` | Parses ISO-8601 into milliseconds since the epoch |

## Instance reads

| Member | Signature | Notes |
| --- | --- | --- |
| `toISOString` | `toISOString(): string` | ISO-8601 representation |
| `toString` | `toString(): string` | Human-readable representation |
| `getTime` | `getTime(): i64` | Milliseconds since the epoch |
| `getFullYear` | `getFullYear(): number` | Calendar year |
| `getMonth` | `getMonth(): number` | Month, zero-indexed (`0` is January) |
| `getDate` | `getDate(): number` | Day of the month |
| `getDay` | `getDay(): number` | Day of the week (`0` = Sunday) |
| `getHours`, `getMinutes`, `getSeconds` | `(): number` | Time components |
| `getUTCFullYear`, `getUTCMonth`, `getUTCDate`, `getUTCDay`, `getUTCHours`, `getUTCMinutes`, `getUTCSeconds` | `(): number` | Identical to the non-UTC getters (all Dates are UTC) |

## Instance writes — in place

| Member | Signature | Notes |
| --- | --- | --- |
| `setTime` | `setTime(ms: i64): i64` | Sets this Date from a millisecond timestamp |
| `setFullYear` | `setFullYear(year: number): i64` | Sets the UTC year in place |
| `setMonth` | `setMonth(month: number): i64` | Sets the UTC month (0-indexed) in place |
| `setDate` | `setDate(day: number): i64` | Sets the UTC day of the month in place |
| `setHours` | `setHours(hour: number): i64` | Sets the UTC hour in place |
| `setMinutes` | `setMinutes(minute: number): i64` | Sets the UTC minute in place |
| `setSeconds` | `setSeconds(second: number): i64` | Sets the UTC second in place |

Each returns the new timestamp (`number` in TypeScript).

## Arithmetic and comparison — immutable

| Member | Signature | Notes |
| --- | --- | --- |
| `addDays` | `addDays(n: number): Date` | New Date moved `n` days; receiver untouched |
| `addHours` | `addHours(n: number): Date` | New Date moved `n` hours; receiver untouched |
| `addMinutes` | `addMinutes(n: number): Date` | New Date moved `n` minutes; receiver untouched |
| `addSeconds` | `addSeconds(n: number): Date` | New Date moved `n` seconds; receiver untouched |
| `isBefore` | `isBefore(other: Date): boolean` | Compares timestamps |
| `isAfter` | `isAfter(other: Date): boolean` | Compares timestamps |
| `diffMs` | `diffMs(other: Date): i64` | This timestamp minus `other`'s, in milliseconds |

Calendar components are not a duration: use `diffMs` for elapsed time. `"std/date/types"` publishes `Date` for annotation positions.

Behavior details: [guide](../../standard-library/08-date.md)
