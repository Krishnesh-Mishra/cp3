---
title: Mutability and immutability
chapter: Chapter 1 · The language
---
# Mutability and immutability

Mutability has two different questions: may this **name** be rebound, and may the **value it refers to** be changed? `const` and `let` answer the first question. The value’s own API and type rules answer the second.

## Rebinding a name

```ts
const title = "Draft";
// title = "Final"; // rejected: const binding cannot be reassigned

let state = "Draft";
state = "Final"; // allowed: same inferred type
```

The compiler tracks reassignment and enforces the declared or inferred type. A mutable binding is not dynamically typed; `let state: string` still cannot be assigned a number.

## Changing a referenced value

An array is a reference-shaped value. A constant binding can still refer to a mutable array:

```ts
const queue: number[] = [];
queue.push(4); // changes the array object
```

The binding `queue` remains attached to the same array. Reassigning the name is a separate operation and is not permitted for a `const` binding.

The same distinction applies to class instances and collection objects. Whether an operation changes the referenced value is defined by that type’s methods and runtime behavior.

## Primitive values and reference values

Primitive values such as numbers, booleans, and strings behave as values when copied. Reference values can share the same underlying object. Assigning a reference usually copies a handle; it does not deep-copy every field. The runtime’s representation is described in [Values and references](#02-runtime/01-value-model).

```ts
let first = [1, 2];
const second = first;
second.push(3);
// first and second still refer to the same array value
```

Do not infer deep immutability from a `const` declaration. CP3 currently does not treat `const` as a recursive immutable type qualifier.

## Captures and mutable bindings

A closure can observe a binding from its enclosing scope. If the captured binding is reassigned, the generated program must preserve the shared-variable behavior expected by CP3. The compiler uses a shared cell for captured and reassigned bindings in the implementation paths that require it; captured values that are never reassigned can remain ordinary captures.

This distinction costs runtime work only for the captured, reassignable case. A local immutable number captured by a closure does not automatically become a shared mutable cell. See [Closures and captured variables](#02-runtime/04-closures).

## Mutability across threads

`let` is not a thread-safety annotation. A mutable value shared with a worker or parallel body must satisfy the concurrency checker’s capture and mutation rules. CP3 rejects unsupported shared writes; it does not make ordinary fields atomic merely because they are captured.

Use immutable inputs, explicit result values, or the documented parallel reduction patterns when crossing concurrency boundaries. See [Concurrency and workers](#02-runtime/07-async-and-concurrency).

## Design guidance

1. Prefer `const` when a name should remain bound to its initial value.
2. Use `let` for a name that is intentionally rebound.
3. Treat mutable arrays and objects as shared state when aliases exist.
4. Avoid strong-reference cycles unless the lifetime is deliberately managed.
5. Do not assume a closure capture or reference is thread-safe.

These are source-level clarity rules. They do not promise that the compiler can stack-allocate every object or eliminate every reference count. Optimization eligibility is narrower and documented separately.
