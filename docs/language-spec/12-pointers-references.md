# 12. Pointers & references

Raw pointers exist in the generated C++ but are **not exposed** to normal C+++ code. Internally, generated code is free to use `*`, `&`, `std::unique_ptr`, `std::weak_ptr`, etc. - `weak` ([§11](11-reference-cycles-the-weak-keyword.md)) compiles down to `weak_ptr` directly, reusing it as-is.

Reference-type values ([§10](10-memory-management.md)) are held through **`std::shared_ptr`**, used as-is - standard atomic reference counting, no custom smart pointer. `weak` ([§11](11-reference-cycles-the-weak-keyword.md)) is `std::weak_ptr`. Value types (primitives, enums) are plain C++ values and involve no pointer at all.

**No raw address access exists in beta one, full stop** - there is no way, safe or unsafe, to read or write an arbitrary memory address (`0x1234`-style) from cp3 code today. This isn't an oversight; it's deliberate for the reason [§10](10-memory-management.md)/[§11](11-reference-cycles-the-weak-keyword.md) already establish: memory safety rests on every reference being a tracked, reference-counted handle, and an arbitrary address is precisely the case that model has no way to reason about (is it valid, is it owned, is it even mapped into this process?).

An advanced/unsafe mode exposing raw pointers and direct memory addresses could be introduced later as an opt-in escape hatch, separate from the core language - gated behind an explicit `unsafe { ... }` block, never required for normal cp3 code, and never silently reachable from safe code. Two concrete rules for whenever that mode is actually built, decided now rather than left for an implementer to guess:

- **A real integer type for addresses, never `number`.** `number` is always a 64-bit *double* ([§3](03-type-system.md)) - it cannot exactly represent every possible 64-bit pointer value. A future address type must use one of the existing raw-type escape hatches (`r_'u64'` or `c_'uintptr_t'`, [§3](03-type-system.md)), never the default `number`.
- **A pretty function-call syntax around a raw address does not make it safe on its own.** `let a = getAddress(x)` looks friendlier than C's `int* p = &x`, but if `a` can be stored, returned, or passed elsewhere and used *after* `x` is gone, it's exactly as dangerous as a raw pointer - the safety was never about hiding the `*`, it's about whether the reference can outlive what it points to. Any future safe-ish reference API has to be scoped and non-escaping by construction (the same pattern [§19](19-c-cpp-interoperability.md)'s native pointer safety rule already relies on for wrapping native pointers) - a callback that receives the reference and cannot let it leak out, not a plain value that can be assigned anywhere.

## `ref` - by-reference parameters

For the case where a function needs to modify the caller's actual variable, not a copy of it, `ref` marks a parameter as by-reference:

```cp3
function addTen(ref x: number) {
  x = x + 10;
}

let a = 5;
addTen(a);
console.log(a);   // 15 - the real 'a' was modified, not a copy
```

This is a thin wrapper over an existing, standard C++ feature - `ref x: number` transpiles directly to C++'s own reference parameter syntax (`number& x`). No new engineering required; C++ already handles this correctly.

