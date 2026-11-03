---
title: Escape analysis and stack storage
chapter: Chapter 2 · Runtime and memory
---
# Escape analysis and stack storage

The CP3 code generator performs a conservative local escape analysis. It scans bindings and uses, then changes generated storage only when the object’s observable behavior remains equivalent. An object not proven safe keeps the regular `Ref<T>` and `make_shared` path.

## What “escape” means here

An object escapes a scope when the program can still observe it after the defining frame or local region ends. Returning it, storing it in a global or an array that outlives the scope, passing it to an unknown owner, capturing it in a longer-lived closure, or crossing a worker boundary can make the object escape.

The compiler tracks uses rather than guessing from the fact that a `new` expression appears inside a loop. It also checks the candidate’s defining frame and whether writes are limited to known fields.

## Current class-object proof

The stack-object path is deliberately narrow. The current analysis requires, among other checks:

- a known user class with no base class;
- no weak fields or field initializers on the candidate class;
- a constructor with a restricted body: assignments to `this` fields using expressions the analysis recognizes as pure, or no constructor body;
- only same-frame uses the analysis understands;
- field reads and writes limited to known, non-optional scalar fields, or narrowly proven getter forms;
- no unsupported alias, escape, cross-frame use, or unknown call.

When those checks succeed, generated code can create the class value as a local stack object and refer to it through a non-owning pointer for that scope. The object itself is then not allocated with a shared-pointer control block. This is not a general stack-allocation rule for every class or every `new` expression.

## Scalar replacement and local arrays

Some proven local forms can store class values directly in a local value array. The current check requires a non-escaping local array, freshly constructed objects pushed through the recognized form, a safe constructor, and scalar field reads through indexed elements. Unsupported operations or reference-valued fields keep the regular shared representation.

This is close to scalar replacement for the fields used by the loop, but it should not be described as full SROA for arbitrary aggregates. The pass does not decompose every class into independent registers.

## Fallback behavior

The analysis fails closed. Unknown method calls, exported bindings, field aliases, weak members, unsafe constructors, cross-frame uses, or unsupported mutations prevent the rewrite. The fallback is the existing checked/shared path, preserving lifetime and alias semantics.

## Thread-confined `LocalRef` lowering

The compiler also has a conservative class-representation choice for a compilation unit. When the unit contains no detected worker/parallel boundary, weak references, interface declarations, exported values, or inherited classes, eligible non-exported, non-generic classes that do not implement interfaces can use `LocalRef<T>` instead of `Ref<T>`.

`LocalRef<T>` keeps shared aliasing and lifetime behavior, but its owner count is a plain integer because code generation only selects it under the unit-level thread-confinement gate. It is **not** a raw pointer and does not put the object itself on the stack. `makeLocalRef` still creates a control block; its storage can be reused through the runtime's bounded thread-local pool.

This gate is deliberately coarse: one disqualifying feature in the compilation unit disables the `LocalRef` choice for its classes. It is not a per-object escape proof and does not mean the compiler has established thread confinement for every arbitrary alias in a whole program. Exported classes, generic classes, interface implementations, inheritance, weak links, worker/parallel use, and units with worker configuration keep the ordinary shared-reference path.

## What remains outside the current analysis

The pass is not a whole-program ownership system. It does not prove arbitrary closure and object graphs local, does not convert all eligible objects to stack values, and does not replace all `std::shared_ptr` values with `LocalRef`. The runtime's thread-local free-list allocator reuses some allocation blocks; it is not a general bump arena that bulk-frees arbitrary escaping objects. Unknown calls and unsupported aliases keep the safe representation.

## How to verify a claim

Inspect the generated C++ for the specific source case. A stack-lowered object should appear as a local value; a class covered by the thread-confinement gate should use `LocalRef`/`makeLocalRef`; ordinary references use `Ref`/`makeRef`. Check that observable reads, writes, aliasing, and destruction still match CP3 semantics. Then consult the corresponding correctness cases and benchmark with enough repetitions to report variation.

The pass implementation lives in `src/codegen/src/support/core/escape_analysis.cpp`; the runtime ownership alias is in `src/runtime/include/cp3/core/value.hpp`.

## Analysis workflow

The analysis runs as part of code generation after semantic analysis has resolved bindings and types. It scans the program for candidate declarations, records how identifiers are read and written, records frame boundaries, and associates uses with resolved binding identities rather than relying only on source spelling. It then selects storage for candidates that meet the supported conditions.

This is a local code-generation optimization. It does not change the source type, the class declaration, or the rules for what a program may return or store. Codegen has enough information to choose a different C++ representation only for a proven pattern.

## Stack-object candidate restrictions

The current class-object path requires a known user class with no base class, weak member, or field initializer. A candidate constructor must be absent or a single constructor with a block body composed only of assignments to that object's own fields. The assigned values must use the pure expression forms recognized by analysis.

Field accesses must be statically known and have supported scalar types such as number, string, boolean, enum, native scalar, or scalar literal types. Some getter forms are accepted only when they return a known field directly. Methods with effects, arbitrary calls, optional fields, reference fields, and unknown member access prevent the direct-value proof.

The candidate's uses must remain in the defining frame and match the analyzer's supported read/write/use patterns. Returning the object, exporting it, storing it in an outer container, capturing it in an escaping closure, passing it to an unknown function, or creating an unsupported alias makes the heap representation necessary.

## Direct local arrays of objects

A narrow value-array rewrite can represent a local array's elements directly instead of storing shared handles to separately allocated objects. It requires a fresh local array and recognized pushes of fresh safe class instances, with later access constrained to known scalar fields. The array itself and the objects it contains must remain local under the proof.

This is not a general transformation of `Class[]`. Inserting an existing alias, storing the array outside the frame, exposing elements through a callback, or using unrecognized operations retains the ordinary reference representation.

## Non-owning parameter aliases

For calls to functions whose parameter uses satisfy the supported local access conditions, the analysis can record a non-owning parameter form. This avoids creating an ownership copy for specific proven uses; it does not make every function parameter a raw pointer and it does not change the caller's object lifetime requirements.

The generated C++ should be inspected at both the call site and function signature. If either endpoint retains a shared handle, do not report a complete reference-count elimination.

## What disables the rewrite

Treat the following as reasons to expect fallback unless the implementation explicitly recognizes the exact form:

- a base class, initializer, weak field, or unsafe constructor;
- a reference-valued or optional field used by the candidate;
- an unknown or effectful call on the object or array;
- an alias that cannot be traced through resolved bindings;
- a use from another function frame or a closure whose lifetime escapes;
- return, export, global storage, or insertion into an escaping collection;
- index/member patterns outside the structural analysis.

The implementation is intentionally conservative. Adding an accepted pattern requires proof that aliases, destruction timing, exception behavior, and mutation remain equivalent.

## It is not full SROA or a general arena

Scalar replacement of aggregates usually means decomposing an object into its independent fields and eliminating the aggregate completely. CP3's current optimization can use local object/value storage for constrained cases, but it does not scalarize arbitrary class graphs into registers. It also does not allocate all short-lived objects from a thread-local arena and does not replace every `std::shared_ptr` with a local reference count.

See [Reference counts, cycles, and threads](03-reference-ownership.md) for the ordinary reference path. A benchmark may still be dominated by heap allocation and reference counts if the objects escape or fail this proof.

## How to prove it fired

Use `--show-runtime` on a small representative program and inspect the generated module function. A stack-optimized object should appear as a local value or direct value-array element. A non-stack class object may instead use `makeLocalRef` if the compilation-unit safety gate permits it. Compare against a unit with a worker/parallel boundary, weak field, interface, export, or inheritance; it should keep the atomic `Ref` path. For an individual escape-analysis candidate, add one unsupported escape or field and confirm that its ordinary lifetime is preserved.

Then run the relevant correctness family and benchmark only after the code change is complete. Verify output first. Report the specific source form that transformed, the C++ evidence, and the measurement distribution; the existence of this pass alone is not a benchmark result.

## Implementation map

The pass is in `src/codegen/src/support/core/escape_analysis.cpp` and is invoked by the generator on the semantically resolved program. It uses class models and structural walks to inspect class members and expression uses. The runtime's ordinary alias and `makeRef` are in `src/runtime/include/cp3/core/value.hpp`.

Add positive and negative conformance cases when broadening the proof. Test aliasing, field mutation, object return, local closure capture, and any destructor-visible effects that the language supports. Keep uncertain cases on the existing path.
