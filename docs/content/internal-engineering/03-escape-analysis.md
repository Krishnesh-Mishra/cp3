---
title: Escape analysis
---
# Escape analysis

Escape analysis is the compiler optimization that lets eligible objects skip the shared heap entirely. When CP3 can prove an object never needs to outlive its defining region, it stores the object directly — a local, a stack `std::vector`, or a plain reference to it — and no `Ref<T>`, no control block, and no atomic retain/release ever exists. When the proof fails, nothing changes: the program keeps the ordinary, checked reference path.

This chapter explains what the compiler recognizes today, what disqualifies a candidate, and how you verify that the proof fired for your program.

## Why it matters

Allocation and reference-count traffic are two separate costs, and [No garbage collector](./02-no-garbage-collector.md) covers both. For object-heavy code (parsing, building intermediate records, loop-local temporaries), the winner is not "a faster allocator" — it is removing the allocation. Escape analysis is CP3's tool for that, and it is deliberately narrow so it never changes observable behavior.

## How it works

The pass is `analyzeEscapeAllocations` in `src/codegen/src/support/core/escape_analysis.cpp`, run during code generation *after* semantic analysis resolves bindings and *before* any C++ is emitted. It does not reason about source names; it walks the checked AST and binds every use to a resolved binding identity (an unresolved binding silently fails every check — the analysis is fail-closed by construction).

The analysis tracks, per binding:

- every read and write, and the statement frame it occurs in;
- member reads and writes (`obj.prop`, including writes to full field paths);
- index reads (`obj[i]`, `arr[i]`);
- calls — including method calls, which are classified by shape;
- whether the binding escapes a frame boundary (returned, stored longer-lived, captured, passed to an unknown caller).

Only then does it decide how to store each candidate. The emitted code for a proven class candidate is a plain local plus a non-owning pointer:

```cpp
// proven candidate `new Cell(1)` in frame
Cell stack_object_3 = Cell(1);
auto* const cell = &stack_object_3;
```

A proven local array becomes a stack `std::vector` (empty-literal case) or a **value array** — `std::vector<Struct>` of the element type with direct element access — when every pushed value is a fresh `new` of a class that accepts it:

```cpp
// `const xs: Cell[] = [];` with only proven pushes and scalar field reads
std::vector<Cell> stack_object_7{};
auto* const xs = &stack_object_7;
```

## What the compiler recognizes today

Five candidate shapes survive the current proof. If your code does not match one of them, the ordinary shared path is kept — that is the correct outcome, not a failure.

### 1. Stack class objects

`const c = new C(...)` becomes a direct local when **all** of the following hold:

- `C` is a known user class with **no base class**, no weak fields, and no field initializers;
- at most one constructor, and if one exists its body is only assignments of the form `this.<scalar field> = <pure expression>`;
- field accesses are statically known **scalar** fields (number, string, boolean, enum, native scalar, scalar literal types), or narrowly accepted getter forms that return a known scalar field;
- every use of the candidate stays in the defining frame and is a supported read/write pattern;
- nothing returns, exports, stores outer-side, passes to an unknown callee, or captures it in an escaping closure.

### 2. Stack local arrays

`const a = []` with element type known and only supported uses (`.length` reads, `.push`/`.pop` on a recognized shape, same-frame index reads) lowers to a local `std::vector` referenced through a plain pointer.

### 3. Value arrays of fresh objects

A proven local array that only ever receives freshly constructed, provably stack-safe class instances, and whose indexed accesses are all scalar field reads, is stored as `std::vector<Struct>` of the element type — the objects live inline in the vector, and `v[i].field` reads directly without handles. This is scalar-replacement-*like* (it decomposes this particular loop's objects), but it is **not full SROA**: the compiler does not decompose arbitrary aggregates into independent registers.

### 4. Alias bindings

`const b = a` where `a` is a proven stack object becomes another non-owning pointer to the same storage — aliases share, never copy.

### 5. Non-owning parameter passing

For calls to top-level functions whose class-typed parameter is used only for scalar reads (verified getters or direct scalar data-field reads, no writes, no escaping uses, frame-confined), the analysis records a by-reference parameter (C++ `T&`) and the call site passes `*expr` instead of a copied handle — no atomic handle copy at the call.

## What disqualifies a candidate (fail closed)

Every one of these keeps the ordinary `Ref<T>` path:

- a base class, a weak field, a field initializer, or an unsupported constructor shape;
- a reference-valued or optional field used in a way the analysis does not understand;
- an unknown or effectful call on the object or its array;
- an alias the walk cannot trace through resolved bindings;
- a use from another frame, or a capture by a closure whose lifetime escapes;
- returning the object, exporting it, storing it into longer-lived state, or inserting it into an escaping collection;
- any member index/access pattern outside the structural analysis;
- any unresolved binding identity.

Adding a single unsupported construct — one weak member, one foreign call — is enough to send the candidate back to the shared path. The proof is deliberately not "mostly local is good enough."

## Thread-confined `LocalRef` (the sibling decision)

The same pass also chooses the class representation: classes in a compilation unit that shows **no worker/parallel boundary, no weak references, no interfaces, no inheritance, and nothing exported** may use the non-atomic `LocalRef<T>` instead of `Ref<T>` (see [No garbage collector](./02-no-garbage-collector.md#localref-paying-for-counting-only-where-someone-might-look)). That decision is unit-wide and coarse — it is *not* escape analysis and it does not stack-allocate anything; it removes atomic traffic for handles that would exist anyway. Stack storage and `LocalRef` compose: a stack candidate never allocates at all, and a non-stack candidate in a clean unit may still use `LocalRef`.

## The counted-push reserve hint

A separate, equally narrow pass (`src/codegen/src/emit/misc/push_reserve.cpp`) recognizes a `while` loop with:

- a plain counter, a comparison against a known bound, and a unit increment of that same counter;
- exactly one push site in the body (a single side-effect-free conditional push may contribute an upper-bound reservation);
- a push receiver that is a plain array identifier with a single non-spread argument;
- no `break`/`continue`/nested loops/unrecognized calls.

When all of that holds, it emits one `::cp3::runtime::arrayReservePush(arr, bound - i)` **before** the loop — the vector allocates its capacity once instead of growing geometrically. It reserves *capacity*, never *length*; a failed reservation is ignored; `length` semantics are untouched; and a 256 MiB cap applies. Do not generalize it to arbitrary loops — the fail-closed checks exist so the reservation never precedes an observable effect that wouldn't have happened anyway.

## What remains out of scope

- **No SROA.** The compiler does not split arbitrary aggregates into independent SSA values; value arrays are the entire extent of "decomposed" storage.
- **No arena.** Short-lived objects are not bulk-allocated from a thread-local arena; escaping objects keep individual `Ref<T>` handles.
- **No general dataflow.** This is a local, intra-frame proof over resolved bindings — a whole-program ownership system would be a different compiler.
- **DataView loop analysis is a recorded, unconsumed scaffold.** The pass (`analyzeDataViewLoops`, same file) detects narrow `getUint8`/`getFloat64` reduction loops over fresh fixed-size typed arrays, but nothing in the emitters consumes `fastDataViewReads_` today. Do not expect vectorized loads from this; the typed-array hot loop is only as fast as the generated code says it is — inspect it.

## How to prove it fired

1. Build or run with `--show-runtime` and read the generated module function.
2. Look for your object: `T stack_object_<n> = ...; auto* const name = &stack_object_<n>;` — direct local storage, proof fired.
3. A stack-lowered local array appears as `std::vector<…> stack_object_…{}`; a value-array appears as `std::vector<ElementStruct>` with no `Ref` at all; a by-reference parameter appears as `T&` in the callee's signature and a dereference at the call site.
4. A non-stack object in a clean unit appears as `makeLocalRef`/`LocalRef<…>`; in an ineligible unit, as `makeRef`/`Ref<…>`.
5. To prove it did *not* fire, add one unsupported use (a return, an export, one weak field) and confirm the ordinary shared path is preserved — failure must be silent and semantically invisible.
6. Verify observable behavior first (the correct tests), then measure with enough repetitions to report variation. A benchmark win is not evidence by itself; the generated code is.

## Related pages

- [No garbage collector](./02-no-garbage-collector.md) — lifetimes, `Ref`, `LocalRef`, weak links, and the cycle collector
- [Compiler design](./01-compiler-design.md) — where in the pipeline the analysis runs
- [Arrays](../language/04-values-and-objects/04-arrays.md) and [Optimization pipeline](../cli-and-compiler/03-optimization-pipeline.md) — user-facing summary of this machinery
