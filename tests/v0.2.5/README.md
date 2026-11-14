# Proposal: `tests/v0.2.5/` - the dev-toolchain end-to-end corpus

status: DRAFT for review (not yet wired into CI or committed)
scope: v0.2.5 "fast cp3 dev loop" work - everything that landed this release

## Problem

The corpus harness (`tests/cases/**`) covers the *language*: programs and their
expected output. The v0.2.5 work added a whole second surface - the **dev
loop toolchain** - that no test asserts today:

- per-module TUs, parallel TU pool, `-O0` dev profile, extended PCH
- `.cp3/cache` object store and the shared object cache
- incremental frontend (fragment reuse) and its fingerprints
- dev watcher (debounce, classification diagnostics, exe swap)
- `cp3 warm` (prebuild), `cp3 install`, `--show-runtime`
- module-libs dev build (`cp3runtime_dev.dll` + per-unit DLLs + bootstrap)
- the Lane B background toolchain/PCH build
- the link response file for >32 KB command lines
- no-arg discovery for run/build/check/dev/warm

A regression in any of these is invisible to the language corpus today
(e.g. the Lane B thread-join bug that made *every* failing `cp3 dev`/`build`
crash with `terminate called without an active exception` was caught by hand,
not by a test).

## Layout

```
tests/v0.2.5/
  README.md            this document
  runner.py            the runner (sequential by default; --jobs N for the
                       parallel-safe subset)
  cases/
    <category>/<name>/case.json   fixture project + spec
```

Category directories mirror the features: `incremental/`, `dev/`, `warm/`,
`module-libs/`, `install/`, `discovery/`, `link-rsp/`, `show-runtime/`,
`lane-b/` (regression cases).

## Fixture directory (`cases/<category>/<name>/`)

```
package.json             the project manifest (optional per fixture)
src/*.cp3                the program
case.json                the expectation (below)
extra/                  files copied verbatim into the fixture root
                        (e.g. a vendored native backend, a lockfile seed)
```

Each case runs in a **unique temp copy** of the fixture (the fixture itself is
never dirtied), with the cwd set to the copy, and `PATH` pointing at the repo's
build tree binary (configurable).

## `case.json`

```json
{
  "args": ["dev", "--once", "src/index.cp3"],
  "expectExit": 0,
  "stdoutContains": ["Hello, world!"],
  "stderrContains": ["cp3c: build ok - ", "exit 0)"],
  "stderrNotContains": ["terminate called"],
  "filesExist": [".cp3/cache/index.o", ".cp3/build/cp3_program.hpp"],
  "filesAbsent": [],
  "scenario": "fresh-or-rebuild",
  "rebuildArgs": ["build", "src/index.cp3", "--incremental"],
  "timeoutSeconds": 60,
  "env": {"CP3_DEV_MODULE_LIBS": "1"},
  "optional": false
}
```

Scenarios (the runner implements them; each is one case.json):

- `fresh` - one build; assert exit/output/files (default)
- `rebuild` - build twice, assert the second reports zero recompiles where the
  spec asks for it (`"secondBuildStderrContains": ["up to date, reusing"]`)
- `incremental` - build, then edit `edits/<file>.cp3` (a second file with the
  *same stem*, applied by the runner), rebuild; assert the rebuilt program
  prints the new output, and the stderr line names the reused fragment count
  (e.g. `incremental: 1 units, 1 kept`)
- `dev-once` - `cp3 dev --once`; assert the child's one-shot output
- `warm-then-run` - `cp3 warm` succeeds without producing/running a program;
  a following `cp3 run` reuses the cached objects (`stderrContains`)
- `module-libs` - builds with `CP3_DEV_MODULE_LIBS=1`; assert the module DLLs
  exist next to the exe and the program prints identically to the
  monolithic build (same fixture run twice, two roots)
- `module-libs-badabi` - a deliberately corrupt descriptor (fixture-compiled
  `cp3_module_state_*` with a wrong `compiler_key` baked through the env seam)
  must exit 2 with the "descriptor failed verification" message, never crash
- `watch` - real watch flow: spawn `cp3 dev`, edit a watched file, wait for
  the `build ok` line, kill; **e2e and sequential-only**, marked
  `"requiresE2E": true`, skipped unless `--e2e` is passed
- `archive-rsp` - link a fixture with a generated program whose object list
  exceeds 30 KB; the link succeeds via `@link-objects.rsp`
  (`"requiresLargeGraph": true`, only when the giant fixture is present)
- `install-file` - `cp3 install` against a `file:` dependency; lockfile
  integrity sha recorded; offline, no network
- `discovery-noarg` - run/build/check/dev/warm with no positional args in
  (a) `src/index.cp3` project, (b) `package.json` `main` project,
  (c) empty dir -> the friendly hint + exit 2
- `failure-exit` - a deliberately broken program must exit exactly **1**
  (regression guard for "terminate called without an active exception")

## Assertion vocabulary

```
expectExit             integer, exact
stdoutContains         list[str], all must be found in stdout
stdoutNotContains      list[str], must not be found
stderrContains         list[str], all must be found in stderr
stderrNotContains      list[str], must not be found
filesExist             list[str], paths relative to the temp copy
filesAbsent            list[str]
exeExists              bool (shorthand for the .cp3/out exe existing)
```

No wall-clock or timing budgets are asserted (flaky on shared CI). Fingerprint
counts (`1 kept`) are asserted where printed deterministically; everything else
asserts *shape*, not speed.

## Runner behavior

- sequential by default (dev-run cases spawn watchers/servers/child exes and
  would race for cores and temp space); `--jobs N` only for cases that set
  `"parallelSafe": true`
- `--filter SUBSTR`, `--list`, `--json`, `--verbose`
- per-case timeout (default 60 s, overridable)
- failure prints the case path, the failing assertion, and the captured
  stdout/stderr tail
- exit codes: 0 all pass, 1 any failure, 2 infra error
- `--e2e` opt-in for `requiresE2E` cases (watch flow)

## Why not extend the language corpus

Different lifecycle: the language corpus runs `cp3c` against a single-file
program and checks stdout; the v0.2.5 cases build real projects, rebuild them,
spawn children, and assert on artifacts and exit guarantees. Bolting that onto
the existing corpus would slow and complicate it for cases that both are about
*compiler behavior*, not program behavior.

## Decisions locked in

1. Fixture root is never dirtied (temp copy per case).
2. Sequential default; no parallel watcher cases.
3. No timings asserted.
4. Network-dependent cases are out of scope (install uses `file:` deps);
   GitHub/`https:` install is covered by the existing packaging tests.
5. The watch e2e is opt-in (`--e2e`), flaky-tolerant, and asserts only stable
   markers (the `build ok - ` line and child output).

## Open questions

1. Should the e2e watch case wait for a *rebuilt* exe (mtime) or just the log
   marker? (current plan: log marker + child stdout match; mtime is racy)
2. The `archive-rsp` case needs the giant-repo fixture as a submodule or a
   generated 40 KB-object program; generated is preferred but it's ~300 files
   of codegen; OK to skip on CI via `"requiresLargeGraph": true`?
3. Module-libs negative ABI test: is the env seam (`CP3_DEV_MODULE_LIBS=1` with
   a poisoned fixture) the right injection, or should the test patch the
   descriptor bytes of a built module directly?
