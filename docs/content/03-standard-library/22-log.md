---
title: Logging
chapter: Chapter 3 · Standard library
---
# Logging

`std/log` provides a named, level-filtered logger. Each emitted call writes one synchronous text line, like `INFO app started`. There is no file transport, rotation or async queue in this release; output goes to stdout, or to a callback you install with `setSink`.

```ts
import { Logger } from "std/log";

const log = new Logger("app");
log.info("started");
log.warn("slow");
log.error("failed");
```

This prints:

```text
INFO app started
WARN app slow
ERROR app failed
```

## Levels

Levels are ordered `debug` < `info` < `warn` < `error`. A logger has a minimum level; a call below it prints nothing. The default is `info`, so `debug` stays silent until you opt in:

```ts
log.debug("hidden by default");
log.setLevel("debug");
log.debug("now shown"); // prints: DEBUG app now shown
log.setLevel("warn");
log.info("also hidden");
```

`setLevel` accepts the four names case-insensitively. An unknown name leaves the level unchanged. `log.level` reads the current level back as `"debug"`, `"info"`, `"warn"` or `"error"`.

```ts
console.log(log.level); // "warn" after the setLevel above
```

## Line format

Every emitted line is `LEVEL name message`, with extra arguments appended after spaces, like `console.log` arguments:

```ts
log.info("count", 42, "ok", true); // prints: INFO app count 42 ok true
```

- The level prefix is uppercase: `DEBUG`, `INFO`, `WARN`, `ERROR`.
- An unnamed logger (`new Logger()`) omits the name: `INFO count 42 ok true`. `log.name` returns the name, or `""` for an unnamed logger.
- Arguments render for `string` (raw), `number` (shortest exact spelling), `boolean`, `null` and `undefined`; objects and arrays are not specially formatted in this release.
- Each call is synchronous and flushed, so lines appear in call order. All levels write to stdout (there is no stderr split); tests capture stdout.

## Sinks

`setSink` replaces stdout: the callback receives each emitted line with no trailing newline. It is the intended test seam, and the place for a file or network transport later:

```ts
const lines: string[] = [];
log.setSink((line: string) => { lines.push(line); });
log.info("buffered"); // nothing on stdout; "INFO app buffered" is in lines
```

`resetSink()` restores stdout. A sink that throws is not caught; the exception propagates to the logging call site, so sinks should be total.

## Example

```ts
import { Logger } from "std/log";

const log = new Logger("server");
log.setLevel("debug");
log.debug("listening on", 8080);
log.info("ready");
```

```text
DEBUG server listening on 8080
INFO server ready
```

## API summary

| Member | Type | Description |
| --- | --- | --- |
| Member | Signature (exact types) | Semantics |
| --- | --- | --- |
| `new Logger()` | `Logger(name?: string): Logger` | Unnamed logger, level `info`. The constructor takes at most one argument (the name, must be a `string`); extra arguments are an arity error. |
| `name` | `string` | The constructor name (`""` when unnamed). |
| `level` | `string` | Current minimum level: `"debug"`, `"info"`, `"warn"`, or `"error"`. Default is `"info"`. |
| `setLevel` | `setLevel(level: "debug" \| "info" \| "warn" \| "error"): undefined` | Sets the minimum emitted level. Accepts the four names case-insensitively; an unknown name leaves the level unchanged. |
| `debug` | `debug(msg: string, ...args: object[]): undefined` | Emits one `DEBUG` line only when level is `debug`; otherwise silent. |
| `info` | `info(msg: string, ...args: object[]): undefined` | Emits one `INFO` line when level is `info` or `debug`. |
| `warn` | `warn(msg: string, ...args: object[]): undefined` | Emits one `WARN` line when level is `warn` or more verbose. |
| `error` | `error(msg: string, ...args: object[]): undefined` | Emits one `ERROR` line (always emitted). |
| `setSink` | `setSink(sink: (line: string) => undefined): undefined` | Replaces stdout: the callback receives each emitted line with no trailing newline. A sink that throws is not caught; it propagates to the logging call site, so sinks should be total. |
| `resetSink` | `resetSink(): undefined` | Restores the default stdout sink. |

## Limits

- Stdout only: all levels write to stdout (there is no stderr split). Install a callback with `setSink` to capture lines elsewhere (for example, in tests); there is no file transport, rotation, or async queue in this release. If you need lines on disk, write them yourself from a sink.
- No timestamps, source locations, colours or structured fields; the line is the whole record.
- No async sink over a channel (the ledger's optional async sink is deferred) and no file transport.
- No `fmt`-style format strings: arguments are appended, never substituted into the message. Arguments render for `string` (raw), `number` (shortest exact spelling), `boolean`, `null` and `undefined`; objects and arrays are not specially formatted.
- Spread arguments (`log.info(...parts)`) are not supported; pass arguments directly.

## Test and implementation boundary

The conformance cases under `tests/cases/stdlib/02/log` pin the line format, the default and filtered levels, the `level` property, argument rendering and the `setSink` seam. They run unbuilt until the module is wired (see the report in the implementation change). The implementation and semantic member table are under `src/std/log`; unknown members are compile-time errors rather than dynamic property access.
