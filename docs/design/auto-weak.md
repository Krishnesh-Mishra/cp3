# Automatic cycle handling — Stage 2: compiler-injected non-owning edges (auto-weak)

Status: implemented, **report-only** (the proof+fuzzer gate has not passed, so
behavior is unchanged: CP3401 still fires and every field stays a strong ref
unless the user wrote `weak`). This document is the Stage 2 design record for
ROADMAP §2 "Automatic cycle handling". It is not a region/group-counting doc
(those are Stage 3, separate).

## Goal

Today an SCC cycle group (SCC > 1) with no manual `weak` edge is CP3401: the
user must mark one edge `weak`. Stage 2 lets the compiler inject that edge
itself — treat ONE strong edge per SCC cycle group as non-owning
(weak-cost zero, a `WeakRef` member) — **when a whole-program ownership proof
holds**. When the proof is unavailable the compiler keeps CP3401 (fail
closed). No GC, no cycle collector, no runtime metadata: a proven
compiler-injected edge lowers byte-for-byte like a manual `weak` field.

## Where it lives

| Piece | File |
| --- | --- |
| Edge graph, SCC groups, one-edge selection, proof obligations, gate | `src/sema/src/checks/class/class_check.cpp` (`buildCycleGraph`, `cycleGroupsOf`, `selectAutoWeak`, `kAutoWeakGateEnabled`, `computeCycleCapable`, `userClassMember`) |
| Contract parse + `WeakRef` lowering decisions | `src/codegen/src/emit/class/auto_weak.h`, wired into `class_decl.cpp` (struct member), `class_expr.cpp` (read/store), `class_init.cpp` (class literal) |
| Report surface | existing `cp3 check --report`, which already prints `SemaResult::cycleEscapeSites`; sema now adds `auto-weak …` lines there (no CLI change needed) |

No CMake change was needed: the code lives in already-listed translation
units and one new header under the same directory.

### The sema → codegen contract (no struct change)

`SemaResult` is frozen, so the selection travels as strings in the existing
`cycleEscapeSites` vector (whose documented purpose is exactly this shared
store/escape pass). One line per intra-group strong edge of every unresolved
group, sorted:

```
auto-weak A.b -> B : candidate (escape: none)          # preview only
auto-weak A.b -> B : proven (escape: none)             # may lower as weak
auto-weak A.b -> B : rejected (reason: container-edge) # never
```

Reasons: `unknown-risk`, `container-edge`, `not-selected`. Codegen parses
these lines (`autoWeakDst` in `auto_weak.h`) and honors **only `proven`**;
`candidate` and `rejected` leave emission untouched. That is what makes
report-only the default: while sema's gate is off, it only ever emits
`candidate`.

## Algorithm (sema)

1. Build the strong-edge class graph exactly as Stage 1 did: field types are
   walked recursively (arrays, Map/Set values, unions, function parts and
   closures count), `weak` fields are excluded, `unknown` marks its source
   `unknownRisk` (fail closed), static/method members are not edges.
2. Cycle-capable classes = classes that reach themselves through strong
   edges (unknown-risk included). SCC groups of size > 1 over that set are
   the cycle groups; singleton self-references (`next: Node | null`) are
   exempt, as before.
3. For each group **without** a manual `weak` edge, collect intra-group
   edges and select **exactly one** (P4): the lexicographically smallest
   eligible `Src.field` (then `Dst`). Eligible = P1 ∧ P2 ∧ P3 (below).
4. Publish all decisions as `auto-weak …` lines (step 3 also names the
   rejected edges and why, so `--report` shows the whole choice).
5. CP3401: if the chosen edge is `proven`, the cycle is broken at zero
   refcount cost and the diagnostic is suppressed. Otherwise (candidate or
   no eligible edge) CP3401 fires as today, now naming the candidate edge:
   `…; auto-weak candidate A.b -> B (report-only: whole-program ownership
   proof not yet available)`.

## Proof obligations

For the chosen edge `A.f -> B`, all of P1–P7 must hold. P1–P4 and P6 are
checked today at the field-type level; P5 and P7 are the outstanding
whole-program obligations and keep the gate off.

| ID | Obligation | Checked today? |
| --- | --- | --- |
| P1 direct | `A.f`'s type, after stripping `Nullable` / nullish union members, is exactly the class ref `B`. Container edges (`B[]`, `Map<_,B>`, function parts) cannot be one `WeakRef`. | Yes (`directClassRef`) |
| P2 no-unknown | `A.f`'s type mentions no `unknown`, and `A` is not `unknownRisk`. An opaque field is an escape the graph cannot see. | Yes |
| P3 escape-none | No escape site for `A.f` other than the field store itself (no `escape: opaque`, no unknown). The target does not escape through this field. | Field-type level only (see P5) |
| P4 one-per-SCC | Exactly one edge is injected per cycle group, deterministically (lexicographic order), so the group's strong-edge count drops by exactly one and the cycle is broken, not over-weakened. | Yes (`selectAutoWeak`) |
| P5 body escapes | The target `B` does not escape the cycle through any *body* site: no return of `this`/the target, no store to a global/static, no closure capture that outlives the owner, no native/worker crossing, no `unknown` sink. | **No — outstanding.** The Stage 1 escape sites cover field types, not bodies. This is the main missing proof. |
| P6 instance | `A.f` is an instance field. Static fields are program-lifetime roots and are excluded from the graph entirely (they can never be the injected edge). | Yes (by construction) |
| P7 gate | `tools/fuzz_graphs.py` differential fuzzing (auto-weak vs manual-weak binaries agree) and ASan runs pass on the cycle corpus. | **No — outstanding.** |

`kAutoWeakGateEnabled` (in `class_check.cpp`) is the P5∧P7 gate. Flipping it
is a one-line change that must not happen until P5 (body-level escape
analysis feeding `cycleEscapeSites`) and P7 land. Until then every chosen
edge is `candidate`, CP3401 is kept, and codegen output is identical to the
manual-`weak`-only compiler.

## Codegen lowering of a proven edge

A proven field behaves exactly like a manual `weak` field everywhere:

- **Declaration** (`memberFieldText`): `::cp3::runtime::WeakRef<T> f{}`
  instead of `Ref<T> f{}` — no refcount acquire/release on store.
- **Read** (`emitModelMember`): `::cp3::runtime::readWeak(f)` returning the
  optional strong handle `T | undefined`, so the same narrowing rules apply.
  Sema mirrors this in `userClassMember` (behind the same gate): a proven
  field's read type is unified with `undefined` and sets `weakFieldHit_`,
  exactly the manual-weak path.
- **Store** (`storeText`): `WeakRef` assignment / `.reset()` on nullish,
  including the optional-of-weak-read reassignment form.
- **Class literal** (`classLiteralInit`): `WeakRef` store / `.reset()`.
- **Constructor parameter properties**: `this->f = param;` — `WeakRef`'s
  assignment from a strong handle, identical to manual weak today.

Two implementation notes, because the frozen codegen signatures carry no
owner class: `memberFieldText`/`storeText` take only the member model, so
their callers (`emitClassMember`, `emitModelAssign`,
`emitSuperMemberAssign`) push the declaring class name onto a lexical
owner stack for the duration of the call; the member helpers check the
stack top against the sema `proven` lines (walking the base chain for
inherited fields). In report-only mode the stack is always present but no
`proven` line ever matches, so the check is a no-op.

## `--report` preview

`cp3 check --report` already prints every `cycleEscapeSites` entry, so the
preview needs no CLI change:

```
cycle-capable classes (2):
  needs weak (unresolved): A
  needs weak (unresolved): B
  store/escape: A.b : B (store: field)
  store/escape: auto-weak A.b -> B : candidate (escape: none)
  store/escape: auto-weak B.a -> A : rejected (reason: not-selected)
manual weak share: 2/2 (100%)
```

Groups that already contain a manual `weak` edge get no `auto-weak` lines
(already resolved). The candidate annotation in CP3401 names the same edge
the report prints.

## Limitations (deliberate, fail-closed)

- **Report-only by default.** Nothing changes until P5+P7; the CP3401
  error for an unbroken SCC cycle is preserved verbatim (plus the candidate
  annotation).
- **One edge per SCC.** Longer groups get exactly one injected edge — the
  minimum that breaks every cycle in the group. Over-weakening extra edges
  would add needless `T | undefined` narrowing at their read sites.
- **Direct references only.** Cycles that run through containers
  (`children: B[]`) or closures cannot be injected — there is no single
  `WeakRef` spelling for them, and a container's elements are individual
  strong owners. Those keep CP3401 (manual `weak` on a direct edge, or
  restructure).
- **No group counting / regions.** Injecting one weak edge does not elide
  the remaining strong edges' refcounts and does not place the group in a
  region; those are Stage 3 verdicts (`cycleGroups`/`cycleRegions`/
  `cycleGroupCounts`) and consume different proofs.
- **Same-unit only.** The graph is built from the current unit's resolved
  class table; a cycle whose classes span units is only as complete as
  that table. Whole-program (multi-unit) escape proof is P5's scope.
- **Dangling reads are the manual-weak contract.** A proven edge reads as
  `T | undefined`; if the strong owner dies first, the read is empty and
  the same narrowing discipline as manual `weak` applies. The proof (P3+P5)
  is what licenses the compiler to choose the edge — it does not change
  what a dead weak read means.

## Gate checklist (before flipping `kAutoWeakGateEnabled`)

- [ ] P5: body-level escape analysis publishes return/global/closure/native
      escape sites into `cycleEscapeSites`; selection requires
      `escape: none` across all of them for the target.
- [ ] P7: `tools/fuzz_graphs.py` differential runs (proven auto-weak vs
      manual `weak` generated C++ agree) clean; ASan cycle corpus passes.
- [ ] Corpus: existing CP3401 cycle tests updated to expect the proven
      lowering (struct member is `WeakRef`, reads narrow) and re-run.
- [ ] Flip the gate, re-run `--report` (candidates become `proven`), and
      update ROADMAP §2.
