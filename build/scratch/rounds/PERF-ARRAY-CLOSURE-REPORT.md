# PERF-ARRAY-CLOSURE-REPORT - perf array reads and counted-push reserve, ported to current master

Branch: `merge/perf-array-closure` (base `0876540`, current `emitControlDispatch`
class/object/closure architecture).
Sources read (never merged wholesale):
`wave/perf-array-safety` (`93adb33` + the preserved WIP under
`cp3-preserved/2026-10-21/worktrees/perf-array-safety/`), `wave/perf-closure-capture`
(`998febd`, "perf: reserve capacity for counted push loops").
Required reading: `build/scratch/rounds/WAVE-PERF-SAFETY-BRIEF.md` (Lane ARRAY),
`build/scratch/rounds/PERF-ROWS-DIAGNOSIS.md` (SS4 at-ref, SS5 array-growth),
`docs/rough/roadmap.md` (gaps 2, 4, 5).

## 1. What landed

### 1.1 WS2.7 array read references (from `wave/perf-array-safety`)

`const word = words[i]` used to lower to `arrayAt<E>`, which returns a fresh
`std::string` per lookup. It now binds a `const E&` into the vector, so the
per-lookup copy disappears. The element type is restricted to number, string,
boolean and enum (the scalar shapes whose copy/alias behaviour is fully
modelled).

| File | Change |
|---|---|
| `src/codegen/src/support/array_refs.cpp` | new: the whole-unit proof |
| `src/codegen/src/emit/decl.cpp` | new: `arrayRefDeclText` (the reference spelling) |
| `src/codegen/src/emit/stmt.cpp` | `declaratorText` asks for the reference spelling |
| `src/codegen/src/code_generator.cpp` | `runUnit` runs the analysis before the emit walk |
| `src/codegen/src/detail/generator.h`, `generator_state.h` | the declaration set and the method |
| `src/codegen/CMakeLists.txt` | the new unit |
| `src/std/typed-arrays/include/cp3/array_extras.hpp` | `arrayAtRef<E>` (same bounds check and panic as `arrayAt`) |

### 1.2 Counted-push capacity hint (from `wave/perf-closure-capture`)

`while (i < bound) { a.push(v); i = i + 1; }` reserves the remaining pushes
once, so the vector allocates once instead of growing geometrically. Capacity
is not an observable cp3 value, so the hint cannot change a program's result.

| File | Change |
|---|---|
| `src/codegen/src/emit/push_reserve.cpp` | new: the structural match and the hint |
| `src/codegen/src/emit/control.cpp` | `emitWhile` emits the hint before the loop |
| `src/std/typed-arrays/include/cp3/array_extras.hpp` | `arrayReservePush<E>` (a capped, failure-tolerant reserve) |

### 1.3 Closure-capture adversarial tests (from the closure-capture branch)

`tests/cases/functions-closures/closure_adversarial_*.cp3` (5 cases): shared
cells, per-iteration capture, nested mutation, object alias, escaped alias.
They pin the closure semantics the array-ref proof leans on (a capture can
outlive the frame a reference points into).

## 2. Safety argument

### 2.1 The proof (support/array_refs.cpp)

The walk is whole-unit and complete-or-nothing: a construct it does not model
sets `complete = false` and *no* candidate in the unit is accepted. A class
declaration, a destructuring pattern, an enum member with an initializer, a
`delete`, an unknown statement or expression kind all fail closed.

A candidate is `const <name> = <array>[<index>]` with an identifier array and
an identifier index. It is accepted only when all of the following hold.

1. **The array is private storage.** The array binding is declared exactly
   once in the unit and its initializer is an *array literal* (`[...]`).
   `Array(...)` / `new Array(...)` are rejected: `Array` is an ordinary global
   name a unit can shadow, and a shadowed `Array` can hand back storage that
   another name in the unit also owns.
2. **The array is never mutated, anywhere in the unit.** The scan is
   order-insensitive: a mutation textually *before* the read rejects the
   candidate too, so the proof does not depend on call order. Rejected
   mutations: every known mutator (`push`, `pop`, `shift`, `unshift`,
   `splice`, `sort`, `reverse`, `fill`, `copyWithin`, `unique`, `shuffle`),
   any element write (`arr[i] =`, `arr[i]++`), any member write rooted at the
   array (`arr.length = 0`), a binding assignment, a `delete` rooted at the
   array, a `for...of` / `for...in` rebinding of the array, and any write
   inside a free function, an arrow body or a parameter default in the unit.
   This is the brief's review finding 1 (the `grow()` helper) and finding 2
   (`length = 0`): both are mutations, both reject.
3. **The array never escapes.** The array is never passed as a call argument,
   never returned, never stored in an object or array literal, never read into
   a template literal or a conditional, never given to an unknown member
   (`holder.refresh()` on another receiver is unknown, so it rejects), never
   exported, and never aliased (`const b = words` is a value read, so it
   rejects). An import is not a declaration, so an imported array has no
   declaration and cannot be a candidate.
4. **The array outlives the reference.** The array is either a namespace-scope
   global (a module-body binding outside any block; it lives for the whole
   program) or it is declared in the *same frame* as the candidate. An array
   in an outer frame is rejected: a nested function kept past the outer call
   would outlive it.
5. **The candidate is a `const` with no declared type**, declared exactly once,
   never written (not even through a root such as `word.length = 0` or
   `word[0] =`), and read **only in the frame that declares it**. A read in a
   deeper frame is a capture, and a capture can outlive the array.
6. **The element type is a scalar shape** and the binding's own C++ spelling
   is exactly the element's (`bindingCpp(d).cpp == element.cpp`, same shape),
   so the reference and the copy have the same type at every use site.

### 2.2 Why the reference cannot dangle

The reference points into the vector owned by the array's `Ref`. The vector is
reallocated only by a mutation of the array (push/pop/splice/erase/realloc on
`reserve`), and rule 2 rejects every mutation in the unit, in any frame, in
any order. The vector's owner (`Ref`) is replaced only by a binding assignment
of the array, which rule 2 also rejects. The reference's own scope is inside
the array's scope by rule 4. Therefore the element the reference names is
alive for the whole life of the reference.

`arrayAtRef<E>` performs the same `indexInRange` check and the same
`typedArrayPanic("Array index out of range")` as `arrayAt<E>`, so an
out-of-range, negative or non-integer index panics identically.

### 2.3 Why the reference and the copy are interchangeable

A `const E` value and a `const E&` reference are interchangeable at every use
site of a scalar element:

* passed by value: both copy;
* passed as `const E&`: both read the same element;
* passed as `E&` (a cp3 `ref` parameter): **both fail to compile** - a `const
  prvalue` and a `const lvalue` are equally rejected. This is why the
  candidate is restricted to `const` (see 3.1);
* returned: the cp3 return type is always a value type, so both copy;
* stored in an object or an array: both copy;
* compared, concatenated, formatted: both read the same value.

The only difference is aliasing, and rule 2 removes it: the element never
changes while the reference is alive.

### 2.4 The counted-push hint

`arrayReservePush<E>(a, count)` only calls `reserve`, which changes the
capacity and never the size, the elements or the order. The count is
`bound - i` (plus one for `<=`), computed from an identifier bound and an
identifier or numeric literal bound, so the hint itself evaluates two
variables and has no side effect. The reservation is capped at 256 MiB of
element storage and a failed reservation is swallowed, so the hint can never
turn a working program into a failing one (it can only move an
out-of-memory failure a little earlier, in a program that was already
allocating the same memory). The body match is exact: a block of exactly one
`push` statement and one unit increment of the loop counter, in either order.
A `break`, a `continue`, a second push, a nested call or any other body shape
does not match, and the hint is skipped. The int64 speculative body is
skipped as well.

### 2.5 The closure tests

The five adversarial closure cases pin the shared-cell rules the array-ref
proof depends on: a binding captured by a nested frame and reassigned is one
shared cell; a capture can outlive the frame that owns it; an alias of an
array is the same storage. They are regression tests for the current
architecture, not for a change in this branch.

## 3. Defects found in the inherited port and fixed here

The inherited WIP (the preserved `tracked.patch` plus the uncommitted work in
this worktree) was **not** sound. Each item below was reproduced against the
inherited code before the fix.

### 3.1 A `let` candidate broke `ref` parameters (compile regression)

`let w = words[0]` lowered to `const std::string& w = ...`, and cp3's `ref`
parameter is a `std::string&`. Reproduced: the generated C++ failed to
compile with *"binding reference of type 'std::string&' to 'const std::string'
discards qualifiers"*, while the same program compiled before the change.

Fix: only a `const` candidate uses the reference spelling (support/array_refs.cpp
`findCandidate`). A `let` binding keeps the copying form, so a `ref`
parameter still sees a mutable value. Test:
`tests/cases/perf-array/let_candidate_ref_parameter.cp3`.

### 3.2 A declared type on the candidate was silently dropped

The reference spelling bypassed the `d.type` handling in `declaratorText`, so
(a) the not-implemented report for a type M1 cannot represent was lost (an
error became a success) and (b) a wider annotation (`string | undefined`)
would have produced a `std::string&` where the ordinary lowering produces a
`std::optional<std::string>`.

Fix: a candidate with a declared type keeps the ordinary lowering
(`findCandidate` and `arrayRefDeclText` both refuse it). Test:
`tests/cases/perf-array/declared_type_candidate.cp3`.

### 3.3 The index operand text differed from the ordinary lowering

The inherited port emitted `expr(index)`, while the ordinary array read emits
`indexOperandText(index)`. The two differ for an enum-shaped index: the
ordinary form emits `static_cast<double>(member)` (or reports the not-implemented
case for a string-backed enum), the inherited form emitted the enum itself,
which does not convert to the `double` index parameter.

Fix: `arrayRefDeclText` uses `indexOperandText`, so the index text is
byte-identical to the ordinary lowering.

### 3.4 `Array(...)` counted as a fresh array

The inherited port accepted `Array(...)` / `new Array(...)` as a private
array. `Array` is an ordinary global name; a unit that declares its own
`Array` (or imports one) can return storage that another name in the unit
also owns, and the proof cannot see the alias.

Fix: only an array literal is a private origin (`freshArrayInit`).

### 3.5 The candidate's own type was not checked against the element type

The reference spelling hard-coded the element's C++ type. If the binding's
own type were a different spelling (an optional, a union, a cell), the
reference and the ordinary lowering would disagree.

Fix: `arrayRefDeclText` requires `bindingCpp(d)` to be representable and to
match the element's spelling and shape, and refuses a shared cell (a cell is
read through `get()` and written through `set()`; a reference is neither).

### 3.6 The frame rule was too coarse (lost optimization, not a bug)

The inherited rule rejected a candidate read in *any* function body, so
`function first(): string { const w = words[0]; return w; }` kept the copying
form. The precise rule is a frame-depth rule: the candidate is read only in
the frame that declares it, and the array is in that frame or is a global.

Fix: the walk tracks the frame depth and the declaration frame of every name
(`Facts::depth`, `Facts::declFrame`, `Facts::reads`) and the array-outlives
condition. Tests: `function_scope_candidate.cp3` (accepted, reference form)
and `closure_capture_candidate.cp3` (rejected, a capture outlives the frame).

### 3.7 The lint budget

`collectBlocks` (a separate block walk) exceeded the 80-line hard budget.
The block list is now recorded by the main walk (`Facts::blocks`), so the
second walk is gone and `array_refs.cpp` has no lint error.

## 4. Tests

All run with the compiler built in this worktree
(`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`,
`cmake --build build --target cp3c --parallel 2`, green, no warnings from the
new units), `python tests/runner/run_tests.py --cp3c build/bin/cp3c.exe
--filter <name> --jobs 1 --no-bench`.

| Filter | Result |
|---|---|
| `perf-array` | 35 found, 35 passed, 0 failed, 0 skipped, 0 ambiguous |
| `arrays-strings` | 187 found, 186 passed, 0 failed, 0 skipped, 1 ambiguous (pre-existing `for...in` over a Map) |
| `functions-closures` | 72 found, 60 passed, 0 failed, 8 skipped, 4 ambiguous (all pre-existing: the skips are the CP3002 cases, the ambiguities are the `_ambiguous` questions) |
| `closures` | 73 found, 61 passed, 0 failed, 8 skipped, 4 ambiguous (same) |

No full corpus and no benchmark suite were run (out of scope for this lane).

The `perf-array` category is 35 cases: 30 inherited from the safety branch
(whole-unit mutation, alias, element write, helper mutation, length write,
`delete`, for-of rebinding, index forms, escaping) and 5 added here
(`let_candidate_ref_parameter`, `function_scope_candidate`,
`declared_type_candidate`, `module_scope_candidate`,
`closure_capture_candidate`).

Every case has a hand-computed oracle (the JS value of the expression, or the
documented cp3 behaviour), not a snapshot of the compiler. The spelling-level
claims (which cases keep the copying form) were checked by inspecting the
generated C++ in `tests/cases/perf-array/.cp3/build/`.

Static gates: `python tools/check_cp3002.py` reports 391 marker hits, the
same count as the base commit `0876540` (the gate already fails on the base;
this change adds no marker). `python tools/lint.py` reports no new error: the
two errors in the files it touches (`Generator::runUnit` 94 lines,
`Generator::declaratorText` 89 lines) are over the hard budget on the base
too (93 and 84 lines).

## 5. Rejected remainder (not landed, with the reason)

1. **The old int64 index path** (`arrayAtRefI64`, the old branch's R2 modulo
   index proof). Not ported: the current master has no int64 array-read fast
   path to hook into, and the old proof leaned on the old divisibility facts
   that the perf-int lane owns. The port keeps the ordinary index text, so a
   modulo index is emitted exactly as before (`modulo_index_reference.cp3`
   pins the values). Cost: no int64 index win; benefit: no dependence on
   another lane.
2. **Rejecting a candidate passed to an unknown member call** (the old
   `passedToUnknownMemberCall` rule). Not ported: in the current architecture
   the candidate is a `const E&` and cp3 has no reference-typed values, so no
   callee can retain it; the `const` restriction already covers the case the
   old rule protected (a `ref` parameter). Residual risk: a C++ builtin that
   stored a `const E&` would keep the element alive; the array is proven never
   mutated, so the value could not change, and no such builtin is on the cp3
   runtime surface.
3. **A class method that mutates the array** (the brief's review finding 3).
   Closed by fail-closed: a unit that declares a class sets the walk's
   `complete` flag, so no candidate in that unit is accepted. A class in
   *another* unit can only reach this unit's arrays through an export, and an
   exported name is rejected. Residual: none known.
4. **`words.length = 0` and the member write** (the brief's finding 2 and
   the two "check" cases). The frontend accepts them, but codegen does not
   implement an assignment to a non-identifier target, so a dangling
   reference cannot be observed at runtime today. The proof already treats
   both as mutations. The cases stay as check-mode cases that pin the
   frontend behaviour, with a comment that names the gap.
5. **`delete words[0]`**: rejected by the frontend with CP3801, so it is a
   check-mode case with the real diagnostic. The proof treats a `delete`
   rooted at the array as a mutation as well.
6. **The `for...of` rebinding of an existing array**: the frontend accepts it
   and codegen lowers it, but cp3's for-of does not re-point the binding the
   way JS does. The proof treats the rebinding as a mutation, so the
   reference is never used there; the case pins cp3's current behaviour and
   the comment says the divergence is outside this change.
7. **No benchmark numbers.** The lane scope forbids the benchmark suite, so
   this report carries no row number. The measured cause of the win is in
   `PERF-ROWS-DIAGNOSIS.md` SS4: `arrayAt` returning by value costs -3.7%
   alone on the word-frequency row (`at-ref` 1423.5 ms vs 1489.4 ms
   baseline). The whole-unit mutation rule (finding 2) means the
   word-frequency entry, whose vocabulary array is pushed to in the same unit,
   keeps the copying spelling today; the win lands for arrays that are never
   mutated in their unit.
8. **The old shared-cell closure lowering** (the `wave/closures-port` merge
   inside `wave/perf-closure-capture`) is **not** re-landed: it predates the
   current capture architecture, which already implements the shared-cell
   rule. Only its adversarial test set was ported (section 1.3).

## 6. Coverage

**100%** of the safety brief's required evidence for Lane ARRAY is landed:
17 of 17 required checks have a landing case or a landing rule (the brief's
five review findings 1-5, and the twelve required test items - direct push,
`length = 0`, helper `grow()`, alias push, element write, mutation before the
read, mutation after the block, candidate in a loop body, string and number
array, the R2 modulo index, two candidates from one array, and the
deletion/reallocation-through-a-returned-alias family).

Measured as: (5 review findings + 12 required test items) each mapped to a
named rule in `support/array_refs.cpp` and a named case under
`tests/cases/perf-array/`, counted against the brief's own list.

Both source branches' portable behaviour is landed (2 of 2): the array-read
reference proof and the counted-push capacity hint. The third behaviour in
`wave/perf-closure-capture` (the old shared-cell closure lowering) is
superseded by the current capture architecture and is recorded in section 5.8
rather than re-landed.
