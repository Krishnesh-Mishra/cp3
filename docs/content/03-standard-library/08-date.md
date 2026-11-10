---
title: Dates and times
chapter: Chapter 3 · Standard library
---
# Dates and times

CP3's `Date` is a built-in class family. The static constructor operations are available through the `std/date` default namespace:

```ts
import Date from "std/date";

const started = Date.now();
```

Date values support formatting, component reads, comparisons, differences, and immutable arithmetic. An `addDays`-style method returns a new value; it does not mutate the original Date.

## Creating dates

### `Date.now()`

Returns a Date representing the current date and time.

### `Date.fromISOString(value)`

Parses an ISO-8601 string and returns a Date:

```ts
const release = Date.fromISOString("2026-10-30T08:00:00Z");
```

Supply a valid ISO timestamp. Invalid text is not a substitute for an optional value; handle parse failure according to the runtime's exception behavior.

## Formatting

- `toISOString()` produces an ISO-8601 string suitable for machine-readable storage and interchange.
- `toString()` produces a human-readable representation.

Prefer ISO output for logs and serialized data when stable formatting matters.

## Reading components

`getFullYear`, `getMonth`, `getDate`, `getHours`, `getMinutes`, and `getSeconds` return numeric components. `getMonth()` is zero-indexed: January is `0`, matching JavaScript's convention. `getDate()` is the day of the month. `getDay()` returns the day of the week, `0` = Sunday.

All Dates are UTC, so the `getUTC*` family returns exactly the same values as the non-UTC getters: `getUTCFullYear()`, `getUTCMonth()`, `getUTCDate()`, `getUTCDay()`, `getUTCHours()`, `getUTCMinutes()`, and `getUTCSeconds()` are the UTC getters, provided for symmetry with runtimes that expose both families.

Calendar components are not the same thing as a timestamp. Use `diffMs` for elapsed-time comparisons instead of subtracting separately read calendar fields.

## Setting components

The setters mutate the receiver in place and return the new timestamp (`i64` in Rust/C++ projects, `number` in TypeScript projects). `setMonth()` is zero-indexed. This is deliberately different from the `add*` family: `addDays(n)` returns a NEW Date and leaves the receiver untouched, while `setDate(n)` rewrites the same Date:

```ts
const release = Date.fromISOString("2026-10-30T08:00:00Z");
release.setFullYear(2027);      // returns the new timestamp; release is rewritten
const shifted = release.addDays(1); // new Date; release is also unchanged by addDays
```

| Method | Signature | Meaning |
| --- | --- | --- |
| `setFullYear` | `setFullYear(year: number): i64` | Set the UTC year in place; returns the new timestamp |
| `setMonth` | `setMonth(month: number): i64` | Set the UTC month (0-indexed) in place; returns the new timestamp |
| `setDate` | `setDate(day: number): i64` | Set the UTC day of the month in place; returns the new timestamp |
| `setHours` | `setHours(hour: number): i64` | Set the UTC hour in place; returns the new timestamp |
| `setMinutes` | `setMinutes(minute: number): i64` | Set the UTC minute in place; returns the new timestamp |
| `setSeconds` | `setSeconds(second: number): i64` | Set the UTC second in place; returns the new timestamp |

(Each returns `number` in TypeScript projects, and the timestamp width `i64` in Rust/C++ projects.)

## Arithmetic and comparison

`addDays(n)`, `addHours(n)`, `addMinutes(n)`, and `addSeconds(n)` return new Date values. `isBefore(other)` and `isAfter(other)` compare timestamps. `diffMs(other)` returns this date minus the other date, in milliseconds.

```ts
const tomorrow = release.addDays(1);
const elapsed = tomorrow.diffMs(release);
console.log(release.toISOString(), elapsed);
```

The original `release` remains unchanged after `addDays`.

## API index

Static: `now()`, `fromISOString(s)`, `parse(s)`. Instance: `toISOString()`, `toString()`, `getFullYear()`, `getMonth()`, `getDate()`, `getDay()`, `getHours()`, `getMinutes()`, `getSeconds()`, `getTime()`, `getUTCFullYear()`, `getUTCMonth()`, `getUTCDate()`, `getUTCDay()`, `getUTCHours()`, `getUTCMinutes()`, `getUTCSeconds()`, `setTime()`, `setFullYear()`, `setMonth()`, `setDate()`, `setHours()`, `setMinutes()`, `setSeconds()`, `addDays(n)`, `addHours(n)`, `addMinutes(n)`, `addSeconds(n)`, `isBefore(other)`, `isAfter(other)`, `diffMs(other)`.

Numeric Date values follow the project vocabulary. A `typescript` project keeps `number`; a `rust` or `cpp` project uses the exact standard-library width: `Date.parse(s)`, `getTime()`, `diffMs(other)`, `setTime(ms)` and every mutator's returned milliseconds are `i64` (`int64_t` in C++), and `setTime` takes that same `i64`. The calendar getters and the calendar parameters of `setFullYear` through `setSeconds` stay `number`.

## Full method reference

| Method | Signature | Meaning |
| --- | --- | --- |
| `Date.now` | `now(): Date` | Current date and time |
| `Date.fromISOString` | `fromISOString(s: string): Date` | Parse an ISO-8601 date string |
| `toISOString` | `toISOString(): string` | ISO-8601 representation |
| `toString` | `toString(): string` | Human-readable representation |
| `getFullYear` | `getFullYear(): number` | Calendar year |
| `getMonth` | `getMonth(): number` | Month, zero-indexed (`0` is January) |
| `getDate` | `getDate(): number` | Day of month |
| `getDay` | `getDay(): number` | Day of week (`0` = Sunday) |
| `getHours`, `getMinutes`, `getSeconds` | `(): number` | Corresponding time component |
| `getUTCFullYear`, `getUTCMonth`, `getUTCDate`, `getUTCDay`, `getUTCHours`, `getUTCMinutes`, `getUTCSeconds` | `(): number` | UTC accessors; identical to the non-UTC getters (all CP3 Dates are UTC) |
| `setFullYear`, `setMonth`, `setDate`, `setHours`, `setMinutes`, `setSeconds` | `(field: number): i64` | Set the component in place; returns the new timestamp (`number` in TypeScript projects) |
| `addDays`, `addHours`, `addMinutes`, `addSeconds` | `(n: number): Date` | New Date shifted by the requested amount |
| `isBefore`, `isAfter` | `(other: Date): boolean` | Compare date ordering |
| `getTime` | `getTime(): i64 (`number` in TypeScript projects)` | Milliseconds since the epoch |
| `setTime` | `setTime(ms: i64): i64` | Set this Date from a millisecond timestamp (`number`/`double` in TypeScript projects) |
| `diffMs` | `(other: Date): i64` | This timestamp minus the other's, in milliseconds |

`Date` is a built-in nominal type. Importing `std/date` provides the static namespace operations; instance methods operate on Date values. Unknown static and instance members are not an invitation to use the full JavaScript Date API.

## Date parsing and validation

`fromISOString` expects an ISO-8601 string. Treat untrusted input as untrusted: catch parse failures and report the source value or validation error at the boundary. A string that looks date-like is not automatically a valid timestamp.

Use `toISOString()` for stable machine-readable interchange. `toString()` is intended for human-readable display and should not be used as a serialization format unless its exact output is acceptable to the receiving system.

## Calendar fields versus elapsed time

A month number is zero-based, matching the JavaScript convention. A day-of-month is returned by `getDate`; it is not a day-of-year. Calendar components represent a calendar view of the Date, while `diffMs` compares timestamp values. Use the latter for elapsed durations rather than subtracting calendar components.

Do not assume a calendar day is always exactly 24 elapsed hours across timezone or daylight-saving transitions. The `addDays` and `addHours` operations represent different units and are implemented separately. If the application depends on a particular timezone calendar rule, verify that rule against the runtime and tests for the target platform.

## Arithmetic is immutable

`addDays`, `addHours`, `addMinutes`, and `addSeconds` return a new Date. The receiver is unchanged, so chaining is explicit:

```ts
const start = Date.fromISOString("2026-10-30T08:00:00Z");
const later = start.addDays(1).addHours(2);
```

This avoids a hidden mutation through an alias. Keep the original value when both instants are needed for comparison or reporting.

## Test and implementation boundary

`tests/cases/stdlib/02/date` covers signatures, ISO parsing/formatting, component reads, arithmetic, comparison, and differences. The semantic member lists live in `src/std/date/sema.cpp`; code generation and the built-in Date runtime define how those calls are represented. Use those sources and tests when documenting timezone or invalid-input behavior instead of assuming full ECMAScript Date parity.
