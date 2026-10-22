# CP3 consolidation and gap roadmap

Date: 2026-10-22

Status: consolidation complete, no implementation lane started. This file is the
single decision document for the next step. It records what is already in
`master`, what is preserved, what is still missing, and the measured baseline.

No benchmark, performance change, or CP3002 fix is currently running.

## 1. Repository state

| Item | State |
|---|---|
| Long-lived local branch | `master` only |
| `master` tip at consolidation | `c6ea90c` |
| Worktrees | main repository only |
| Working tree | clean |
| Compiler build | green, Ninja, `build/bin/cp3c.exe` |
| Diagnostics | 72/72 codes documented, 0 findings |
| CP3002 gate | 378 hits in 91 files |
| Full corpus | 1482 found, 1404 passed, 30 skipped, 30 ambiguous; 18 load timeouts all pass serially |
| Benchmark baseline | measured on compiler `6f1fa2c`; reporting commit `c6ea90c` does not change C++ |

The benchmark WIP in `benchmarks/bench/run_suite.py` was consolidated into
`c6ea90c` (`bench: consolidate terminal UI and gate reporting WIP`). It is
presentation and reporting only; it does not change any measurement.

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

## 4. Verification detail

### Build and static gates

- Main build: green.
- `python tools/check_diagnostics.py`: 72 codes defined, 72 documented, 434
  source files scanned, 0 findings.
- `python tools/check_cp3002.py`: 378 hits in 91 files.
  - `src`: 348
  - `tests`: 11
  - `docs`: 19
  - `packaging`: 0
  - markers: `CP3002` 123, `kNotImplemented` 9, `notImplemented` 232,
    `NotImplemented` 14.

### Corpus

First full pass at `--jobs 12 --timeout 60`:

- 1482 found
- 1404 passed
- 18 failed
- 30 skipped
- 30 ambiguous

All 18 failures were timeouts: 17 network cases and
`stdlib/map_forEach_sum`. Every one passed when rerun serially with
`--jobs 1`. A second pass at `--jobs 4` also stalled in the same network path
and was stopped. This is test-harness oversubscription (fixed ports plus many
compiler processes), not a compiler regression. Network tests must be run with
`--jobs 1`.

The 30 skipped cases are the CP3002 not-implemented cases. They are not counted
as passing.

### Baseline benchmark

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
`health-server` and `health-server-multi` were not measured in this pass; the
earlier diagnosis found server ratios client-bound and not trustworthy.

The fast rows are hello-world, string-churn, compute-single-int, and
compute-single (the last is only a small WARN, not one of the five major gaps).

## 5. The five performance gaps

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
| 18 | Worker and parallel modules | worker module statements/re-export/destructuring/module variable/entry arrow/listener parameter, parallel for/reduce/run shapes and initializer types | `emit/worker.cpp`, `emit/worker_handles.cpp`, `emit/worker_scan.cpp`, `emit/parallel.cpp` |

The scanner gate itself is `tools/check_cp3002.py`; its 30 skipped corpus cases
are the observable effect of these groups. The CP3002 exit code and catalog
entry are intentionally preserved until the true count reaches zero.

## 7. Work already done vs not done

Done:

- Full-history preservation and per-worktree WIP capture.
- Fast-forward of `master`; one local branch and one worktree.
- Worker merge and the typed network JSON regression fix.
- Build, diagnostics, CP3002 scan, full corpus pass plus serial rerun of every
  timeout case.
- Baseline benchmark table for the runnable compute rows.
- Benchmark reporting WIP consolidated.
- Ten-lane ownership plan drafted at
  `build/scratch/rounds/TEN-LANE-PLAN.md` (not started).

Not done, by instruction:

- No CP3002 feature implementation.
- No performance optimization or branch port.
- No 10-lane launch.
- No server benchmark measurement.
- No merge of the nine unique old workstreams into the current architecture.

The next decision is which of the 18 CP3002 groups and which of the five
performance gaps to attack first, and whether the 10-lane split should follow
the measured groups or the older five perf branches.
