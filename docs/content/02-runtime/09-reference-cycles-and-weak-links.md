---
title: Weak links and reference cycles
chapter: Chapter 2 · Runtime and memory
---
# Weak links and reference cycles

CP3's ordinary reference is strong: it keeps its target alive. Strong reference counting cannot reclaim an object graph whose members keep one another alive. A weak field lets an object refer back to another object without owning that target.

## The strong-cycle problem

Suppose a parent owns its child and the child stores a strong reference back to its parent:

```text
parent --strong--> child
   ^                  |
   +------strong------+ 
```

After the rest of the program drops its references, the two fields still own each other. Their counts do not reach zero, so reference counting cannot destroy either object. CP3 has no tracing collector that later discovers the unreachable cycle.

## Declare a weak field

Mark a class field with `weak`. Weak fields are intended for back-links and other non-owning relationships:

```ts
class Parent {
  child: Child | undefined;
}

class Child {
  weak parent: Parent | undefined;
}
```

The parent strongly owns the child, while the child's link to the parent does not keep the parent alive. The compiler lowers a weak field to a runtime `WeakRef<T>` backed by `std::weak_ptr<T>`.

## Assign and read a weak link

A weak field can be assigned a strong class reference. Reading it has the optional type `T | undefined` because the target may already have been destroyed:

```ts
let parent = new Parent();
let child = new Child();
parent.child = child;
child.parent = parent;

const maybeParent: Parent | undefined = child.parent;
if (maybeParent !== undefined) {
  // Use the live target through this local handle.
}
```

The local result of a successful read is a strong handle to the live target. The check is not a permanent guarantee that other code cannot later mutate state; it is a value read that has already acquired a handle. Keep the read and subsequent use explicit.

## Empty and expired targets

A newly constructed weak field with no assignment reads as `undefined`. Assigning `undefined` or `null` clears the weak field where the declared type permits it. If the target has no remaining strong references, a later weak read also returns `undefined`.

The conformance cases `tests/cases/memory-weak/weak/weak_unset_reads_undefined`, `weak_target_destroyed_reads_undefined`, and `weak_alive_behaves_normal` pin the basic observable cases. `weak_narrow_before_use` checks that code handles the optional result before member access.

## Use a weak field only when non-ownership is intended

Weak references are appropriate for parent pointers, observer links, caches that can be recomputed, and graph edges that must not own their targets. They are not a drop-in replacement for ordinary fields: a weak target can disappear at any time before a later read, so callers must model absence.

Prefer a strong field when the containing object requires the target to remain alive. Prefer `T | undefined` alone when the value is optional but does not need to refer to a class instance through a weak ownership link.

## Syntax versus runtime representation

The source-level `weak` modifier is valid on supported class reference fields. It is not a general weak-pointer type operator for every CP3 type. The compiler's runtime `WeakRef<T>` stores a weak pointer; reading it calls the runtime helper and produces an optional strong `Ref<T>` or `undefined`.

The expression form `{ weak: value }` is also parsed as a weak-link wrapper in supported contexts. Consult class and operator conformance cases before using it in a new context; field declaration and wrapper inference have separate rules.

## Areas to treat carefully

Some ambiguous fixture cases are intentionally recorded under `tests/cases/memory-weak/_ambiguous`. In particular, do not infer that a weak modifier is valid for a primitive field, that every weak optional spelling is accepted, or that a wrapper literal receives any desired target type. An ambiguity fixture is documentation of an unresolved contract, not a passing supported feature.

When changing weak-link semantics, review the parser's weak-expression handling, class semantic checks, generated field type, and `readWeak` runtime helper together. Add tests for live, unset, and expired targets and for cycles; do not change expected outputs merely to make the tests pass.

## Implementation map

- Parser: weak field modifiers and `{ weak: value }` expression handling.
- Semantic analysis: weak field compatibility and unsafe reads.
- Code generation: stores a weak handle and reads it as an optional strong value.
- Runtime: `WeakRef<T>`, `MaybeRef<T>`, `readWeak`, and `cloneWeak` in `src/runtime/include/cp3/core/value.hpp`.

The exact locations can move as the compiler evolves. Search for `WeakRef`, `readWeak`, and `weakFieldHit_` when tracing behavior.

## Related pages

- [Value model](./01-value-model.md)
- [Reference counts, cycles, and threads](./03-reference-ownership.md)
- [Classes and interfaces](../01-language/04-values-and-objects/02-classes-and-interfaces.md)
- [Garbage collection: what CP3 does not provide](./08-no-tracing-gc.md)
