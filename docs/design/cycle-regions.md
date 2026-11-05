# Automatic cycle handling — Stage 3: regions + group counting

Status: **implemented, report-only.** Sema computes the verdicts and
publishes them in `SemaResult`; no codegen consumes them yet, so plain
per-object refcounting is unchanged for every type. This document is the
design contract a future codegen consumer must satisfy before either
verdict may change emitted code.

Related: ROADMAP §2 (automatic cycle handling), `docs/cycle-stage0.md`
(Stage 0 corpus numbers), Stage 1 (`SemaResult::cycleCapableClasses` /
`cycleEscapeSites`, built in `Analyser::computeCycleCapable`).

## 1. What Stage 3 adds

For every cycle group — a strongly-connected component of
`cycleCapableClasses` (mutual reachability over strong field edges),
settled in `computeCycleCapable` — sema publishes one
`SemaResult::CycleGroupInfo` with two independent verdicts (see the
`Analyzer::computeCycleCapable` Stage 3 block for the implementation):

1. **Region inference** — is this group a *bump-arena candidate*?
   (`regionEligible`, `regionKind = "bump-arena-candidate"`, human view
   `cycleRegions`.)
2. **Group counting** — may the group's per-object refcounts be elided
   to one counter per group? (`groupCountEligible`, human view
   `cycleGroupCounts`.)

Both verdicts are **fail-closed**: anything unknown, unprovable, or
outside the closed-world class graph yields no region / no elision and
plain refcounting. There is no GC and no cycle collector; nothing here
collects cycles — these are the two mechanisms ROADMAP §2 defers, staged
so they can only activate behind their own proofs.

## 2. Definitions

- **Strong edge** `A -> B`: a non-`weak`, non-static-class-method field of
  `A` whose type (arrays, `Map`/`Set` values, unions, function parts
  included) references class `B`. Inherited fields are **not** edges —
  Stage 1 only sees declared members — which is why §3 denies both
  verdicts whenever a base-class edge crosses the group boundary.
- **Cycle group**: an SCC of the strong-edge graph over
  `cycleCapableClasses`, singleton self-loops (`next: Node | null`)
  included. Groups are sorted; verdicts are deterministic.
- **Per-group counts**: `strongInternalEdges` (elided retain/release
  pairs), `weakInternalEdges`, `externalStrongEdges` (roots),
  `unknownRefs`.

## 3. Region inference (bump-arena candidates when short-lived)

A group is a bump-arena candidate **only if all** of the following are
provable from the class graph alone:

| # | Fact | Why a bump arena needs it |
|---|------|---------------------------|
| 1 | No `unknown`-typed member in the group (`unknownRefs == 0`) | An opaque field can hold anything; lifetime unprovable. |
| 2 | No static field (in the group, or in any class) stores a group class | A static is a program-lifetime root; a function-local arena cannot outlive it. |
| 3 | No function-typed field references the group | A closure captures a hidden reference the field graph does not count. |
| 4 | No generic or abstract member | The concrete shape is not closed; substitution/subtyping is invisible. |
| 5 | No base-class edge crosses the group boundary, either direction | Inheritance is an invisible strong edge through uncounted inherited fields. |
| 6 | No strong edge leaves the group and none enters it (closed group) | An outward edge escapes into an unknown lifetime; an inward edge may be held by a heap object that outlives the arena. |
| 7 | No user `weak` edge inside the group | Already resolved manually; the arena buys nothing. |

**Candidate ≠ activation.** These facts prove the group is *internally
closed and short-lived-shaped*; they do not prove that the group's
allocation sites are function-local (a group value could still be
returned, thrown, or stored in a global at a call site). Codegen must
prove allocation-site locality before placing any allocation in an
arena, and must fall back to `Ref<T>` otherwise. The only region kind
Stage 3 names is `"bump-arena-candidate"` (scope lifetime); caller /
request / program lifetimes are future work.

## 4. Group counting (refcount elision per group)

One group counter replaces the per-object counters of the group's
members: external roots acquire/release the group counter, strong edges
*inside* the group touch no counter at all. Eligible when facts 1–5 of
§3 hold. Facts 6–7 are intentionally **not** required:

- **External strong edges are allowed** — they are the group's roots.
- **A user `weak` edge is allowed** — it never retained anything, so
  eliding the rest of the group's counters changes nothing about it.

The elided work is reported exactly (`strongInternalEdges` internal
strong edges per group instantiation), so `cp3 check --report` can show
the saving before codegen ever acts on it.

## 5. Wiring

```
Analyzer::run (semantic_analyzer.cpp)
  └─ computeCycleCapable (src/sema/src/checks/class/class_check.cpp)
       ├─ strong-edge graph + cycleCapableClasses_      (Stage 1, unchanged)
       ├─ cycle groups (sorted SCCs)                     (shared)
       ├─ step-4 CP3401 diagnostics (unchanged behaviour)
       └─ Stage 3 verdicts per group ──► SemaResult
            cycleGroups        (structured, for codegen)
            cycleRegions       (report lines, eligible groups only)
            cycleGroupCounts   (report lines, every group)
```

`SemaResult::clear()` clears all three new tables (it previously also
failed to clear `cycleCapableClasses`/`cycleEscapeSites`; that is fixed
as part of this change). A future `cp3 check --report` consumer prints
`cycleRegions` / `cycleGroupCounts` alongside the existing cycle
report; that CLI change is **not** in this patch (allowed paths cover
sema + `analysis.hpp` only) and is listed as follow-up in §7.

## 6. Worked examples (corpus shapes)

- **BST self-loop** (`left/right: BST | null`): closed both ways, no
  unknown/static/closure/generic/base edges → **bump-arena-candidate**;
  group-count eligible with 2 internal edges elided. (Whether codegen
  activates the arena still needs the allocation-site proof: the
  corpus test assigns the root to a `const`, which is not yet checked.)
- **A/B mutual cycle** (`A.b: B`, `B.a: A`): same shape → candidate,
  2 edges elided. The step-4 diagnostic still fires (no `weak` edge);
  Stage 3 only *describes* the automatic resolution.
- **Child/Parent with `weak parent`**: `hasWeakEdge` → region
  ineligible ("already manual"), group-count eligible (the weak edge
  never counted).
- **Group with an `unknown` field**: ineligible for both verdicts,
  reason `unknown-typed field (opaque, unprovable)`.
- **Group with a static field, a closure field, a generic member, or a
  base class on either side**: ineligible for both verdicts, with the
  specific fail-closed reason recorded in the group info.

## 7. Limitations and follow-ups (explicit)

1. **Report-only.** No codegen reads `cycleGroups` yet. Activation
   additionally requires: allocation-site escape proof (regions), a
   group-counter runtime type + atomicity/threading rule (counting),
   and the fuzzer/ASan gate ROADMAP §2 demands.
2. **No whole-program proof.** Sema sees one unit's class graph;
   cross-unit groups merge only by class name, and `unknown`-typed
   fields already fail closed for exactly this reason.
3. **Inherited fields are invisible** to the edge graph (Stage 1
   limitation, inherited here conservatively as §3 fact 5).
4. **Closure capture is invisible** except as a function-typed field;
   parameters, returns, arguments and local variables are not escape
   proofs — hence candidacy, not activation.
5. **CLI report wiring** (`cp3 check --report` printing
   `cycleRegions`/`cycleGroupCounts`) and **tests** are follow-ups
   outside this patch's allowed paths; the published tables are the
   stable contract for both.
6. Short-lived is interpreted strictly as *closed, function-local
   shaped*. Request/program-lifetime regions are not inferred.
