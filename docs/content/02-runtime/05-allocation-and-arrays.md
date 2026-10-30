---
title: Allocation, arrays, and object churn
chapter: Chapter 2 · Runtime and memory
---
# Allocation, arrays, and object churn

Allocation-heavy workloads combine object allocation, handle copies, container growth, and destruction. Optimizing only one cost may leave the benchmark dominated by another.

## Ordinary allocation path

Class and array reference values normally use the `Ref<T>` / `std::shared_ptr` model. Arrays are backed by `std::vector`. A growing vector may allocate a larger backing store and move or copy its elements. If each element is a shared reference, that movement also copies reference handles.

## Proven local objects

Escape analysis can change selected class allocations into stack objects and selected local arrays into value storage. Eligibility depends on known constructors, scalar field operations, same-frame use, and non-escape. See the detailed [escape analysis rules](#02-runtime/02-escape-analysis).

## Counted-loop reserve hint

The code generator also has a narrow counted-push analysis that can reserve expected array capacity before an eligible loop. It requires a recognized induction variable, known constant bound, and a provable push pattern. Conditional pushes may reserve an upper bound. Unknown calls, mutated bounds, or uncertain aliasing prevent the hint.

The hint does not pre-size the array’s logical length; it reserves vector capacity. `length` and subsequent push behavior still follow the language API.

## What is not done

- No universal arena allocates every short-lived object.
- No global object pool batches all destruction.
- A conservative compiler gate can select non-atomic `LocalRef<T>` for some class types; this is a compilation-unit decision, not a per-object escape proof.
- No reserve size is guessed from a benchmark name or source path.

The fallback remains the ordinary atomic shared reference and vector behavior. `LocalRef` still allocates a control block, though the runtime pool may reuse its storage; neither fact makes it a stack allocation.

## Diagnose object churn

For a benchmark like `object-churn`, verify that the compiler used the optimized source path by inspecting generated C++, then compare allocation counts or profile runtime calls. A single median can mislead if other processes are competing for CPU or memory. Keep the run’s repetition count, variation, output oracle, and compiler binary identity with the result.

For `array-growth`, compare capacity growth, element representation, reserve calls, and the number of reference copies. Do not change the benchmark source to make the compiler pass fire; improve the general proof or keep the normal behavior.

## What vector growth costs

A CP3 array is a reference to a `std::vector<E>` in the current runtime. The vector's `size` is its logical element count; `capacity` is the amount of storage it currently has. When a push exceeds capacity, the vector requests a larger allocation and moves or copies existing elements. Capacity is not directly observable in CP3, but the element work and reference-handle traffic are.

For a `number[]`, moving elements is a value copy. For an array of class references, moving elements also copies shared handles. For objects with nested arrays or strings, the element movement may be cheap handle copies while allocation and destruction occur at the referenced values' lifetimes.

## What the counted-push hint actually proves

The code generator recognizes a deliberately simple loop shape: a plain counter, a less-than or less-than-equal bound check, one unit increment of that same counter, and exactly one push site in the loop body. The push receiver must be a plain array identifier and the call must have one non-spread argument.

The loop body may include supported side-effect-free conditions around one push. In that case the emitted reservation is an upper bound: some iterations may not push, so the array can end with less capacity in use than reserved. The transformation reserves remaining iterations before the loop; it does not pre-fill the vector or change array length.

The hint is skipped when the body has `break`, `continue`, nested loops, multiple push sites, an unrecognized increment, uncertain array target, or operations that could invalidate the proof. Do not simplify these fail-closed checks as though they were arbitrary compiler conservatism: moving allocation before a loop can change observable failure ordering if the proof is not sound.

## Example shape

A supported loop can look like this:

```ts
const kept: Cell[] = [];
let i = 0;
while (i < limit) {
  if (shouldKeep(i)) {
    kept.push(new Cell(i));
  }
  i = i + 1;
}
```

The actual accepted `shouldKeep` and constructor forms are narrower than this illustrative example: calls inside the condition or constructor can prevent the hint. Check the generated C++ for the exact source before concluding that reserve was emitted.

The reserved amount is `bound - currentCounter`, so a loop entered with a nonzero starting counter can reserve only the remaining iterations. For `<=`, the calculation includes the inclusive endpoint. When a conditional push is present this remains a capacity upper bound, not an exact count.

## Reservation failure and semantics

The runtime reserve helper is a performance hint, not a new CP3 operation. It does not alter `length`, iteration order, values, aliasing, or the result of `push`. A failed capacity request is ignored so the ordinary vector growth path can still carry out later pushes or fail through its normal allocation behavior.

Do not turn the hint into an unconditional `resize`: resizing would create logical elements and change source semantics. Do not add a guessed reserve based on benchmark names, file paths, or expected output. The compiler's proof should depend on the loop and source operations.

## How to verify a reserve transformation

1. Build the CP3 compiler after changing codegen.
2. Compile a small representative source with `--show-runtime`.
3. Locate its generated function and find the array reserve helper before the loop.
4. Confirm the emitted amount matches the remaining loop bound.
5. Check that an ineligible loop still has ordinary vector growth and no injected reservation.
6. Compare correct output and benchmark variation before attributing a speedup.

The implementation is `src/codegen/src/emit/misc/push_reserve.cpp`. It is separate from class escape analysis. A reserve hint avoids some vector reallocations; it does not remove allocation of each element or shared-pointer refcount operations.

## Relating this to benchmark families

`array-growth` stresses container capacity and element movement. `object-churn` stresses object creation, ownership, and destruction as well as the kept-object array. A reserve hint can address only the capacity-growth part; it cannot make escaping objects stack allocated. Measure the targeted effect and inspect generated code before deciding what remains expensive.

## Related pages

- [Escape analysis and stack storage](#02-runtime/02-escape-analysis)
- [The CP3 value model](#02-runtime/01-value-model)
- [Reference counts, cycles, and threads](#02-runtime/03-reference-ownership)
- [Inspecting runtime costs](#02-runtime/11-inspecting-runtime-costs)
