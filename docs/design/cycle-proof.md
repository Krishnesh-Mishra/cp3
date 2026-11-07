# Whole-program ownership proof for auto-weak (design, stages 2–3 gate)

Status: **design only**. No compiler, runtime, diagnostic, or test change is
made by this document. It is the gate that must be satisfied before the
deferred stages in `ROADMAP.md` ("Regions, compiler-injected non-owning
edges and group counting (stages 2–3) are deferred: they need the
whole-program ownership proof plus the fuzzer/ASan gate") may be implemented.
Companion tooling, also design/measurement only, lives in
`vendor/tools/fuzz_graphs.py` (differential strong-edge fuzzer) and
`vendor/tools/cycle_stats.py` (auto-weak coverage %), which model the proof
obligations below without changing shipped behavior.

## 1. Baseline this proof replaces (Stage 1, shipped)

`Analyzer::computeCycleCapable` (`src/sema/src/checks/class/class_check.cpp`)
already implements a report-only strong-edge class graph:

- Walk each non-static field type recursively (`Type::parts`), including
  arrays, `Map`/`Set` values, unions, and function/closure parts.
- A `weak` field contributes no strong edge; it is recorded as a weak edge
  for cycle-breaking.
- An `unknown`-typed field is an opaque sink: its source class is marked
  cycle-capable (fail closed) and a store/escape site is recorded.
- A class is *cycle-capable* iff it can reach itself through strong edges
  (self-loop, mutual, or longer), or carries an `unknown` risk.
- Diagnostic rule (Step 4): an SCC of size > 1 with no internal `weak` edge
  is `CP3401` ("mark one edge weak"). A single self-referencing field
  (`next: Node | null`) is exempt: recursive types are not, by themselves,
  runtime cycles.
- `SemaResult::cycleCapableClasses` / `cycleEscapeSites` feed
  `cp3 check --report` (cycle-capable classes, store/escape sites,
  manual-weak share).

Auto-weak is the stage-2 successor of the diagnostic rule: instead of
rejecting an unbroken SCC > 1, the compiler may **inject a non-owning edge**
on one (or a feedback set of) edges, lowering it to the existing
`WeakRef<T>` path, and accept the program. Everything below is the
condition under which that injection is semantics-preserving.

## 2. Definitions

- **Strong edge** `A.f -> B`: a field store that increments `B`'s count and
  keeps `B` alive as long as `A` is alive.
- **Non-owning (weak) edge**: a store that does not increment the count;
  reads yield `T | null` and may be absent after `B` is destroyed.
  The runtime already provides this via `WeakRef<T>` / `readWeak`
  (`src/runtime/include/cp3/core/value.hpp`, weak field codegen).
- **Auto-weak edge**: a strong edge the compiler reclassifies as
  non-owning without a source `weak` modifier. The reclassification is a
  *semantic change* (the target may expire), so it requires a proof, not a
  heuristic.
- **Store site**: any program point where a value is written into a
  strong slot: field initialization in a constructor, `this.f = …`, an
  array element store (`push`, index assignment), a `Map`/`Set` value
  store, a global/static store, a return, an argument to an unknown or
  native callee, capture by a closure, or an export.
- **Escape site**: a store site after which the container's lifetime can
  outlive the defining frame (global, heap container, returned value,
  worker payload, native handle). Escape is the failure mode the proof
  must reason about; a purely local store is cheap to reason about.

## 3. Proof obligations

An auto-weak injection on edge `e: A.f -> B` inside SCC `S` is permitted
**iff all** of the following hold for the whole program (all units,
imports included — a per-unit proof is unsound, see P8):

- **P1 — Cycle break.** Removing every injected edge from the strong-edge
  graph makes the graph acyclic (no SCC > 1 remains, and no *new* strong
  path is created by reclassification). The injected set is a feedback
  edge set of `S`; `|injected| = 1` for a simple directed cycle, more for
  chorded SCCs. Remaining self-loops are still exempt and untouched.
- **P2 — Read safety (narrowing).** Every read of the injected edge is
  provably narrowed before dereference in the lowered program
  (`if (x !== null)`, `?.`, explicit `undefined` check, or a join where
  all paths narrow). An unnarrowed dereference of an auto-weak edge is a
  proof failure: with a strong edge it was guaranteed live, with a weak
  edge it can be absent. The compiler must either insert the lowering
  (`T | null` read) and re-check the use, or refuse injection. This is
  the obligation most likely to reject real code, and that is intended.
- **P3 — Store-site ownership (the core obligation).** For every store
  site `s` that writes a `B` through edge `e` (directly, through an
  array/`Map`/`Set` slot, or through a closure capture of `A`), the
  proof must show one of:
  1. **Independent strong path:** at every point where `A` is alive and
     a read of `A.f` can observe the stored value, `B` is also reachable
     from roots through strong edges *not* using `e` (locals, globals,
     owning containers, an owner field on the path from a root); or
  2. **Dead read:** no read of `A.f` can observe the value after `B`'s
     last independent strong handle is dropped (e.g. a back-link
     `child.parent` written once and read only while the parent is
     still rooted elsewhere).
  
  If neither can be established conservatively, the edge keeps its strong
  owner and the program keeps the manual-`weak` CP3401 (or the user
  writes `weak` explicitly). In particular, a store into a global,
  static, exported, returned, or worker-sent container **must** be
  treated as escaping: the independent-path argument must be made from
  that container's owner, not from the local frame.
- **P4 — No strengthened escape.** A read that strengthens the weak
  handle into a temporary strong `Ref<T>` (the `readWeak` result) must
  not itself be stored into a strong slot, returned, or sent across a
  worker/native boundary unless P3 is re-proved for that new store site
  with the temporary's (frame-local) lifetime. This forbids laundering
  an auto-weak edge back into a strong cycle.
- **P5 — Container edges.** An edge whose type is `B[]`, `Map<K, B>`,
  `Set<B>`, or a union containing one is a store site per element, not
  per field. The owner of the container is the owner of its elements.
  Injection through a container edge requires P3 to hold for the
  container as a whole (e.g. `parent.children: Child[]` with
  `child.parent` injected is the classic safe shape; the reverse,
  injecting `children`, is not, because the children would expire
  while the parent still lists them).
- **P6 — Threads, workers, natives.** No auto-weak edge may cross a
  worker/parallel boundary, be stored in a native handle, or be read
  concurrently with destruction on another thread. `WeakRef` uses
  `std::weak_ptr`; P3's independent-path argument does not transfer
  across threads. Any unit containing a worker boundary, parallel call,
  or native interop keeps manual `weak` for its cycle edges. (This also
  keeps auto-weak edges off the `LocalRef<T>` path: a unit with a weak
  edge already stays on atomic `Ref<T>` per the existing unit gate.)
- **P7 — Lifetime observability.** Code must not depend on the cycle
  *leaking* (a leaked cycle is never destroyed, so its destructor never
  runs; an auto-weak cycle is destroyed and destructors run). If any
  destructor, finalizer-like hook, or `static` teardown in `S` is
  observable and ordered relative to the cycle's destruction, injection
  is refused. Memory reclamation itself is not observable in CP3.
- **P8 — Opacity (fail closed).** No injection if any edge in `S`, any
  store/escape site of `S`, or any type in the SCC involves `unknown`,
  a generic type parameter, an interface/dynamic value, inheritance
  with an unresolved base, a `Function`/closure type that may capture
  `A` or `B`, or a native/FFI type. The Stage 1 rule already routes
  `unknown` to cycle-capable; auto-weak never clears that risk.
- **P9 — Determinism.** The chosen feedback set is a pure function of
  the (sorted) strong-edge graph: edges ordered by
  `(source class, field name, target class)`, SCCs sorted by member
  names. Declaration order, file order, and hash iteration must not
  change the injected edge. (The pre-weak auto-weak design failed
  exactly here: reordering classes silently flipped ownership.)
- **P10 — Reportability.** `cp3 check --report` must list, per SCC:
  the injected edge(s), every store/escape site considered, and the
  obligation(s) that failed for rejected edges, so the manual-weak
  share is auditable. A silent injection is not acceptable.

## 4. Algorithm sketch (for the implementer)

1. Build the whole-program strong-edge graph exactly as Stage 1 does,
   extended across units (fields of imported classes included).
2. Condense SCCs; discard acyclic classes and exempt self-loops.
3. For each SCC > 1 with no manual `weak` edge, enumerate candidate
   feedback edges in P9 order and test P2–P8 with the existing
   escape-analysis machinery (`escape_analysis.cpp`) extended to
   store/escape sites listed in `cycleEscapeSites`.
4. Inject the first candidate set satisfying P1–P10 (lower to `WeakRef`,
   reads become `T | null`, re-run narrowing checks). If none
   satisfies, emit the existing CP3401 naming the cycle path.
5. Re-run the Stage 1 analysis on the *lowered* graph to confirm P1
   (acyclicity) before codegen.

## 5. Fuzzer / measurement gate (before any stage-2 code)

- `vendor/tools/fuzz_graphs.py --mode graph`: generates random class graphs
  (strong/weak/`unknown`/container/union edges), computes the reference
  strong-edge graph in Python (Tarjan SCCs, cycle-capable, CP3401
  prediction — an independent implementation of §1), and diffs it
  against `cp3c check --report` output. Any divergence in
  cycle-capable set, CP3401 presence, or store/escape sites fails the
  case. This is the differential test that the graph construction —
  the input to every proof obligation — is correct.
- `vendor/tools/cycle_stats.py`: reports auto-weak coverage % (classes in
  auto-weak-eligible SCCs vs classes still needing manual `weak`),
  an **upper bound**: the Python model cannot prove P2/P3 (narrowing,
  independent strong paths), so the compiler's real coverage will be
  ≤ the reported figure until P2/P3 are implemented in the model.
- Runtime differential (`--mode runtime`, pre-existing): object-graph
  programs run under the debug detector are diffed against the
  tracing-GC oracle. Auto-weak programs additionally require an
  ASan/leak run: no leak from a broken cycle, no dangling read after
  expiry. Neither gate is met yet; that is why stages 2–3 stay deferred.

## 6. Limitations and non-goals

- The reference model in the tools is a regex/Tarjan approximation of
  the compiler's type-walk; it does not model subclassing, generics
  instantiation, or cross-unit graphs precisely. Divergence between
  the Python model and `cp3c` is reported as a fuzzer failure, not
  silently absorbed — but not every divergence is a compiler bug.
- P3's independent-strong-path argument is undecidable in general
  (aliasing through closures, natives, workers). The proof is
  therefore conservative by design: it will refuse many programs a
  human could prove safe. Those programs keep manual `weak`.
- Auto-weak changes observable lifetime: a target that a strong edge
  would have kept alive can now expire. P2/P3 exist to make that
  change explicit at every read and store site; there is no mode in
  which auto-weak is "the same program, cheaper".
- Group counting, regions, and injected-weak codegen (stages 2–3
  proper) are out of scope for this document and its tools.
