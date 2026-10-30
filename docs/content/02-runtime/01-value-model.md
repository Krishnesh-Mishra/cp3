---
title: The CP3 value model
chapter: Chapter 2 · Runtime and memory
---
# The CP3 value model

CP3 has value-shaped types and reference-shaped types. The distinction explains copies, aliases, object lifetime, and many performance results.

## Value-shaped data

Numbers, booleans, and other value forms are carried as ordinary values in generated code. Assignment copies the value. A local arithmetic update does not allocate a reference object unless the expression is placed into a reference-shaped container.

The general `number` type is a 64-bit floating-point value. CP3 also supports explicit native integer types for interop and workloads that need exact integer arithmetic beyond what a double can represent exactly. Native types have their own conversion and operator constraints.

## Reference-shaped data

Arrays, class instances, and several runtime values have reference behavior. The ordinary reference handle is `Ref<T>` backed by `std::shared_ptr<T>`:

```cpp
template<class T> using Ref = std::shared_ptr<T>;
```

and the normal object maker is:

```cpp
template<class T, class... Args>
Ref<T> makeRef(Args&&... args) {
    return std::allocate_shared<T>(RefPoolAllocator<T>{},
                                   std::forward<Args>(args)...);
}
```

The custom allocator keeps a bounded thread-local free list for allocation blocks so a later object of the same allocator specialization can reuse storage. `std::shared_ptr` still owns the handle, so its control-block reference count remains atomic. Pool reuse is not a bump arena and does not remove the shared ownership semantics.

Generated code usually copies the handle when another name receives the same reference. The object itself remains shared. This is how two aliases can observe mutations to one array or instance. For some eligible user classes, the code generator can choose `LocalRef<T>` instead: it preserves reference sharing with a non-atomic count under a conservative compilation-unit safety gate. It is still an allocated control block, not a raw pointer or stack object. See [Reference ownership](#02-runtime/03-reference-ownership) and [Escape analysis](#02-runtime/02-escape-analysis).

## Arrays

The ordinary array representation is a shared handle to a `std::vector<E>`. `makeArray` creates that shared vector. Array growth therefore has at least two costs to reason about: vector capacity changes and copying or moving its elements. If elements are themselves shared references, their handles are copied as the vector grows.

This explains why `reserve` and element representation matter for array-growth performance. The compiler only injects a reserve hint when it proves a specific loop shape and bound; see [Allocation and arrays](#02-runtime/05-allocation-and-arrays).

## Weak references

Weak fields use a `WeakRef<T>` backed by `std::weak_ptr<T>`. A weak reference does not keep the target alive. Reading a weak link can yield `undefined` when the target has expired. Use weak references to break ownership cycles intentionally, not as a general replacement for a strong field.

## Captured mutable variables

A captured binding that is reassigned uses a shared cell in the generated representation. The cell contains the current value and is shared by the enclosing function and its closures. This preserves the source-level fact that both observe one variable.

## Why this model is neither tracing GC nor Rust ownership

The runtime does not trace all reachable objects, and the language does not enforce Rust’s unique ownership and borrow rules. Strong shared references are released when their reference count reaches zero; a strong cycle can therefore remain alive. Escape analysis can remove some local allocations, but it is not a global lifetime checker.

Read [Reference counts, cycles, and threads](#02-runtime/03-reference-ownership) before designing long-lived object graphs.
