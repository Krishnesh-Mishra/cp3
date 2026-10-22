# PERF-FOUNDATION-REPORT - perf foundation / int lane (merge/perf-foundation)

Base commit: `0876540` (master). Branch: `merge/perf-foundation`.
Sources ported: `wave/perf-fixes` (`93adb33`) and `wave/perf-int-safety`
(`c8d7cd9`), re-implemented against the current `emitControlDispatch` /
class-object architecture. No old file layout was replayed.

## 1. What landed

| # | Piece | Where | Status |
|---|-------|-------|--------|
| 1 | Divisibility fact: `x % k == 0` in a branch licenses an exact `x / k` | `support/int_mode_body.cpp`, `detail/int_div_facts.h`, `emit/stmt.cpp` | landed |
| 2 | Fact invalidation on every write path + rebinding | `support/int_mode_body.cpp` | landed |
| 3 | Fact stability: a branch that defers/repeats/outsources is rejected | `support/int_mode_body.cpp` | landed |
| 4 | R1 loop-counter rewrite (`while (c < N) { ...; c = c + K; }` -> `for`) | `support/top_int64.cpp`, `emit/control.cpp` | landed |
| 5 | R2 bounded recurrence (`x = (x * A) % K`) | `support/top_int64.cpp`, `emit/assignment.cpp` | landed |
| 6 | WS1.3 module bindings become `main` locals when the entry body is their only reader | `code_generator.cpp`, `emit/module_var.cpp` | landed |
| 7 | int64 fast-path safety: calls / new / await / delete / nested writes / global writes rejected | `support/int_mode_body.cpp`, `emit/expr.cpp` | landed |
| 8 | `CP3_NOINLINE` on the int64 fast body | `runtime/include/cp3/core/arith.hpp` | landed |
| 9 | Entry-body int64 locals (the two proven shapes above) | `support/top_int64.cpp` | landed |
| 10 | 47 adversarial run cases with independent oracles | `tests/cases/perf-int/` | landed |

Files added: `src/codegen/src/support/top_int64.cpp`,
`src/codegen/src/support/top_int64_scan_expr.cpp`,
`src/codegen/src/support/top_int64_scan_stmt.cpp`,
`src/codegen/src/support/int_mode_body.cpp`,
`src/codegen/src/detail/top_int64_scan.h`,
`src/codegen/src/detail/int_div_facts.h`, `tests/cases/perf-int/`.

## 2. Safety argument, per piece

### 1-3. The divisibility fact (the `x / k` peephole)

A fact `name % k == 0` (k a positive integer literal) is recorded when an `if`
condition is exactly `name % k == 0` or `name % k === 0`, and only inside that
then-branch. It licenses `idiv(name, k)` -> a plain int64 divide (or an
arithmetic right shift when `k` is a power of two).

* **The value is in range.** The int64 fast path's invariant is `|v| <= 2^53`
  for every value in the region (`cp3/core/arith.hpp`). `name` is a frame-local
  int64 binding, so `|name| <= 2^53` at the division point.
* **The quotient is exact.** `name % k == 0` means `k` divides `name`, so
  `name / k` is an integer; int64 division truncates toward zero, which is the
  exact value. The double path's `fmod`-based remainder is zero too, so the two
  paths agree bit for bit.
* **The shift is exact.** C++20 defines `E1 >> E2` as `E1 / 2^E2` rounded
  toward negative infinity (`CMakeLists.txt`: `CMAKE_CXX_STANDARD 20`). With
  `name` divisible by `2^s`, the floor is the exact quotient, so the shift and
  the divide agree, for negative `name` as well (`tests/cases/perf-int/
  div_guard_negative.cp3` pins `-12 / 2 = -6`).
* **The fact cannot go stale.** A write to the name invalidates the fact in
  place (`dropDivisibleFacts`): every write path reaches it - plain assignment
  and compound assignment (`intAssignText`), `++`/`--` (`intUpdate`), and a
  rebinding (`intNewBinding`, which covers a block shadow and a `var`
  redeclaration). A branch that contains a loop, a call, `new`, `await`, a
  member or index access, `try`, `throw`, a function, an arrow or a class is
  rejected outright (`factStableBranch`): those can defer, repeat or outsource
  the write, and the fact is only valid on a straight-line path. The fact is
  scoped to the then-branch and truncated back to its entry depth at the branch
  exit; the else-branch never sees it.
* **A deferred binding is refused.** A binding whose bound check was deferred
  may sit outside the exact range, so `divisibleGuard` refuses to record a
  fact for it.

### 4. R1, the loop counter

`let c = c0; while (c < N) { ...; c = c + K; }` becomes
`for (; c64 < N; c64 += K) { ... }` with `c64` a `std::int64_t`.

* **The counter has exactly one write**, the increment, and the increment is
  the loop body's last statement (the proof checks the node id). The emitter
  skips that statement, so the header performs it.
* **`break` is unchanged**: the `for` header step is not executed on `break`,
  exactly as the `while` body increment was not.
* **`continue` is refused**: a `continue` in the `while` body would skip the
  increment in the `while` form but not in the `for` form. The walk attributes a
  `continue` to every enclosing `while` (not to a nested `for`, whose
  `continue` targets that `for` and does not skip the outer increment), and any
  loop with a `continue` is not rewritten
  (`counter_continue_rejects.cp3`, `counter_nested_continue.cp3`,
  `counter_nested_for_continue.cp3`).
* **The value stays exact**: `c0`, `N` and `K` are integral constants with
  `|c0|, |N| <= 2^53`, `K > 0`, and the exit value (`bound - 1` or `bound`,
  plus `K`) is at most `2^53`, so every value the counter takes and the one it
  holds on exit are exact doubles. `static_cast<double>(c64)` reproduces them.
  The 2^53 boundary is pinned by `counter_limit_boundary.cp3`.
* **The final read is unchanged**: the exit value is the first value past the
  bound in both forms (`counter_final_read.cp3`).
* **A negative-zero start is refused** (`integralInRange` rejects `-0`), since
  int64 has one zero and `1 / -0` is observable
  (`counter_negative_zero.cp3`).
* **A start past the bound is refused** (conservative; the loop would not run).

### 5. R2, the bounded recurrence

`x = (x * A) % K` becomes `x64 = x64 * A % K` in an int64 register, wrapped in
`static_cast<double>(...)`.

* **The value stays in `[0, K)`**: `x0` is required to be in `[0, K)`, `A > 0`
  and `A * K <= 2^53`, so `x * A < K * A <= 2^53`: the product is an exact
  integer, the remainder is exact, and `x` never leaves `[0, K)`. The int64
  remainder of two non-negative operands equals `fmod`, which is what the
  double path computes.
* **`A * K > 2^53` is refused** (`recurrence_big_mul.cp3`): the double product
  would round and the int64 one would not, so the two paths could disagree.
* **The emitted value has the original's type.** The rewrite is wrapped in
  `static_cast<double>(...)`, so a *nested* write (its value observed by an
  enclosing expression) hands back exactly the `number` the original
  assignment produced - the formatter and the numeric operators see a `double`,
  not an int64 (`recurrence_nested_write.cp3`,
  `recurrence_nested_format.cp3`). This is what makes the nested case provable
  rather than merely rejected.
* **A second write, an update form, a write inside a nested function or arrow,
  a shadowed name, a shadowed modulus or multiplier, and a non-constant `A`
  or `K` are all refused** (`recurrence_shadow*.cp3`).

### 6. WS1.3, the module bindings as `main` locals

A module binding becomes a `main` local only when the entry function is its
sole possible reader. `moduleNeedsGlobals_` is forced true by: a whole-program
build, a scaled server, any declared function, any worker site or worker
option, any top-level `export` or `class`, and `moduleUsesBeforeDecl` - a
whole-unit walk that refuses the local lowering when any construct it does not
model appears (a destructuring pattern, a class, an arrow, a `for...in`, an
unknown statement or expression kind) or when a binding is referenced before
its declaration. The walk is complete-or-nothing: one unmodelled construct
rejects the whole unit.

* A **declared function** cannot see a `main` local, so any function keeps the
  namespace-scope lowering (`module_global_function_read.cp3`,
  `module_mixed_scope.cp3`).
* A **top-level arrow** captures the binding; the walk marks an arrow as an
  unmodelled construct, so the unit keeps globals
  (`module_top_level_arrow.cp3`).
* A **class** reads the binding from a namespace-scope method; a top-level
  class keeps globals.
* An **export** is a link-table name; it keeps globals
  (`module_export_binding.cp3`).
* The lowering preserves the **source order** of the initializers: the
  declaration and its initializer both land in the `main` body at the
  statement's own position, exactly where the namespace-scope form put the
  initializer.
* The binding keeps its **type** (`module_local_non_number.cp3` covers a
  string, an array and a boolean).

### 7. The int64 fast-path safety rules

The fast body is emitted next to the double body, and the wrapper re-runs the
double body when a guarded operation bails. Therefore the fast body must have
no observable effect before a bail. The port now rejects:

* a **call, a construction, an `await` or a `delete`** anywhere in the body;
* a **nested write** (an assignment or an update that is not the whole
  expression of a statement): C++'s unspecified operand order would not match
  JS's left-to-right order;
* a **write to a module global or a shared cell**: the store is observable
  before the double re-run, and the target is not an int64 local at all;
* a **read of a module global, a shared cell, `NaN` or `Infinity`**: those are
  doubles, never int64;
* a **destructuring binding** in the region: its names are not modelled.

**This is a real base-compiler bug, proved on `0876540`:**

```
$ cp3c run int_call_side_effect_once.cp3      # base 0876540
hello
hello          <-- the fast body ran the call, then bailed and the double
1.5                body ran it again
```

The ported compiler prints `hello` once. The base has no effect guard at all:
`expr()` dispatched a call statement straight into the fast body.

### 8-9. `CP3_NOINLINE` and the entry-body int64 locals

`CP3_NOINLINE` keeps the checked int64 body from being duplicated into its
double wrapper (it used to be inlined into both, bloating the hot function).
The entry-body int64 locals are the R1/R2 rewrites above; the read of a proven
binding is `static_cast<double>(register)`, exact under the proof.

## 3. Tests run

Build (Release, Ninja):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cp3c --parallel 2      # green, no errors
```

Targeted families (`--jobs 1 --no-bench`):

| family | result |
|--------|--------|
| perf-int (new) | 47 found, 47 passed, 0 failed |
| variables | 58 found, 55 passed, 0 failed, 3 skipped |
| operators | 132 found, 132 passed, 0 failed |
| control-flow | 75 found, 68 passed, 0 failed, 1 skipped, 6 ambiguous |
| functions-closures | 67 found, 55 passed, 0 failed, 8 skipped, 4 ambiguous |
| types | 130 found, 123 passed, 0 failed, 3 skipped, 4 ambiguous |
| modules | 44 found, 41 passed, 0 failed, 3 ambiguous |
| classes | 51 found, 51 passed, 0 failed |
| arrays-strings | 186 found, 185 passed, 0 failed, 1 ambiguous |
| typed-arrays | 94 found, 94 passed, 0 failed |
| errors | 26 found, 25 passed, 0 failed, 1 ambiguous |
| syntax | 72 found, 72 passed, 0 failed |
| diagnostics | 9 found, 9 passed, 0 failed |

The skipped and ambiguous counts are the pre-existing markers of those
families (they are identical on the base commit).

Guards:

```
python tools/check_diagnostics.py   -> 0 findings, OK
python tools/lint_layout.py         -> 16 errors, 211 warnings
```

The base commit `0876540` scores **16 errors, 208 warnings** on the same
linter, so this branch adds **no new hard violation** (the 16 errors are all
pre-existing on the base) and 3 new soft warnings, all on the new files
(a file-size soft budget and two soft function budgets). The linter was red
before this branch and is red after it, with the same error count.

## 4. Coverage

**89%.** Measured as: of the 9 items the INT lane of
`WAVE-PERF-SAFETY-BRIEF.md` asks for (7 numbered review findings + the
`tests/cases/perf-int/` requirement + the compute-single performance target),
8 landed; the 9th is the compute-single benchmark target, which this lane's
task explicitly excludes (no benchmarks, no full suite). 8/9 = 88.9%, rounded
up to 89%.

## 5. What is NOT here, and why

**Other lanes (not this lane's ownership, per the brief):**

* WS2.7 array read references (`src/codegen/src/support/array_refs.cpp`,
  `src/std/typed-arrays/include/cp3/array_extras.hpp`) - the brief assigns
  these to the ARRAY lane.
* WS2.8 the fused map update and the `MapData` hash index
  (`src/std/collections/**`) - the MAP lane.
* The chunking / `reduceCombine` purity classifier
  (`src/codegen/src/emit/parallel.cpp`, `src/runtime/src/parallel/**`) - the
  PARALLEL lane.

**In this lane's scope but not done:**

* The `imulAddConst` peephole and the raw `3 * n + 1` product site (the
  brief's "safe candidates" list) are **not** implemented: removing that
  check needs a recorded per-site bound, and no such bound was produced, so
  the products stay range-checked. The `n / 2` site of the same loop **is**
  covered by the divisibility fact (item 1).
* The compute-single benchmark numbers are **not** measured: this lane's task
  forbids benchmark runs and the full suite. The correctness side of the row
  is covered by the int64 fast-path tests.
* The `CP3_NOINLINE` A/B (`always_inline` hurt, `noinline` helped) was taken
  on the source branch; it is not re-measured here.

**Rejected as unsafe (fail-closed decisions, kept in the code):**

* A divisibility fact is **not** established in a branch that contains a loop,
  a call, `new`, `await`, a member or index access, `try`, `throw`, a
  function, an arrow or a class. Cost: one remainder check.
* Module bindings **stay globals** when the unit contains any function,
  arrow, class, export, worker, or a use-before-declaration, or any construct
  the whole-unit walk does not model.
* A binding whose name is bound more than once in the unit, or referenced
  before its declaration, or written inside a nested function or arrow, is
  **not** rewritten to int64.
* An R2 recurrence with `A * K > 2^53`, a start outside `[0, K)`, a
  negative-zero start, a non-constant `A`/`K` or a second write is **not**
  rewritten.
* An R1 counter whose increment is not the loop body's last statement, or
  whose loop contains a `continue`, is **not** rewritten.
* An int64 fast-path body that contains a call, construction, `await`,
  `delete`, a nested write, or a write to a module global or a shared cell is
  **discarded**; the ordinary double body is the only lowering.

## 6. Residual risk

* The whole-unit walk is complete-or-nothing but not total: a *new* AST node
  kind that the walk does not model sets `complete = false` and rejects the
  whole unit, so an unknown construct is safe (it only costs the
  optimization). A construct the walk models *wrongly* would be the danger;
  the statement and expression halves are the ported walk, reviewed line by
  line against the old branch, and the 47 cases pin the shapes.
* The name-keyed rewrite assumes the binding is unique in the unit. The
  uniqueness proof counts every binding the walk sees (declarations,
  parameters, catch bindings, import names, function names). A binding form
  the walk does not model (a destructuring pattern) sets `complete = false`
  and rejects the unit.
* `static_cast<double>` on an int64 register is exact only because the proof
  bounds the value by `2^53`. The bound is checked for R1 (the exit value) and
  for R2 (`x < K` and `A * K <= 2^53`).
