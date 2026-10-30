# PERF RUNTIME REPORT - map + parallel + async port onto the current architecture

- Date: 2026-10-22
- Branch: `merge/perf-runtime` (base `0876540`)
- Worktree: `C:\Users\Krishnesh\.local\share\opencode\worktree\596ccf\merge-perf-runtime`
- Source branches ported (behavior only, never file layouts):
  `wave/perf-map-safety` (d172c33), `wave/perf-parallel-safety` (cd45976),
  `wave/perf-async-fanout` (5773cc1)
- Safety brief: `build/scratch/rounds/WAVE-PERF-SAFETY-BRIEF.md`
- Measured causes: `build/scratch/rounds/PERF-ROWS-DIAGNOSIS.md`
- Async audit: `build/scratch/rounds/WAVE-PERF-ASYNC-REPORT.md`

## 1. What landed

### 1.1 Map (WS2.8 fused update + hash store)

- `src/std/collections/codegen_map.cpp` (new): the fused
  `const v = m.get(k) ?? d;` + `m.set(k, value)` lowering, its pure-read
  proof (`repeatable`), the shape check (`fusedMapShape`) and the unique
  generated-name suffix.
- `src/std/collections/codegen.cpp`: the `Map`/`Set` surface only (the fused
  code moved out so both files stay inside the layout budget).
- `src/std/collections/include/cp3/std/collections.hpp`: `MapData` is now an
  insertion-ordered key vector (the iteration order, D44) plus a hash index
  that owns the values. `set`/`get`/`has` are one probe; `remove` erases from
  both; `keys`/`values`/`entries`/`forEach`/`size`/`clear` walk the key
  vector. `findRef` (a mutable slot pointer) and `getOrInsert` back the fused
  update.
- `docs/standard-library/04-imported-std-modules.md`: the Map/Set paragraph
  now states the key-vector + hash-index shape instead of "no hashing
  anywhere".

### 1.2 Parallel (chunked `parallel.reduce` with a purity + exactness proof)

- `src/codegen/src/detail/parallel_combine.h`,
  `src/codegen/src/support/parallel_combine.cpp` (new): the shape gate. The
  body must be `acc <op> rhs` (the accumulator only as the left operand,
  `rhs` never mentioning it) with the identity initializer (`0` for `+`, `1`
  for `*`), or the max/min conditional shape.
- `src/codegen/src/detail/parallel_safety.h`,
  `src/codegen/src/support/parallel_safety.cpp` (new): the pure integer
  expression proof (purity + integrality, transitive over declared
  functions, `/` only for a proved multiple of an integral literal divisor).
- `src/codegen/src/emit/parallel.cpp`: the gate decides between the
  sequential 4-argument lowering and the chunked 5-argument one; the chunked
  call carries the combine kind as a template argument.
- `src/runtime/include/cp3/parallel/parallel.hpp`,
  `src/runtime/src/parallel/parallel.cpp`: the chunked runtime. Per-chunk
  private folds, an ordered combine, a run-time exactness guard, a
  *sequential-prefix* proof, a balanced chunk split with no `total * chunk`
  overflow, and a chunk count of `4 * threads` that cannot overflow.

### 1.3 Async (fanout overhead without an ordering change)

- `src/runtime/include/cp3/async/awaiters.hpp` (new): the awaiter types
  (`PromiseAwaiter`, `ReadyAwaiter`, `ValueAwaiter`, `SleepAwaiter`), split out
  of `promise.hpp` so both stay inside the file budget.
- `src/runtime/include/cp3/async/eventloop.hpp`,
  `src/runtime/src/async/eventloop.cpp`: a move-only inline `Task`, a
  ready-queue for zero-delay timers merged with a deadline min-heap by
  `(deadline, id)`, and a microtask vector with a head index.
- `src/runtime/src/async/timers.cpp` (new): the timer builtins, moved out of
  `eventloop.cpp` so a program that does not use the loop does not link them.
- `src/runtime/include/cp3/async/promise.hpp`,
  `promise_impl.hpp`, `promise_combinators.hpp`: one tagged continuation
  list (Fulfilled / Rejected / Both) with two inline slots, a moved
  `Promise.all` input vector, and a moved result vector.
- `src/codegen/src/emit/async.cpp`, `src/codegen/src/emit/call_args.cpp`,
  `src/codegen/src/emit/decl.cpp`, `src/codegen/src/code_generator.cpp`,
  `src/codegen/src/emit/worker.cpp`: the `await sleep(ms)` and
  `await Promise.resolve(v)` lowerings, plus the shadow guards (a declared
  function, an imported binding or a local value named `sleep` / `Promise`
  disables the rewrite).
- `src/cli/src/toolchain/util.cpp`: the new `timers.cpp` runtime unit.

### 1.4 Tests

- `tests/cases/perf-map/`: 33 cases (29 ported + 4 new).
- `tests/cases/perf-parallel/`: 21 cases.
- `tests/cases/async/`: 5 new cases.

## 2. Safety argument, piece by piece

### 2.1 Fused map update

The fused form evaluates, in this order: the receiver (once), the key
(once), the probe, the fallback **only on a miss**, the value, then the
insertion and the in-place store. The unfused form evaluates the key twice,
the fallback only on a miss, the value before the insertion, then the
insertion and the store.

* **The fallback does not run on a hit.** The fallback sits in the false
  branch of `slot ? *slot : (fallback)`, exactly where `??` puts it. A
  fallback that would panic (`arr[3]` on a two-element array) therefore still
  panics only on a miss. Case: `fallback_not_evaluated_on_hit`.
* **The value is evaluated before any map mutation.** The insertion happens
  at the `getOrInsert` line, which is after the value temporary. A value that
  reads `m.size` therefore sees the pre-insert map, as in the unfused form.
  Cases: `value_reads_size_on_miss`, `value_reads_size_on_hit`,
  `value_reads_size_on_miss_in_if`, `alias_size_on_miss`.
* **The receiver cannot be invalidated between the probe and the store.** The
  receiver, key, fallback and value must all be *pure reads* (`repeatable`):
  no call, assignment, update, `delete`, `await`, `new`, closure or object /
  array construction. The only member reads accepted are `Map`/`Set` `.size`
  and `Array`/`string` `.length`, which are plain runtime reads. So the
  cached slot pointer cannot dangle and no user code can run between the
  probe and the store.
* **The key is evaluated once.** The key is a pure read, so the single
  evaluation yields what the unfused form's two evaluations yielded, and
  nothing can change the binding between them. Case:
  `key_compound_evaluated_once`.
* **Only a copyable primitive value takes the path.** The binding's C++
  spelling must be representable and its shape primitive; a class or array
  value keeps the unfused lowering.
* **The generated names cannot collide.** The fused form is an immediately
  invoked lambda, so it opens a new C++ scope and a user binding named
  `cp3_map0` is shadowed, not redeclared. The suffix is chosen so that none
  of the fused expressions mentions `cp3_mapN` / `cp3_keyN` / `cp3_slotN` /
  `cp3_valueN`. Cases: `generated_name_collision`, `generated_name_shadow`.
* **The receiver and the key are the same expression in both calls.** The
  emitted texts of the `get` receiver and the `set` receiver (and of the two
  keys) must be identical, so the fused pair really is one map and one key.
  Cases: `no_fusion_receiver_differs`, `no_fusion_key_differs`.

Two representation hazards were found while auditing the port and are now
closed by explicit guards (both fail closed to the ordinary lowering):

* **A shared cell binding.** A captured-and-reassigned local has the
  `Cell<T>` representation (read through `get()`, written through `set()`).
  The fused form declares a plain value, which broke the cell: the C++
  compiler rejected `v.set(...)` on a `double`. Guard: `isCell(declName)`.
  Case: `no_fusion_cell_binding` (it failed to compile before the guard).
* **The int64 fast-path region.** Inside the int64 region a `number` binding
  is a `std::int64_t`; a fused `double` binding for the same name would
  contradict the region. Guard: `intOn()`. Case: `no_fusion_int64_region`.

### 2.2 MapData hash store

* **The iteration order is unchanged.** The keys live in an
  insertion-ordered vector, and `keys` / `values` / `entries` / `forEach` /
  `size` walk that vector. The hash index only answers "where is the value
  for this key". D44 (insertion order) is preserved exactly.
* **The key equality is SameValueZero**, which is what a JS `Map` does:
  `==` for everything, with a NaN matching itself. This is *required* by the
  hash store: with plain `==` a NaN key could never be found again, so every
  `set` of the same NaN would insert a new entry and the map would grow.
  `KeyHash` sends every NaN to one bucket and hashes `-0` like `+0`, so the
  hash agrees with the equality. Cases: `nan_key_update`, `nan_fused_update`,
  `negative_zero_key`, `negative_zero_fused_update`.
* **The slot pointer stays valid.** The index is node based, so a pointer
  into it survives an insertion of another key; the fused update only
  inserts when the probed slot was null, and then takes the reference the
  insertion returns.
* **Delete removes from both.** `remove` erases from the index and then the
  matching key from the order vector; a missing key returns false and leaves
  the order alone. Case: `delete_readd_order`, `fused_readd_after_delete`.
* **A repeated `set` keeps the position.** The value is updated in place; the
  key vector is not touched. Case:
  `insertion_order_update_keeps_position`.

### 2.3 Chunked `parallel.reduce`

The chunked path folds every chunk privately from `init` and combines the
partials in chunk order on the calling thread. The schedule is unordered, so
the gate and the runtime both have to prove the result cannot depend on it.

* **Shape.** The body must be `acc + rhs` / `acc * rhs` (the accumulator
  only as the left operand, `rhs` never mentioning it) with the identity
  initializer, or the max/min conditional. Every other body keeps the
  sequential lowering, which is the baseline semantics.
* **Purity.** `parallel_safety.cpp` accepts a right-hand side only when it
  reads no binding outside its own parameters and locals, makes no
  assignment or update to a non-local, and calls only functions that pass the
  same proof transitively. A module-level read rejects (another closure or
  worker could write it while the chunks run), a call to an unknown or
  non-pure function rejects, and recursion rejects (the memo marks a call
  that is already in progress as not accepted). Cases:
  `reduce_impure_global_read`, `reduce_mutating_helper`,
  `reduce_transitive_helper`, `reduce_parity_helper`.
* **Integer exactness.** The accepted expressions are integer literals, the
  index parameter, integer locals, `+ - * %`, the comparisons, the
  conditionals and calls whose returns and locals are integer exact. A
  division is accepted only when the numerator is a proved multiple of an
  integral literal divisor (the `x % d == 0` dominating test, or an integral
  literal numerator), so `1 / (i + 1)` rejects and Collatz's `n / 2` inside
  `if (n % 2 === 0)` qualifies.
* **The run-time guard.** Every chunk-local accumulator and every combined
  partial must be a finite exact integer strictly below 2^53. A chunk that
  crosses the boundary abandons the whole chunked attempt and the pure
  reduce runs again sequentially; purity is what makes the rerun
  unobservable. The non-`double` accumulator types keep the sequential
  semantics.
* **The sequential-prefix proof.** A chunk starts from `init` while the
  sequential fold enters it with the running total, so the chunked sum can
  be exact while the sequential double fold rounded (large values in one
  chunk, a cancellation in the next). The runtime therefore also proves the
  *sequential* order: for `+` the chunk-local extrema are checked offset by
  the ordered prefix (`total + lo` and `total + hi` inside 2^53), for `*` the
  prefix magnitude is checked against the chunk's largest local magnitude,
  and for max/min no magnitude proof is needed (max and min are associative
  and idempotent, so any integral initializer folds identically). A failed
  check reruns sequentially. Cases: `reduce_overflow_sum`,
  `reduce_overflow_product`, `reduce_product_offset`, `reduce_float_helper`,
  `reduce_float_reciprocal`.
* **The chunk split.** `parallelChunkBoundary` uses a balanced split
  (`q` per chunk, the first `r` chunks get one more) instead of the old
  `total * chunk` product, so no intermediate can overflow. The chunk count
  is `4 * threads` with an explicit overflow guard, at least 1, and at most
  the element count.
* **No deadlock.** A reduce that runs inside a pool worker sees
  `parallelChunkCount() == 1`, so the chunked path is not taken and the
  single chunk runs inline on the calling worker. Case:
  `reduce_nested_in_worker`. Cases: `reduce_empty_and_tiny`,
  `reduce_negative_range`, `reduce_sum_pure_helper_ranges`.

### 2.4 Async

* **The awaiter always suspends.** `PromiseAwaiter::await_ready` is false
  for a settled promise, so a resolved promise still resumes from the
  microtask queue and the caller regains control first. `ValueAwaiter` (the
  `await Promise.resolve(v)` lowering) and `SleepAwaiter` (the
  `await sleep(ms)` lowering) keep the same rule. Case:
  `await_caller_resumes_first`, `sleep_caller_resumes_first`.
* **The sleep resume is posted, not direct.** `SleepAwaiter` arms the same
  timer but posts the resume as a microtask from the timer callback, which is
  exactly where the ordinary `Promise<void>` continuation ran. The firing
  order inside one timer batch is unchanged.
* **The continuation list keeps the per-path FIFO.** One tagged list holds
  the Fulfilled / Rejected / Both entries; a settlement walks it once and
  runs only the matching entries, in registration order. The entries for the
  other settlement are destroyed during the drain, so a rejected promise no
  longer keeps a fulfilment-only handler (and its captures) alive.
* **The `Promise.all` state is one allocation.** The input handles are moved
  into the `AllState`, the result vector is moved into the derived promise,
  and the per-element continuation observes both settlements. The first
  rejection wins and later settlements are ignored.
* **The timer order is unchanged.** The ready queue (zero delay) and the
  deadline heap are merged by `(deadline, id)`, which is the order the old
  ordered map produced. `clearTimeout` erases from whichever container holds
  the id.
* **The shadow guards.** A declared function, an imported binding or a local
  value named `sleep` or `Promise` disables the corresponding rewrite, so a
  user binding is never replaced by the builtin. The import bindings are
  collected by the declaration pre-scan (`collectDeclaredNames`), so a
  shadow cannot slip through. Cases: `sleep_user_declared_not_rewritten`,
  `sleep_local_shadow_not_rewritten`, `promise_local_shadow_not_rewritten`.

## 3. Build and tests

Build (fresh configure, Release, Ninja):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cp3c --parallel 2
```

Result: **green**, no warnings from the changed files.

Tests (`python tests/runner/run_tests.py --cp3c build\bin\cp3c.exe --filter
<family> --jobs 1 --no-bench`):

| family | found | passed | failed | skipped | ambiguous |
|--------|-------|--------|--------|---------|-----------|
| perf-map | 33 | 33 | 0 | 0 | 0 |
| perf-parallel | 21 | 21 | 0 | 0 | 0 |
| async | 36 | 31 | 0 | 4 | 1 |
| parallel | 52 | 52 | 0 | 0 | 0 |
| stdlib (the collections / Map family) | 315 | 308 | 0 | 6 | 1 |
| concurrency | 90 | 89 | 0 | 0 | 1 |
| timer | 5 | 5 | 0 | 0 | 0 |

The `stdlib` family contains the TCP server cases. On a loaded machine some
of them exceed the 60 s per-case timeout (the C++ compile of the test
program alone competes with other lanes' compiler processes). Re-run with
`--timeout 300` the whole `network_` subset is green (29 found, 28 passed, 1
skipped), and the failing programs produce the expected output when run
directly, so the failures are timeouts, not miscompiles.

Tools:

- `python tools\check_diagnostics.py`: 0 findings.
- `python tools\lint_layout.py`: 16 errors, all pre-existing on the base
  commit (in files this port did not push over a threshold: `cli/commands/
  check.cpp`, `cli/toolchain/worker_module.cpp`, `codegen/code_generator.cpp`,
  `codegen/detail/generator.h`, `codegen/emit/call_args.cpp`,
  `codegen/emit/expr.cpp`, `codegen/emit/module_var.cpp`,
  `codegen/emit/stmt.cpp`, `support/type_map.cpp`, `sema/**`). The port adds
  **no** new lint error: the fused-map code was moved into its own
  translation unit (`src/std/collections/codegen_map.cpp`, 414 lines) and
  `Generator::emitFusedMapUpdate` was split so it stays inside the function
  budget.

No full corpus and no benchmark suite was run (the brief's rule).

## 4. Rejected / set aside

* **The per-coroutine inline resume** (the feature the async report names as
  the smallest sound way to close the last 0.1 ms of the async-fanout row) is
  **not** ported. The source branch had already rejected it once for an
  ordering break; the report's own residual section says it needs a new
  `await_transform`, a loop-phase flag and adversarial tests for nested eager
  starts, `runOnce` batches, rejection propagation and cancellation. That is
  new work, not a port, so it stays out of this branch.
* **The `Promise.all` / `race` / `allSettled` void and `race` combinators**
  keep the old two-registration shape (`addFulfilledContinuation` +
  `addRejectedContinuation`). Only the non-void `all` was moved to the
  single `AllState`. The other two are correct as they are; moving them is
  an optimization without a measured win here.
* **The `Map` `Set` backing store stays a plain insertion-ordered vector**
  with `==` dedup. The hash index was only added where it matters (the
  `Map` value lookup), so the documented D44 wording for `Set` is untouched.
* **A user class named `Promise`** is still mishandled by the *pre-existing*
  `Promise.all` / `Promise.resolve` static path (master lowers it to the
  builtin shape and the C++ then fails to compile). The port's
  `await Promise.resolve(v)` rewrite does not make this worse: it is guarded
  by the same shadow check, so it does not fire for a user class either. The
  pre-existing defect is not fixed here.

## 5. Residual risk

* The `Map` hash store is a behaviour change for a NaN key: a NaN now
  matches itself (JS `Map` semantics) instead of being a fresh key every
  time. This is required for the hash store to be consistent and is what the
  safety brief asks for, but it is an observable change to `Map` semantics.
* The chunked `parallel.reduce` only fires for a narrow shape (a pure
  integer expression over the index, or the max/min conditional). A float
  reduce, an impure helper or a division that is not a proved multiple keeps
  the sequential lowering, which is the baseline cost.
* The `parallel.for` chunk count moved from one chunk per worker to four.
  The set of indices and the result are unchanged; only the pacing differs.
* The async fanout win is carried by the runtime (no temporary promise state
  for `await sleep(ms)` and `await Promise.resolve(v)`, one continuation per
  promise instead of two, a moved `Promise.all` input vector). The ordering
  is unchanged, but the *first* await of every coroutine still suspends, so
  a caller that awaits a resolved promise still resumes after the caller.
