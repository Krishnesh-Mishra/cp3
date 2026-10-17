# 10. Memory management

No manual `delete` / `free`, and no garbage collector. Memory is managed by **automatic reference counting (ARC) plus C++'s own RAII** - no runtime heap scan, no pauses, and no compile-time analysis pass deciding where things live:

```cp3
function test() {
  let person = createPerson();
  let numbers = [1, 2, 3];
} // both released automatically at end of scope
```

The key distinction from GC languages (Java/Go/Python/JS): a GC finds garbage by scanning memory *at runtime*, which is exactly what costs pauses and overhead. cp3 instead frees an object the instant its last reference goes away - deterministically, at a point already fixed by ordinary scope rules - using the same destructor mechanism C++ already runs at the end of every scope.

## Two kinds of values - decided, not inferred

- **Value types - plain C++ values, copied on assignment.** Primitives (`number`, `boolean`, `string`, and the other `typeSystem` scalar types, [§3](03-type-system.md)), enums, and unions of those. No heap allocation, no reference counting, zero cost beyond what C++ itself pays.
- **Reference types - always heap-allocated, always reference-counted.** Plain objects (`{ ... }`), arrays, class instances, `Map`/`Set`/`Buffer`, and promise/closure state. Each is held through a `std::shared_ptr` ([§12](12-pointers-references.md)). Assigning one to another variable, passing it, returning it, or storing it in an array/field copies the *handle*, never the object - exactly the JS sharing behavior [§8](08-variable-bindings.md) already specifies (`let b = a; b.info = 2;` changes `a.info` too).

The compiler does **not** try to prove that a particular object could have lived on the stack instead. The rule is purely by type: reference type → heap + refcount, always. That is a deliberate trade - a small, predictable allocation cost per object in exchange for a memory model that needs no whole-function analysis to be correct.

## How cleanup works - the one rule

Every reference-type variable compiles to a smart pointer, so C++'s ordinary RAII does all the work:

- When a variable goes out of scope, its destructor drops one reference.
- When the last reference to an object is dropped, the object is destroyed (and, recursively, drops the references *it* holds).
- Returning a value, putting it in an array or object field, capturing it in a closure, passing it as an argument, or using it on only some `if` branches are all the same operation - "take another handle" - so there is no special case for any of them.

```cp3
function inner() {
  let a = { data: 123 };
  return a;            // the caller receives a handle; the object stays alive
}

function outer() {
  const data = inner();   // stored → alive until `data` goes out of scope
  inner();                 // result discarded → released immediately after the call
}
```

This eliminates the common C/C++ failure modes (memory leaks, use-after-free, dangling pointers, double-free) for the vast majority of code, by construction - with one known, deliberate exception: reference cycles, which reference counting alone cannot reclaim ([§11](11-reference-cycles-the-weak-keyword.md)).

## Refcounts are atomic

Managed references use **standard atomic reference counting** - literally `std::shared_ptr` ([§12](12-pointers-references.md)). Atomic increments/decrements cost a few nanoseconds each, which is negligible for the application/server code cp3 targets, and in exchange a managed handle can never corrupt its own bookkeeping no matter which thread touches it. This is the same choice Swift's ARC makes. (An earlier draft used a custom non-atomic counter to shave that cost, at the price of forcing the compiler to prove no managed value ever crossed a thread boundary; that trade was reversed - see [§23](23-open-design-questions.md).)

Atomic refcounts protect the *counter*, not the *contents*: two threads mutating the same object's fields without synchronization is still a data race, which is why `worker` and `parallel.*` keep the rules in [§13](13-concurrency-parallel-and-workers.md).

## Closures

A closure that captures a `const`, or a value type that is never reassigned, simply holds a copy of it (or of the handle, for a reference type). A `let`/`var` that is **captured by any closure** is stored in a shared, reference-counted cell instead, so the enclosing function and every closure that captured it see the same variable - exactly like real JS. The cell's refcount is atomic like any other managed reference, but the variable inside it is an ordinary non-atomic value ([§13](13-concurrency-parallel-and-workers.md)). This is a purely syntactic rule (captured + reassignable → shared cell); it requires no analysis of how the closure is used. Closures inherit the same cycle risk, and the same `weak` fix ([§11](11-reference-cycles-the-weak-keyword.md)), as any other reference.

## Coroutines and `worker` - one memory story, one unchanged rule

- **A paused async function needs nothing special.** A C++20 coroutine's frame is heap-allocated by the coroutine machinery itself and lives until the coroutine finishes or is destroyed; the function's reference-type locals are ordinary refcounted handles inside that frame, released when the frame is destroyed, and its value-type locals live in the frame directly. It is the same ARC + RAII mechanism as everywhere else, with no separate allocation path.
- **`worker`'s copy-in is still a genuine deep copy of the argument, never a shared handle.** Atomic refcounts make sharing a handle safe for the *counter*, but not for the object behind it - cp3 deliberately exposes no locks, mutexes, or atomics to users ([§13](13-concurrency-parallel-and-workers.md)), so two threads holding the same mutable object would have no safe way to coordinate. Deep copy is what gives `worker` its JS-Web-Worker-style message-passing isolation: nothing a worker can reach is reachable from anywhere else.


