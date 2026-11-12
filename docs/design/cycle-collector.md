# Automatic cycle reclamation — the trial-deletion collector (design)

> **Status (v0.2.4, implementation landed).** Phases 1 and 2 are in: the
> compile-time reference graph with SCC classification, the collector-eligible
> fixpoint (a component is eligible when every member's strong edges are
> walkable class handles with no opaque target), the runtime `CycleRef<T>`
> collector with suspects list and bounded trial deletion, and codegen for
> cycle-capable classes. CP3401 is retired for components whose members are
> all collector-eligible; components that carry an opaque edge, a non-handle
> field shape, or an ineligible member keep CP3401 (fail closed). `weak`
> keeps its semantics. `ConcurrentMap` carve-out diagnostic: pending.
> The `memory/cycles` corpus is green (265/265).

## What replaces what

Today a strong reference cycle is a compile error: sema reports CP3401
(`reference cycle A -> B -> A has no weak edge; mark one edge weak to break
the cycle`) and the user has to break the cycle by hand with `weak`.

v0.2.4 replaces the error with automatic reclamation. A cycle-capable island
is freed once the whole island becomes unreachable from the live roots; while
any member is reachable, every member stays alive and the reads behave
normally. `weak` stays fully supported with the same semantics — it becomes an
*optional* optimization (a deterministic earlier release), not a requirement.

This is **not** a tracing garbage collector, and it must never become one:
no whole-heap tracing, no stop-the-world pause, no background collector
thread. The mechanism is a compile-time cycle-capability classification plus a
Nim-ORC-style trial deletion that runs only on the owning event loop thread.

## Phase 1 — the static classification (no behavior change)

`src/sema/src/checks/class/cycle_analysis.cpp` builds the whole-program
reference graph and publishes the verdict.

**Nodes**

| Node | Meaning |
| --- | --- |
| `C:<class>` | a class type (per declaration; an instantiation substitutes its type arguments where the sema knows them) |
| `OPAQUE` | the fail-closed sink |

**Edges (strong references only)**

- a class-typed field (an inherited field is a strong edge of the derived
  class too, and a static field is a program-lifetime root),
- the element type of `T[]`,
- the key and value types of `Map<K,V>` / `Set<T>` and the other containers,
- the member types of a tuple / record,
- the members of a union or an optional (`T | undefined` -> `T`),
- an interface-typed field -> every class that implements the interface (the
  closed world),
- a closure-typed field -> the classes its signature names (the type-level
  edge; the capture analysis is the finer one),
- a `Promise`-typed field -> `OPAQUE` (a promise can carry any value).

A `weak` field creates **no** edge: the link does not keep its target alive.

**Fail-closed rules.** A field typed `object`/`unknown`, a type parameter, a
native handle or a `Promise` edges to `OPAQUE`. A class that reaches `OPAQUE`
is cycle-capable: the analysis cannot see through the carrier, so it cannot
prove the class is acyclic.

**The classification.** The strongly connected components come from an
iterative Tarjan pass over the strong edges (deterministic: the node and
successor orders are sorted). A type is cycle-capable when it sits in a
component of size > 1, when it has a (transitive) self edge, or when it
reaches `OPAQUE`. Everything else is acyclic and costs nothing.

**The published tables** (`SemaResult`):

- `cycleCapable` — the verdict per class, keyed by the class declaration's
  node id (the id the symbol table already uses),
- `cycleComponents` — one entry per cycle-capable component: the classes in
  it and the concrete field paths that form the cycle,
- `acyclicClasses` — the classes with zero collector cost.

**The report.** `cp3 check --report` prints every cycle-capable component with
the field path that forms the cycle, and every acyclic class as zero-cost.

Phase 1 changes no ownership and no diagnostic: the CP3401 cycle emission
keeps its current verdict until the collector lands.

## Phase 2 — the runtime collector

`src/runtime/include/cp3/core/cycle.hpp` and
`src/runtime/src/core/cycle.cpp` carry the collector.

**The handle.** A cycle-capable value uses `CycleRef<T>` instead of
`Ref<T>` (`std::shared_ptr`). The control block lives in the same allocation
as the object and adds the cycle bookkeeping: the trial count, a 2-bit color,
the buffered flag and the intrusive suspects link. There is no separate
allocation for the bookkeeping.

**The hot path.** A release that decrements the count to zero frees the
object immediately, exactly as today. A release that leaves the count above
zero marks the object suspect (purple) and pushes it onto the per-loop
suspects list: O(1), allocation-free, no lock, no atomic. A retain of a
suspect cancels the suspicion.

**The collection.** At a loop safe point (between macrotasks, where the
microtask queue already drained) the collector runs a bounded pass:

1. **MarkGray** — the suspects and everything they reach form the island.
   Every edge into an island object decrements its *trial* count. An object
   whose trial count would exceed its real count has an external reference,
   so the island is alive.
2. **Scan** — an island object that still has an external reference (its real
   count above its trial count) keeps the whole island alive; the trial
   decrements are undone and the objects return to plain reference counting.
3. **CollectWhite** — an island with no external reference is garbage: the
   collector takes ownership (black) and frees it, clearing any `WeakRef`
   control block so an expired weak read answers `undefined`.

**The budget.** A pass visits at most 4096 objects or 200 microseconds (both
constants in `cycle.hpp`). A pass that runs out of budget is *resumable*: the
next pass continues where it stopped. The collector drains completely at the
loop exit.

**The threading.** One collector state per event loop. The state is
thread-local and the collector is single-threaded: no locks, no atomics, no
cross-thread visits. Every strong holder of a cycle-capable object must be a
counted handle; a raw `T*` cache has to become a counted or a weak handle.

**The codegen switch.** A class the sema classified as cycle-capable gets the
cycle-aware handle for its reference-holding fields, and a visitor that
enumerates those edges (`cp3_cycle_visit`). A class with no cycle-capable
type in the program keeps its current codegen byte for byte.

## The one carve-out

`ConcurrentMap` is the one cross-thread shared surface. A cycle-capable type
whose values flow into a `ConcurrentMap` cannot be trial-deleted safely
per loop, so that case keeps a compile-time error (a new diagnostic code, not
a retired one) that asks the user for a `weak` edge or a different shape.
Everywhere else the old cycle compile error is retired.

## The gates

- **Gate A** — a program with no cycle-capable type emits byte-identical
  generated C++ (diffed on hello-world, the health-server sources and five
  corpus programs).
- **Gate B** — the full benchmark suite before and after: no row regresses
  beyond the 1% noise band, and the health-server rows stay within noise.
- **Gate C** — a cycle-heavy microbenchmark (1M parent-child islands): the
  collector overhead stays under 3% against a weak-broken equivalent, and the
  peak RSS does not grow unboundedly.
- **Gate D** — no new allocation, lock or atomic on a retain/release path; no
  collection inside a macrotask; no pause longer than the per-tick budget.
