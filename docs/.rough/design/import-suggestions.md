# Std import suggestions (undefined-name diagnostics)

## Problem

Using a std name without its import (`Math.random()`, `new Logger()`,
`new URL(...)`, `fetch(...)`) used to be a bare `cannot find name 'X'`.
The fix must give the exact import line to paste, on the CLI path
(not LSP-only), for both default and named modules.

## How it works (existing wiring, reused)

1. Every undefined-identifier path in sema goes through
   `Analyzer::reportUndefinedName` (`src/sema/src/semantic_analyzer.cpp`,
   callers in `expr_check.cpp`, `ops_check.cpp`, `call_check.cpp`).
   It calls `stdImportHint(name)` (`src/sema/src/checks/type/std_check.cpp`),
   appends `` ; add `import ... from "std/...";` `` to the **message**
   and pushes `Add \`import ...\`` into `Diagnostic::suggestions`.
2. CLI (`src/cli/src/toolchain/runtime/diagnostics.cpp`): the D2 machine
   line renders `d.message` verbatim, so the exact import line is on
   stderr in non-TTY runs with no new plumbing. In pretty mode the
   suggestions render as `help:` lines; a new pretty-only fallback in
   `renderDiagnostics` extracts an `import ...;` substring from the
   message and prints it as `help: Add \`...\`` if no suggestion already
   showed it. No machine line is added or altered (D2 contract intact).
3. LSP auto-import shares the same table via the same diagnostic, so
   CLI and editor can no longer drift apart.

## Hint coverage (`stdImportHint`, std_check.cpp)

Default (`import X from "std/...";`): `Math`, `JSON`, `console`, `path`,
`os`, `crypto`, `fs`, `io`, `Date`, `process`, `url`, `time`, `hash`.

Named (`import { X } from "std/...";`): `fetch`, `createServer`,
`WebSocket`, `WebSocketServer`, `lookup` (`std/network`); `Buffer`
(`std/buffer`); `env` (`std/env`); `Map`, `Set`, `LRUCache`
(`std/collections`); `URL`, `URLSearchParams` (`std/url`);
`TextEncoder`, `TextDecoder` (`std/text`); `Logger` (`std/log`).

Removed: `Mutex`, `Queue`, `Stack`, `PriorityQueue`, `RingBuffer`,
`BitSet`, `MultiMap` — no such `std/collections` export exists in sema,
codegen or docs; hinting them would only produce a CP3604 error next.

Deliberately absent: `std/network` default (registry has a default slot
but no documented default binding name; documented surface is named),
`Stopwatch` (only reachable as `time.stopwatch()`, never imported
directly), and ambient globals that need no import (`Object`, `String`,
`Promise`, timers, typed arrays, `EventEmitter`, ...).

## Wiring needed (outside this change's file scope — needs owner)

- `src/modules/src/support/std_registry.cpp`: add `std/log`, `std/time`,
  `std/hash`, `std/text` to `kStdModules` (log/text named-only, time/hash
  default), otherwise the suggested import still fails import validation
  (CP3601/CP3604) until the registry lands. Without this, hints for
  `Logger`/`time`/`hash`/`TextEncoder` are correct-but-not-yet-accepted.
- `src/std/{log,time,hash,text}` sema dispatch rows in
  `std_check.cpp`/`member_check.cpp` (family owners): needed so the
  imported names actually type-check after the import is added.
- Tests (runner, not added here): cases asserting the machine line for
  e.g. `new Logger()` without import contains
  `import { Logger } from "std/log";`, and `url.encodeURI` without import
  contains `import url from "std/url";`.

## Limitations

- Hint fires only on the undefined-identifier path. Misspelled names,
  member misses on an imported namespace (`console.lgo`), and wrong
  import specifiers use their own diagnostics (CP3305/CP3601/CP3604) and
  do not repeat the import hint.
- One name maps to one hint; a name that could come from two modules
  (none today) would need disambiguation.
- The pretty `help:` fallback is a plain substring scan for `import ...;`
  in the message; producers must keep the `;`-terminated exact line.
- No auto-fix / code action is generated for the CLI; pasting the line
  is the repair. LSP code actions, if any, are separate.
