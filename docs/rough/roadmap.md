# CP3 consolidation and gap roadmap

Date: 2026-10-22

Status: consolidation complete. The parked-worktree cleanup is done: every
preserved non-perf worktree has been classified as merged, corrected-and-merged,
deferred, or waste. Implementation lanes for performance, native-install, the
`.cp3pak` binary cache and editor documentation are in flight.

This file is the single decision document. It records what is in `master`, what
was preserved and what happened to it, what is still missing, and the measured
baseline.

## 1. Repository state

| Item | State |
|---|---|
| Long-lived local branch | `master` only |
| `master` tip | `364d803` (Clang target fix) |
| Worktrees | main repository plus temporary per-lane worktrees |
| Working tree | clean |
| Compiler build | green, Ninja, `build/bin/cp3c.exe` |
| Diagnostics | 72/72 codes documented, 0 findings |
| CP3002 gate | 391 marker hits (the zero-placeholder gate is pre-existing red) |
| Full corpus | 1483 found, 1424 passed, **0 failed**, 29 skipped, 30 ambiguous |
| Toolchains | GCC 16.1.0 and Clang 23.1.4 both `ready`; MSVC not installed |
| Benchmark baseline | measured on compiler `6f1fa2c`; reporting commit `c6ea90c` does not change C++ |

The benchmark WIP in `benchmarks/bench/run_suite.py` was consolidated into
`c6ea90c` (`bench: consolidate terminal UI and gate reporting WIP`). It is
presentation and reporting only; it does not change any measurement.

The corpus is now clean: 0 failures. The 29 skipped cases are the CP3002
not-implemented cases; they are not counted as passing.

## 2. What was combined into master

The consolidated history contains:

- `d4698c1` - zero-placeholder gate and control-flow/closures/modules/classes/
  native-types work that was already in the zero branch.
- `7255f26` - generics lane.
- `cfeef76` - master merge into the zero architecture.
- `e61060a` - generic free functions, constraints, tuples, object arguments.
- `c8f4042` - worker reference objects and linked enum members (`b25ec30`).
- `6f1fa2c` - typed network JSON records wrap in `makeRef`, matching plain
  record reference values and `JSON.parse<T>`.
- `c6ea90c` - benchmark terminal UI and gate reporting WIP.
- `0876540` - post-consolidation roadmap and gap inventory.
- `6785dc4` - generic classes format through `toString` (port from the parked
  classes worktree).
- `8cc6b28` - tuple-typed declarations lower (port from the parked types
  worktree).
- `364d803` - a Windows Clang install is usable with no user flags.
- `ac3568b` - `cp3c native-install`, the command half of native interop (55%
  of the feature).
- `68a2839` - array read references and counted-push reserve, ported from the
  parked perf array/closure branches.
- `ef61707` - a per-platform binary cache inside `.cp3pak` archives
  (`cacheBinary`, `cp3 recompile`, `excludeSource`).

The pre-cleanup inventory had 42 branch names. After the final fast-forward,
32 of the 41 non-master tips are ancestors of `master`; those workstreams are
fully contained. Nine unique workstreams are not merged, because they predate
the current class/object/control-dispatch architecture or are parked
performance experiments. They are preserved, not lost.

## 3. Preservation ledger

Everything removed during branch cleanup is outside the repository:

`C:\Users\Krishnesh\dev\cp3-preserved\2026-10-21\`

| Artifact | Contents |
|---|---|
| `all-refs-and-worktree-heads.bundle` | 70 refs: every old branch tip, master, and detached worktree HEAD; `git bundle verify` passed |
| `branch-patches/*.patch` | Net diffs for every branch that was ahead of master |
| `worktrees/<name>/tracked.patch` | Every dirty tracked worktree change; reverse-apply checked |
| `worktrees/<name>/untracked/` | Every untracked worktree file with paths preserved |
| `manifest.json`, `SHA256SUMS.txt` | Coverage and hashes |
| `README.md` | Restore and apply instructions |
| `main-run_suite-ui.patch` | Current benchmark UI WIP, saved before its consolidation commit |

The nine unique branch references:

| Workstream | Reference commit | Status |
|---|---:|---|
| `wave/functions-closures` | `0e1f259` | Older shared-cell closure/capture implementation; superseded by current architecture, kept for reference |
| `wave/generics-free` | `0ac9bab` | Older free-function generics implementation; current master has the newer generics path |
| `wave/perf-fixes` | `93adb33` | Parked perf stack; includes int64, guard division, array refs, map fusion, chunking |
| `wave/perf-array-safety` | `93adb33` plus WIP | Uncommitted array-safety rewrite and tests are in `worktrees/perf-array-safety/` |
| `wave/perf-int-safety` | `c8d7cd9` | Safety fixes on top of `93adb33` |
| `wave/perf-map-safety` | `d172c33` | Map fused-update safety fixes |
| `wave/perf-parallel-safety` | `cd45976` | Parallel purity/integer-exactness safety fixes |
| `wave/perf-async-fanout` | `5773cc1` | Async fanout/event-loop work |
| `wave/perf-closure-capture` | `998febd` | Counted-push reserve and closure work |
| `wave/perf-net-single` | no unique commit | Dirty WIP preserved only |

Restore a reference without creating a long-lived branch:

```powershell
git fetch C:\Users\Krishnesh\dev\cp3-preserved\2026-10-21\all-refs-and-worktree-heads.bundle `
  refs/heads/*:refs/remotes/preserved/*
git branch wave/perf-map-safety d172c33
```

Do not merge those old tips directly. The perf stack predates the current
`emitControlDispatch`, class, object, and closure redrive. Use the patches and
the safety reports as inputs, then port the behavior into current `master`.

## 3a. Worktree cleanup ledger

Every preserved worktree outside the perf lanes has been classified. The
decision for each one, with its evidence, is in
`C:\Users\Krishnesh\dev\cp3-preserved\2026-10-21\WORKTREE-DECISIONS.md`, and
each surviving directory carries a `DECISION.txt`.

**Merged (correction ported onto current master, built, tested, committed):**

| Worktree | Commit | What it fixed |
|---|---|---|
| classes-redrive | `6785dc4` | A generic class with a `toString` body emitted no `cp3_ts` overload, so `console.log` of a generic-class instance failed in C++. The formatter is now a template over the class type parameters. |
| types | `8cc6b28` | A `let t: [string, number] = [...]` annotation had no classify arm and reported CP3002, even though tuple returns and index reads already worked. Tuples now classify as their `std::tuple` spelling and brace-initialize. |

**Already in master, archive deleted:** `cp3002-generics`, `cp3-base-3d48a05`,
`worker-handles`, `native-types`, `cp3002-zero`, `control-flow-core`,
`generics`, `generics-redrive`, `modules`.

**Deferred (real feature, indivisible, preserved intact for a dedicated
branch):**

| Directory | What it is | Why deferred |
|---|---|---|
| `deferred/native-install` | The whole native-install/bindgen subsystem: CLI command, libclang loader, SDK resolution, `native.json` + `index.cp3` generation, sema checker, codegen lowering. | ~470 KB across 29 modified and 39 new files, 16 apply conflicts against current codegen, no tests, and a new libclang runtime dependency. No half or quarter of it compiles or changes behavior. |
| `deferred/classes-hoisting` | The 24-file local-generic hoisting/capture/lexical-scope rewrite. | Its generic-`toString` hunk is already merged as `6785dc4`; the rest conflicts in three central files and master still reports CP3002 for a local generic class. |
| `deferred/closures-optionals` | Optional object-literal fields and function-typed interface fields. | 4 of 9 files fail to apply against current layouts; it is a multi-file feature, and 8 corpus cases stay skipped because of it. |

**Waste (0% unique behavior, deleted after inspection):** `control-flow` (older
copies of the control/object emitters; every hunk conflicts),
`cp3002-cli` (removes the deliberate exit-3 not-implemented scaffold and adds
unused fields), `modules-redrive` (header-only declarations with no
definitions and no emitter use), `functions-closures` (a superseded 31-file
closure/codegen rewrite with no tests), `w2-classes` (the pre-redrive class
implementation; its only unique test edit replaced a `CP3xxxx` placeholder that
master intentionally keeps), `cp3` (a trailing space and a removed final
newline in one benchmark file).

The rule that decided each one: **0% useful -> deleted; a real feature that
cannot be split -> deferred intact; a bounded usable delta -> ported and
merged.** Nothing was silently dropped; the 70-ref bundle remains the
permanent recovery source.

## 3b. Native-install completion

The native-install feature is landed in two steps rather than one unsound
merge. **Step 1 is merged** as `ac3568b` (merge `0e8a0e2`).

| Step | Scope | Status |
|---|---|---|
| 1 | The `cp3c native-install` command end to end: SDK/header resolution, libclang loading, parsing free functions, enums and simple classes, `index.cp3` + `native.json` generation, the full flag surface, dry-run/JSON/force, named diagnostics for rejected declarations, and a fixture-based test suite. | **MERGED** (`ac3568b`) |
| 2 | The consumption half: `check`/`build` read `native.json`, sema types `import native` calls against it, codegen lowers them, plus the full test suite. | Not started |

**Native-install completion: 55%.** Measured as 8 of 14 equally weighted work
items done - all 8 command-half items done, all 6 compiler-half items not
started. The full table and the calculation are in
`docs/rough/native-install-partial.md` §5, kept in sync with the
supported/unsupported matrix.

What works today, verified on this machine against the fixture SDK:

```
$ cp3c native-install sdk/include/demo/native_demo.h --name demo --out bindings
  declarations: 8 functions, 2 classes (3 methods), 1 enums
  rejected: 5 (listed in index.cp3)
  wrote .../bindings/demo/index.cp3
  wrote .../bindings/demo/native.json
```

The generated `index.cp3` is ordinary cp3 source (`export class Point`,
`export enum Color`, `export function add(a: number, b: number): number`), and
`native.json` carries the `cp3-native-binding/1` schema with the C++ names,
argument conversions, ABI, content hash and link metadata. Unsupported shapes
are rejected with named diagnostics - never dropped silently - and are listed
in both files.

The libclang **headers** are vendored (`third_party/clang-c`, pinned
`llvmorg-23.1.0`, Apache-2.0 WITH LLVM-exception), so cp3c still compiles with
no LLVM installed; the libclang **library** is loaded dynamically at runtime
and `CP3_LIBCLANG` is honoured. 10/10 CLI cases pass, including the live
generation case, which skips cleanly where no libclang is present.

The follow-up agent must still do: the `native.json` reader and staleness
check, `import native "id" as alias` resolution in the module resolver, sema
type-checking of native calls, codegen lowering (include emission, `cppName` /
`cppArgCount` / `returnsCStr` use), build integration (include dirs, defines,
lib dirs, link names, `runtimeDlls` bundling), the spec-19 pointer-ownership
and object-lifetime story, and compiler-side tests for `check` and `build`
against a binding.

## 3c. The `.cp3pak` binary cache

`cacheBinary` and `cp3 recompile` were specified in CLI-README §5.4 but only
warned and returned exit 3. They are now **implemented** and merged as
`ef61707` (merge `cb85d5c`), with `0443049` fixing the pre-existing
`publish --out` failure on the way.

| Behavior | Result |
|---|---|
| `build.cacheBinary: true` | Resolves to this machine's canonical platform id, recorded in the archive. |
| `build.cacheBinary: [...]` | The listed ids. A family claim (`windows`) is satisfied by any arch of that family; an unknown id is a manifest error. |
| Package build | Compiles the generated C++ into a plain object for the current platform only, never cross-compiling. `cp3pak.json` gains the cache fields; an archive without a cache is byte-identical to before. |
| `cp3 recompile <file.cp3pak>` | Replaces only the current platform's cache, preserves every other member and other platform caches, is idempotent, and errors clearly when the archive has no source. |
| `build.excludeSource` | Requires a non-empty `cacheBinary` (build error otherwise). The source is dropped only once every claimed platform is cached; headers and `index.cp3` are always kept. Installing an excluded-source package with no cache for this platform gives the named "No precompiled binary found for your platform" error. |

Archive layout:

```
cp3pak.json                  + optional "cachePlatforms" / "cacheClaims"
package.json  index.cp3
pak/<module>.cpp             dropped by excludeSource
pak/<module>.hpp             always kept
pak/<platform>/cache.json    platform, abi, per-artifact hashes, compiler identity
pak/<platform>/<module>.o    the compiled object
```

Platform ids are always canonical and arch-qualified (`windows-x64`,
`linux-x64`, `mac-arm64`); family aliases (`win32`, `mingw`, `macos`,
`darwin`, `aarch64`, ...) are normalized, case-folded, and an unknown id is a
clear manifest error.

**The `.cp3pak` binary cache is 94% complete** (8.5 of 9 items). The one
open item is the consumer no-recompile link path, and it is blocked by
something outside this feature: the frontend does not yet resolve a package
import to C++, so a consumer program links no package code at all - there is
no "recompile the package source" step to skip yet. What landed is the
archive and install validation, the cached-artifact-vs-source selection, and
the install-time report. The remaining step is to resolve a package import to
the installed package and then add `pak/<platform>/<module>.o` to the link
inputs when the cache matches.

## 4. Verification detail

### Build and static gates

- Main build: green.
- `python tools/check_diagnostics.py`: 72 codes defined, 72 documented, 435
  source files scanned, 0 findings.
- `python tools/check_cp3002.py`: 391 hits, unchanged by the cleanup and the
  perf array/closure lane. The zero-placeholder gate in `tools/lint.py` fails
  on this count - it is pre-existing red on `master`, not a regression.
- `python tools/lint.py`: no new error from any lane.

### Corpus

Current full pass on `master` at `--jobs 4`:

- 1483 found
- 1424 passed
- 0 failed
- 29 skipped
- 30 ambiguous

The earlier 18 failures were all load timeouts (17 network cases and
`stdlib/map_forEach_sum`) from test-harness oversubscription - fixed ports plus
many compiler processes - and every one of them passes serially. Network tests
must be run with `--jobs 1`.

The 29 skipped cases are the CP3002 not-implemented cases. They are not counted
as passing.

### CLI suite

`python tests/cli/run_cli_tests.py --jobs 1 --full`: **346 cases, 346 ok, 0
failing**, 15 xfail (the pre-existing expected failures). The one failure that
existed before the `.cp3pak` lane - `package/publish-out-file`, a
`fs::copy_file` into a non-existent `dist/` parent - was fixed by `0443049`.

| Group | Result |
|---|---|
| native-install | 10/10 |
| recompile | 5/5 |
| package | 17/17 |
| toolchain | 35/35 |
| build / check / clean / dev / editor / fmt / global / hooks / init / install / run / shim | all pass |

### Historical baseline benchmark (2026-10-19)

The current ledger is the 2026-10-23 full-suite run in
`benchmarks/results.md`: every row has a cp3 timing, including
`compute-multi` (31 ms; 0.59x cpp / 0.70x rust), `health-server`
(17,354 req/s) and `health-server-multi` (103,372 req/s). The table below is
the pre-perf baseline that motivated the five gaps.

Compiler `6f1fa2c`, compute rows, best of 2 runs, references rebuilt locally.
Ratios are cp3 time divided by the reference time. Lower is better. cpp and
rust are the primary gate references; Go/JS are tripwires.

| Row | cp3 best ms | cp3/cpp | cp3/rust | Gate |
|---|---:|---:|---:|---|
| compute-single | 225 | 1.06x | 1.18x | WARN |
| compute-single-int | 214 | 1.00x | 1.02x | pass |
| compute-multi | 228 | 6.37x | 5.20x | BIG WARN |
| hello-world | 12.2 | 0.80x | 1.05x | pass |
| string-churn | 365 | 0.18x | 1.00x | pass |
| word-frequency | 1490 | 24.93x | 20.20x | BIG WARN |
| object-churn | 430 | 2.82x | 2.80x | BIG WARN |
| array-growth | 170 | 1.45x | 1.38x | BIG WARN |
| closure-capture | 157 | 1.63x | 5.30x | BIG WARN |
| interface-dispatch | 332 | 2.17x | 2.51x | BIG WARN |
| generics-mono | 246 | 9.36x | 8.53x | BIG WARN |
| json-roundtrip | 238 | n/a | n/a | 1.57x Go, 2.26x JS |
| async-fanout | 35 | n/a | n/a | 1.86x Go |

Geomean over the 11 cpp/rust rows: 2.08x cpp, 2.71x rust.

`json-roundtrip` and `async-fanout` have no dependency-free cpp/rust
counterpart in the suite; Go is the primary reference for those rows.
The 2026-10-23 full-suite run measured both server rows: cp3 17,354 req/s
single-core and 103,372 req/s multicore. The client-bound caveat still applies
to the single-core cluster; the multicore row separates cleanly.

The fast rows are hello-world, string-churn, compute-single-int, and
compute-single (the last is only a small WARN, not one of the five major gaps).

## 5. The five performance gaps

Status 2026-10-23: all five gap rows now run. `compute-multi` and
`word-frequency` improved sharply; the current full-suite gate still flags
object/interface dispatch, closures/generics, and several of the new stdlib
rows. The descriptions below are the original work map, not the current
ledger; use `benchmarks/results.md` for the gate.

These are the five measured gap groups. They are not the same as the five old
perf branches; the old branches cover parts of gaps 1, 2, 4, and 5, but not the
object/interface dispatch work in gap 3.

| # | Gap | Evidence | Root cause | Candidate work |
|---|---|---|---|---|
| 1 | Parallel reduce | compute-multi 6.37x cpp, 5.20x rust | `parallel.reduce` on `acc + collatzSteps(i)` falls back to the 4-argument sequential lowering | Generalize the combine gate to pure integer expressions, prove purity, chunk the range, preserve ordered combine |
| 2 | Map/collection update | word-frequency 24.93x cpp, 20.20x rust | `MapData` is an insertion-ordered vector with linear scans; `get` then `set` probes twice | Add auxiliary hash index, fused find/update, int64 hot loop, array reference reads |
| 3 | Object and interface dispatch | object-churn 2.82x cpp; interface-dispatch 2.17x cpp | Reference-object allocation and interface wrapper dispatch; object/class paths are not specialized | Reference-object/class representation, interface table dispatch, allocation reduction, churn-specific profiling |
| 4 | Closure and generic lowering | closure-capture 1.63x cpp, 5.30x rust; generics-mono 9.36x cpp | Captured cells and generic specialization produce extra wrappers/allocations; monomorphized code is not close to cpp | Reserve counted push loops, reduce capture cells, inspect generic specialization and generated hot code |
| 5 | Array and async/JSON runtime | array-growth 1.45x cpp; async-fanout 1.86x Go; json-roundtrip 1.57x Go | Array growth still uses reference/vector overhead; async fanout pays coroutine/promise allocations; JSON parse/stringify is slower than Go | Array read references/representation, event-loop fanout and inline await, JSON parser/stringifier hot path |

Near-parity rows:

- compute-single: 1.06x cpp, 1.18x rust (WARN, not a major gap).
- compute-single-int: 1.00x cpp, 1.02x rust.
- hello-world: 0.80x cpp, 1.05x rust.
- string-churn: 0.18x cpp, 1.00x rust. The cpp entry is unusually slow for
  this row; rust is the meaningful reference.

### What the perf lanes have landed

The parked perf stack is being ported lane by lane, and each port is judged
against the safety brief rather than merged wholesale.

**Landed: array reads and counted-push reserve** (`68a2839`, merge `aa8f020`)
- `const word = words[i]` used to lower to `arrayAt<E>`, which returns a fresh
  `std::string` per lookup. It now binds a `const E&` into the vector, so the
  per-lookup copy disappears. This is gap 5's "array read references" work and
  it feeds gap 2's "array reference reads".
- `while (i < bound) { a.push(v); i = i + 1; }` now reserves the remaining
  pushes once, so the vector allocates once instead of growing geometrically.
  Capacity is not an observable cp3 value, so the hint cannot change a
  program's result. This is gap 4's "reserve counted push loops".
- The proof is whole-unit and complete-or-nothing: the array must be private
  storage, never mutated in any frame, never escaped or exported, and must
  outlive the reference; the candidate must be a `const` with no declared type,
  never written, and read only in its declaring frame. Anything the analysis
  does not model fails closed - no candidate in the unit is accepted.
- The lane also found and fixed six defects in the inherited port, including a
  `let` candidate that broke `ref` parameters (the generated C++ did not
  compile) and a silently dropped declared type.
- 35/35 `perf-array` cases, 186 `arrays-strings` passed, 60
  `functions-closures` passed (8 pre-existing skips).

**Landed: the int64 foundation** (`ea8b1a8`, merge `05beb63`) - 89% of the
lane's brief.
- A **divisibility fact**: `x % k == 0` licenses an exact `x / k` - a plain
  divide, or an arithmetic shift for a power of two. The fact is invalidated by
  every write, dropped on rebinding, scoped to the then-branch, and refused in
  any branch that can defer, repeat or outsource the computation.
- **R1**: a counted loop (`while (c<N){...;c=c+K;}`) becomes an int64 counter
  with a `for` header step, keeping every counter value and the exit value
  exact.
- **R2**: a bounded recurrence (`x=(x*A)%K`) becomes an int64 register, with
  the emitted write wrapped so a nested write hands back the same value in the
  same type the original `number` had.
- **Module bindings** that only the entry body reads become `main` locals.
- 47 adversarial cases with independent oracles, each in `tests/cases/perf-int/`.
- **The lane proved a real base bug**: on `0876540`,
  `function f(a){ console.log("hello"); return a/2; }` printed `hello` twice
  - the fast body ran the call, bailed, and the double body ran it again. The
  ported compiler prints it once. Verified on `master` after the merge.

**Landed: the runtime port** (`686767c`, merge `1d43df2`) - 100% of the lane's
brief, by file-by-file comparison against the three source branches.
- **Map**: a fused `m.get(k) ?? d` plus `m.set(k, v)` keeps the unfused
  order - receiver, key, probe, fallback **only on a miss**, value, then
  insert and store. Receiver, key, fallback and value must be pure reads, so
  the cached slot cannot dangle and no user code runs between probe and store.
  `MapData` keeps the insertion-ordered key vector (D44) and puts the values
  in a hash index, with SameValueZero equality required - plain `==` would let
  a NaN key never be found again.
- **Parallel**: chunking applies only to an associative shape with a pure,
  integer-exact right-hand side (`/` only for a proved multiple). A run-time
  guard below 2^53 plus a sequential-prefix proof means the chunked sum cannot
  differ from the left-to-right double fold; a failed proof reruns
  sequentially, which purity makes unobservable.
- **Async**: every awaiter still suspends, so the caller order is preserved;
  the sleep resume is posted rather than direct; one tagged continuation list
  keeps per-path FIFO order and releases dropped captures.
- The lane found and fixed two real defects in the inherited port: the fused
  form fired on a shared-`Cell` binding (a C++ compile error) and inside the
  int64 fast-path region. Both now fail closed.

**The two perf features needed a merge-time fix.** The int64 fast path and
the fused map update both rewrite statements, and neither knew about the
other: the int64 path marks the statements it owns, while the fused update
replaces the pair it consumes. Merged naively, the fused update could consume
an int64-owned statement and drop the int64 lowering. The merge therefore
teaches the fused path to refuse a pair that touches an int64-owned statement,
and the block walk to skip those statements before offering them to the fused
path. This is the kind of interaction a wholesale merge of the old branches
would have silently broken.

**Still in flight:** the editor lanes, the native-install compiler half, and
the CP3002 implementation wave.

The baseline table above is the pre-port measurement. The re-measurement after
the remaining perf lanes land is what decides whether a gap is closed.

### Reading the CP3002 gate honestly

The gate scans `src`, `tests`, `docs` and `packaging`, so **prose that mentions
the marker counts too**. Writing this section, and the wave-plan document,
moved the gate total from 388 to 395 while the code count stayed at exactly
**347 in `src/`** - the int64 lane added zero code markers.

The numbers that matter:

| Number | Value | What it is |
|---|---:|---|
| Gate total | 395 | every hit, including documentation prose |
| `src/` hits | 347 | the code markers |
| Live call sites in `src/` | 224 | the places a marker is actually raised |
| Distinct work items | 187 | one per marker message per feature area |

A wave that retires ten call sites moves the `src/` number by ten and the gate
total by ten plus whatever the accompanying documentation says. The `src/`
and call-site numbers are the ones to track.

## 6. The eighteen missing CP3002 feature groups

The gate counts 378 marker hits. The `notImplemented` marker is 232: 230 are
live call sites with 188 distinct messages, and two are the helper definition
and declaration. The calls cluster into the 18 language/runtime feature buckets
below. The list is an ownership map, not a claim that every message is
independent; some cross-cutting messages appear in more than one bucket. CLI
exit plumbing, documentation markers, test expectations, and the marker
definition account for the remaining hits.

| # | Missing feature group | Representative surfaces | Main files |
|---|---|---|---|
| 1 | Spread arguments and spreads | spread argument, spread element, spread property | `emit/call_args.cpp`, `emit/patterns.cpp`, `emit/async.cpp`, `std/typed-arrays/codegen.cpp`, `support/generic.cpp` |
| 2 | Rest parameters | rest parameter, rest parameter type, rest argument, undefined in rest | `emit/patterns.cpp`, `emit/class_decl.cpp`, `emit/call_args.cpp`, `emit/async.cpp` |
| 3 | Destructuring | destructuring parameter/binding/source/property, defaults, no initializer, rest object pattern | `emit/destructure.cpp`, `emit/patterns.cpp`, `emit/stmt.cpp`, `emit/module_var.cpp` |
| 4 | Compound assignment and update targets | compound assignment operator, assignment to non-identifier, update of non-identifier | `emit/assignment.cpp`, `emit/expr.cpp` |
| 5 | Object literal edge cases | computed/method object field, object field value, object literal for constructor class | `emit/objects.cpp`, `support/generic.cpp`, `std/console/codegen.cpp`, `emit/class_expr.cpp` |
| 6 | Class declaration and value surfaces | computed class member names, generic class, weak field, accessor/field/return type, class used as a value | `emit/class_decl.cpp`, `emit/class_expr.cpp`, `support/class_collect.cpp`, `sema/checks/class_check.cpp` |
| 7 | Generic calls and specialization | calls with type arguments, generic arrow, generic methods, generic interfaces, type argument | `emit/call_args.cpp`, `emit/async.cpp`, `support/generic.cpp`, `sema/checks/*` |
| 8 | Type aliases and declared types | type aliases, declared type, return type, async function value type | `sema/support/declarations.cpp`, `emit/decl.cpp`, `emit/module_var.cpp`, `emit/stmt.cpp` |
| 9 | Enum edge cases | linked enum value, enum member initializer, duplicate enum switch case | `emit/enum.cpp`, `sema/checks/enum_check.cpp`, `emit/expr.cpp` |
| 10 | Expression operators and formatting | typeof of non-primitive, union equality, binary/unary/logical/ternary mixed types, formatting a value, property access | `emit/typeof.cpp`, `emit/equality.cpp`, `emit/expr.cpp` |
| 11 | Control flow | for-in, switch case test type, labeled break, break outside loop/switch, labeled continue, continue outside loop | `emit/control.cpp` |
| 12 | Array methods and callbacks | non-arrow array callback, map/reduce result type, no-initial reduce, element/flat type, for-await, for-of source/binding | `std/arrays/codegen_methods.cpp` |
| 13 | Typed arrays and constructors | new expression, buffer length, DataView, family/typed-array constructor, Array(...), Array.of/from, array literal/spread/fill/join/sort/includes/index reads/writes | `std/typed-arrays/codegen.cpp`, `std/typed-arrays/codegen_methods.cpp` |
| 14 | Map and Set | Map/Set type, Map/Set receiver, non-arrow Map forEach, non-arrow Set forEach | `std/collections/codegen.cpp` |
| 15 | Network | fetch options object, fetch(...), next() continuation args, function call, WebSocket, WebSocketServer, json<T>() arguments, res.json(payload), req.body/ReadStream | `std/network/codegen.cpp`, `std/network/codegen_methods.cpp` |
| 16 | FS, process, env, console | stream member/event/listener, spawn argument list/listener event, env.get options/type, console.assert/import form | `std/fs/codegen.cpp`, `std/process/codegen.cpp`, `std/env/codegen.cpp`, `std/console/codegen.cpp` |
| 17 | Async and Promise | Promise., Promise.resolve/reject typed values, async arrow, generic arrow, await outside async/top level, async function value type | `emit/async.cpp`, `emit/decl.cpp` |
| 18 | Worker and parallel modules | worker module statements/re-export/destructuring/module variable/entry arrow/listener parameter, parallel for/reduce/run shapes and initializer types | `emit/worker.cpp`, `emit/worker_module.cpp`, `emit/worker_handles.cpp`, `emit/worker_listener.cpp`, `emit/worker_scan.cpp`, `emit/parallel.cpp` |

The scanner gate itself is `tools/check_cp3002.py`; its 30 skipped corpus cases
are the observable effect of these groups. The CP3002 exit code and catalog
entry are intentionally preserved until the true count reaches zero.

## 6a. Editor experience gaps (RESOLVED - the two reported defects are fixed)

Two concrete problems were reported from VS Code, both visible in a single
screenshot of a five-line file. They were separate defects with separate
owners, and both are now fixed.

### 6a.1 The colour scheme now matches the JavaScript conventions - FIXED

The grammar (`extensions/cp3-vscode/syntaxes/cp3.tmLanguage.json`) used its own
TextMate scopes, so a cp3 file did not read like the JavaScript next to it.
The fix maps cp3 onto the scope names VS Code's built-in themes already
colour, verified against the real `dark_plus.json` and `light_plus.json`
shipped with VS Code:

| What | Before | After (Dark+) |
|---|---|---|
| A local variable | no scope - default foreground | `variable.other.readwrite` (#9CDCFE) |
| A parameter | no scope | `variable.parameter` (#9CDCFE) |
| A user function / method / accessor name | no scope | `entity.name.function` (#DCDCAA) |
| A class name | no scope | `entity.name.class` (#4EC9B0) |
| A type annotation `:` and the user type in it | no scope | `keyword.operator.type.annotation` + `entity.name.type` (#4EC9B0) |
| A property after a receiver | no scope | `variable.other.property` (#9CDCFE) |
| `console`, `Math`, `JSON`, `Date`, `Map`, `Set`, `parallel` | `support.namespace` - **the themes do not colour it** | `support.class.builtin` (#4EC9B0) |
| `of` / `in` | purple control-flow | `keyword.operator.expression.of` / `.in` (the TypeScript convention) |
| `=>` | plain operator | `storage.type.function.arrow` (the TypeScript convention) |
| Control flow, storage, `this`, numbers, strings, comments | already correct | unchanged |

The mapping is documented in `extensions/cp3-vscode/README.md` and
**machine-checked**: `scripts/grammar-smoke.mjs` grew from 51 to **76 scope
assertions** driven through the real TextMate engine, so a scope that stops
matching a theme is a test failure, not a visual surprise.

### 6a.2 Completion is now position-aware and scope-aware - FIXED

The screenshot showed the completion popup at an expression position inside a
function body, offering the entire language: `finally`, `catch`, `abstract`,
`as`, `implements`, `interface`, `private`, `protected`, `public`, `static`,
`get`, `set`, `in`, `of`.

The cause was in the code: `generalCandidates` in
`src/cli/src/commands/lsp_intel_names.cpp` appended **every** keyword from one
flat table to **every** completion request, with no regard for the syntactic
position of the cursor. The document's own declarations were added too, but
below sixty keywords.

Two new units fix it: `lsp_intel_position.cpp` (where the cursor is) and
`lsp_intel_scope.cpp` (what is in scope).

**Position rules:** statement start, expression start, type position, member
position, import clause, class body, parameter list, inside a `try` block,
inside a `switch` body, and a declaration-name slot. Each offers only the
keywords legal there - `catch`/`finally` inside a `try`, `case`/`default`
inside a `switch`, the member modifiers in a class body, `ref` in a parameter
list.

**Scope rules:** the parameters of the enclosing functions, the locals of the
enclosing blocks (only those bound above the cursor), the enclosing
`for...of`/`for...in` binding, the `catch` parameter, the enclosing class
name, the module-level bindings, and the **imported** names - an
`import console from "std/console"` makes `console` completable.

**Ordering:** local scope, then imported names, then module-level
declarations, then snippets, then keywords last.

**The reported case, before and after:**

| | Before | After |
|---|---|---|
| Items offered | 52 | **13** |
| The local `a` and `b` | **not offered at all** | offered, with their inferred type |
| `finally`, `catch`, `abstract`, `private`, `implements` | offered | **not offered** |
| `console` (imported) | offered | offered |

Other positions: inside a `try` block 51 -> 26 items (`catch`, `finally`
first); outside one 51 -> 24 (neither offered); top of file 52 -> 25.

**Regression protection:** fifteen new checks in `tests/lsp/intel_check.py`
cover it - an expression position offers none of the illegal keywords, does
offer the locals, does offer the imported namespace, a `try` block offers
`catch`/`finally` and a non-`try` block does not, a class body offers only the
member modifiers, a parameter list offers only `ref`, a type position offers
only types, the nearest scope leads the popup, and every item carries the
inferred type. The suite is **67/67**.

The position detection is a heuristic, not a parser: the ASI rule and the
"an identifier/literal continues the expression" rule can be wrong in rare
shapes, and the scope collection walks the whole AST per request, which is a
per-keystroke cost on a very large file. Both are known and recorded.

### 6a.3 Documentation in the editor - FIXED

`cp3` now supports JSDoc-style doc comments in its own source, and the
standard library is documented in the editor.

- **Doc comments**: block `/** ... */` and line `/// ...`, with a free-text
  description, `@param <name> <text>` and `@returns` / `@return <text>`. Any
  other `@tag` is kept as plain text and is never an error. Supported on
  functions, classes, methods, class properties, accessors and variable
  declarations. A doc comment never changes a program's output and never
  produces a diagnostic.
- **Standard library**: every member of every table-driven family carries a
  real description written from the standard-library docs - console (12),
  Math (23), path (8), os (7), process (5), env (3), fs (17), JSON (2),
  Date (17), crypto (6), Map (10), Set (7), std/arrays (21), std/string (22),
  RegExp (5). A member with no description renders a **visible**
  `_No documentation available for `x`._` rather than a silent blank.
  `std/network`, `std/buffer` and `std/typed-arrays` are left undocumented
  deliberately: they have no member table to attach it to.
- **What the user sees**: hovering `console.log` gives the signature and the
  description; hovering a documented function gives the description, the
  `@param` list and the `@returns` text; completion items carry
  `documentation` as Markdown; signature help carries the callee's
  documentation and each parameter's `@param` text.
- **Tests**: nineteen new LSP checks (71/71 in total), including a `cp3c run`
  comparison that proves a doc comment does not change a program's output.

### 6a.4 What "as good as the JS extension" means overall

The bar the user set: the cp3 extension must feel like the JavaScript
extension, not merely resemble it. The pieces, in order of how much they
hurt today:

| Capability | JS extension | cp3 today |
|---|---|---|
| Colour scheme matches the language family | yes | **fixed** |
| Completion respects position and scope | yes | **fixed** |
| Hover shows the type and the documentation | yes | **fixed** |
| Go-to-definition, symbols, signature help | yes | present |
| Format on save | yes | present (`cp3 fmt`, byte-identical) |
| Error squiggles with the cp3 diagnostic code | yes | present |
| Refactoring (rename, extract) | yes | absent |
| Auto-import | yes | absent |
| Quick fixes | yes | absent |

The last three are post-beta-one candidates, not beta-one blockers. The first
five are the beta-one bar, and all five are now met.

## 6b. The CP3002 implementation wave (authorized)

CP3002 implementation is now authorized. The gate is at 388 hits, of which
**224 are live marker call sites in `src/`** (the rest are 30 documentation
hits, 11 test hits, and the marker definition itself). The 224 sites carry
**138 distinct work items** - one per marker message per feature area.

The work is planned from the live inventory, not by hand:
`tools/cp3002_wave_plan.py` scans `src/`, extracts every
`notImplemented(` site with its message, groups the sites by the feature area
that owns them (the group table in section 6), and chunks the result into
agent-sized assignments. It writes `docs/rough/CP3002-WAVE-PLAN.md`, which is
the lane sheet every agent reads, and it is regenerated after every merged
wave.

| Measure | Count |
|---|---:|
| Marker call sites in `src/` | 224 |
| Distinct work items | 138 |
| Agent assignments (~10 items each) | 14 |
| Waves of 5 parallel agents | 3 |

### The wave protocol

**HARD RULE: no new agent is launched until a previous agent's work has been
tested and merged by the lead. The queue advances only on a completed merge,
never on an agent's report alone.**

1. Five agents run at once, each owning one assignment from the plan - about
   ten work items, scoped to one feature area so the work is coherent.
2. An agent implements its items, builds, runs the corpus cases its area
   touches, and reports. It does not merge to `master`.
3. The lead verifies the agent's commit on canonical `master` - rebuild, the
   affected corpus cases, the diagnostics gate and the CP3002 count - and
   merges it serially.
4. **Only after that merge** does the lead launch the next agent, so five
   lanes are always running.
5. A lane that fails verification is sent back with the reason; it is never
   merged on the agent's word alone, and it never frees a slot.

Three waves clear all fourteen assignments.

### Rules every lane obeys

- **No marker is deleted to make a number go down.** A marker is retired only
  when the construct it stood for actually works, and a corpus case proves
  it. A "not implemented" that becomes a wrong program is worse than a skip.
- A construct that cannot be completed correctly stays a marker, and the
  report says why. A skipped item is honest; a wrong implementation is not.
- Every retired marker gets a corpus case that would fail before the change
  and passes after it.
- The CP3002 count, the diagnostics gate (0 findings) and the corpus (0
  failures) are checked by the lead after every merge.

## 6b. The CP3002 implementation wave (in progress)

CP3002 implementation is now authorized. The gate is at 388 hits, of which
**224 are live marker call sites in `src/`** (the rest are 30 documentation
hits, 11 test hits, and the marker definition itself). The 224 sites carry
**138 distinct work items** - one per marker message per feature area.

The work is planned from the live inventory, not by hand:
`tools/cp3002_wave_plan.py` scans `src/`, extracts every `notImplemented(` site
with its message, groups the sites by the feature area that owns them (the
group table in section 6), and chunks the result into agent-sized assignments.
It writes `docs/rough/CP3002-WAVE-PLAN.md`, which is the lane sheet every
agent reads, and it is regenerated after every merged wave.

| Measure | Count |
|---|---:|
| Marker call sites in `src/` | 224 |
| Distinct work items | 138 |
| Agent assignments (~10 items each) | 19 |
| Waves of 5 parallel agents | 4 |

### The wave protocol

**HARD RULE: no new agent is launched until a previous agent's work has been
tested and merged by the lead. The queue advances only on a completed merge,
never on an agent's report alone.**

1. Five agents run at once, each owning one assignment from the plan - about
   ten work items, scoped to one feature area so the work is coherent.
2. An agent implements its items, builds, runs the corpus cases its area
   touches, and reports. It does not merge to `master`.
3. The lead verifies the agent's commit on canonical `master` - rebuild, the
   affected corpus cases, the diagnostics gate and the CP3002 count - and
   merges it serially.
4. **Only after that merge** does the lead launch the next agent, so five
   lanes are always running.
5. A lane that fails verification is sent back with the reason; it is never
   merged on the agent's word alone, and it never frees a slot.

Three waves clear all nineteen assignments.

### Wave 1 - merged

| Assignment | Merge | Items | What it retired |
|---|---|---|---|
| 1 | `afe44f6` | 9/10 | Promise combinators over any operand, the Promise static and member surfaces, `Array(...)` and `Array.from` type errors, the unresolved-import error, spread sources |
| 2 | `2b606c3` | - | Spread arguments, async arrows, explicit type arguments, the await-outside-async error |
| 3 | `31af082` | 6/10 | Array read references, counted-push reserve, the array element type gate, the spread element |
| 4 | `802e973` | 4/10 | A spread into a fixed-argument callee, a class rest parameter, a tuple destructuring source, a pattern default |
| 5 | `55ef5ce` | 9/10 | Rest parameter types, a pattern default, the spread element and property, an untyped parameter, the excess-property error |
| 7 | `dcb9b39` | 4/10 + 2 partial | A destructuring binding without an initializer, the destructuring assignment, the shared-cell compound assignment, `??=`, the console event and `assert` surfaces |
| 8 | `c0d698a` | 8/10 + 1 partial | A computed object key, an object literal for a class with a constructor, the console event and import surfaces, `console.assert`, a spread into a constructor, the generic class instantiation, the computed class member name |
| 9 | `c98f983` | 7 | An enum member initializer, a duplicate enum case, type aliases, a re-export, an async function's value type, generic methods, generic interfaces |

The items that stayed markers did so for reasons worth keeping: a generic
arrow needs a parser production the spec does not define, a generic class
value needs a `TypeParam` inside an object annotation, and a spread into a
fixed-argument signature needs the shared type mapping that another lane owns.

### Wave 2 - merging, one lane at a time

Ten lanes, one hundred work items, assigned by the same plan. The lanes cover
the expression operators, the control-flow surfaces, the array and typed-array
methods, the Map/Set and network surfaces, the FS/process/env surfaces, the
whole `parallel.*` family, the worker modules, the CLI `build`/`check` paths and
the call forms.

**The lanes were stopped and committed as-is** at the spec owner's direction,
and the lead merges them one at a time: merge the lane, resolve its conflicts
by hand, build, run the families it touches, check the marker count, and only
then merge the next. A lane that fails the check goes back to its branch for
repair rather than into `master`.

| Lane | Merge | What it retired |
|---|---|---|
| wave 2, lane 9 | `fb01f66` | the parallel accumulator type, the `parallel.run` closure shape, the worker entry/listener/module forms, the `parse` builtin's arity |
| wave 2, lane 10 | `7767a6f` | the call forms and the CLI `build`/`check` surfaces |
| wave 2, lane 8 | `776d8c1` | the class and object surfaces |

**The conflicts were design conflicts, not textual ones.** Two lanes had
rebuilt the same machinery differently: one had a `parallel.run`-specific
closure check, the other a general `ClosureFault` that also verifies arity
and parameter shape for `for` and `reduce`. The merge keeps the general one
**and** the accumulator-type rule the other lane had added, because that rule
reports a real error where the general one would have left a marker.

**The conflict footprint is known in advance.** The lead measured each pending
branch against `master` before merging it, so the cost of each merge is known:
wave 2 lanes 2 and 6 touch one file each (the generated wave plan and the
generator header), lanes 4 and 5 touch two and four, lane 3 touches ten, lane
1 touches nine, and the wave-1 lane 6 touches fourteen - the class-model files
that three other lanes also edit.

### What wave 2 does not cover - the third wave

Of the 133 work items still open after wave 1, the ten wave-2 lanes own 66.
**The other 67 need a third wave**, and they are not a random tail - they are
the three big families, which is where the cross-family seams are:

| Third wave | Area | Items | The seam it needs |
|---|---|---:|---|
| R1 | typed-array element types | 10 | the index read/write, the fill/includes/join/sort element types, the enum value - all one shared "what may an array hold" question |
| R2 | Map/Set + the network family | 10 | the non-arrow `forEach` callbacks, the Map/Set receiver and type, `fetch`, `WebSocket`, `WebSocketServer` |
| R3 | network methods + fs/process | 10 | `req.body` needs the fs ReadStream; `res.json`, `json<T>()`, the stream events, the process argument list |
| R4 | process/env + worker + parallel | 10 | the spawn listener, `env.get` options, `stream.on`, the worker module, `parallel.for` |
| R5 | the whole `parallel.*` family | 10 | `reduce`, `run`, the closure shapes, the re-export - one shared range/combine protocol |
| R6-R7 | the call forms and the CLI | 17 | the listener argument, the method call, the top-level declaration, `cp3c build`/`check` |

This is why the marker count will not reach zero in one more wave: the
remaining work is concentrated where the families meet, and those seams are a
design decision, not ten independent markers.

### Rules every lane obeys

- **No marker is deleted to make a number go down.** A marker is retired only
  when the construct actually works, and a corpus case proves it. A "not
  implemented" that becomes a wrong program is worse than a skip.
- A construct that cannot be completed correctly stays a marker, and the
  report says why. A skipped item is honest; a wrong implementation is not.
- Every retired marker gets a corpus case that would fail before the change
  and passes after it.
- The CP3002 count, the diagnostics gate (0 findings) and the corpus are
  checked by the lead after every merge.

## 7. Work already done vs not done

Done:

- Full-history preservation and per-worktree WIP capture.
- Fast-forward of `master`; one long-lived local branch.
- Worker merge and the typed network JSON regression fix.
- Build, diagnostics, CP3002 scan, full corpus pass with 0 failures.
- Baseline benchmark table for the runnable compute rows.
- Benchmark reporting WIP consolidated.
- Nine stdlib benchmarks integrated into the one suite; full run:
  1873 found, 1813 passed, 0 failed, 33 skipped, 27 ambiguous; 24 benchmark
  rows, no output mismatches.
- Server benchmark measured: cp3 17,354 req/s single-core and 103,372 req/s
  multicore; baseline re-measured in the 2026-10-23 full-suite run.
- Ten-lane ownership plan drafted at
  `build/scratch/rounds/TEN-LANE-PLAN.md`.
- Parked-worktree cleanup: 20 non-perf worktrees classified, 2 bounded
  corrections merged, 9 already-merged archives deleted, 3 real features
  deferred intact, 6 waste entries deleted after inspection.
- The Windows Clang toolchain gap closed: a stock LLVM install beside a MinGW
  GCC install is now `ready` and builds real programs with no user flags.
- A 0-failure corpus.

In flight:

- Two performance lanes (foundation/int, runtime map/parallel/async) porting
  the parked perf stack onto current master.
- Editor documentation: JSDoc-style doc comments in cp3 source, standard
  library member documentation, and LSP hover/completion surfacing, plus the
  cp3 logo and the `.cp3` file icon.
- The native-install compiler half (the remaining 45% of that feature).

Not done, by instruction:

- No 10-lane launch yet (the benchmark consolidation is complete; the wave is
  next).
- The consumer no-recompile link path for the `.cp3pak` cache, blocked on the
  frontend not resolving package imports to C++ yet.

Authorized and queued:

- The 10-lane implementation wave: five CP3002 lanes (C1-C5) and five
  performance lanes (P1-P5), one integration branch, with a verify-and-merge
  gate after every lane. The lane ownership map is
  `build/scratch/rounds/TEN-LANE-PLAN.md`.

Known open, recorded in §6a:

- The editor's colour scheme does not match the JavaScript conventions.
- Completion is context-blind: it offers every keyword at every position and
  does not order by scope.

The next decision is which of the 18 CP3002 groups to attack first, and
whether to re-measure the five performance gaps after the perf lanes land.
