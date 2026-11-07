# Placeholder-diagnostic retirement record

Date: 2026-10-26

Status: complete. The compiler no longer has a "not built in this build yet"
diagnostic, exit status, or skip path. A supported construct compiles and runs;
an unsupported or invalid construct produces a real compile error with a stable
`CP3xxx` code and exit status `1`.

## What replaced the placeholder

The placeholder existed so the early compiler could recognize a construct
before its lowering existed. Each call site was retired only when the construct
actually worked, with a corpus case that failed before the change and passed
after it. A construct that could not be completed correctly became a real
diagnostic instead of a silent skip.

The final wave covered the last feature groups:

- `Object.keys` / `Object.values` / `Object.entries`
- weak-field reads and narrowing
- function-valued object and interface fields
- `unknown` class-field inference
- `Promise.allSettled` typing
- class-handle nullish optionals

Earlier waves covered spread arguments, rest parameters, destructuring, compound
assignment, object-literal edge cases, class value surfaces, generic calls, type
aliases, enum edge cases, expression operators, control flow, array and
typed-array methods, Map/Set, network methods, fs/process/env/console, async and
Promise, and worker/parallel modules.

## Final verification

- The zero-placeholder gate: zero marker hits across `src`, `tests`, `docs` and
  `packaging`.
- `python vendor/tools/check_diagnostics.py`: every header code is documented and every
  `src/**` reference is defined.
- Full corpus: 1923 found, 1896 passed, 0 failed, 0 skipped, 27 ambiguous.
- `tests/network` phase B: the async-handler, typed-JSON and unannotated
  `req.path` comparison fixtures all run as ordinary checks; no
  `PENDING SURFACE` checks remain.

## Notes for future work

- Do not reintroduce a generic "unsupported" placeholder. Add a named
  diagnostic code for the construct, or implement the construct.
- The diagnostic catalog (`docs/cli-tooling/07-diagnostic-catalog.md`) documents
  the current exit codes: `0` success, `1` compile or program failure, `2`
  usage/environment error.
