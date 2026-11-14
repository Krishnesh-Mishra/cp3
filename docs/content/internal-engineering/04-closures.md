---
title: Closure storage and lifetime
chapter: Chapter 2 · Runtime and memory
---
# Closure storage and lifetime

A closure is a function value together with the environment it needs from the enclosing scope. Captures can extend the lifetime of the captured data beyond the function call that created the closure.

## Immutable capture

A capture that is never reassigned can be copied into the closure environment. A small scalar capture can often be stored directly in the closure object. This does not require a shared mutable cell merely because the name is captured.

```ts
function makeAdder(base: number): (value: number) => number {
  return (value: number) => base + value;
}
```

The closure must retain the captured `base` value after `makeAdder` returns.

## Reassigned capture

When a captured binding is reassigned, both the outer function and every closure that captured it must observe the same storage. The compiler represents this case using a runtime cell with shared ownership.

```ts
function makeCounter(): () => number {
  let count = 0;
  return () => {
    count = count + 1;
    return count;
  };
}
```

The returned closure outlives its creator, so `count` cannot live only in the creator’s stack frame. The cell keeps that variable alive and shared.

## Escaping closures and arrays

A closure stored in an array, returned from a function, or passed to a longer-lived owner may escape. Every captured reference must remain valid for as long as that closure can run. This is why a purely local scalar replacement proof cannot be applied to an arbitrary escaping closure graph.

## Costs to measure

Closure calls can involve an indirect call, stored capture data, and reference handles for captured state. Captured-and-reassigned variables may allocate a cell. These costs vary with the capture shape and optimization profile. Inspect generated C++ for the exact case rather than assuming all lambdas are heap allocated or all captures are free.

## Thread boundary

Capturing a value in an async continuation is not the same as sending it to a worker thread. Worker and parallel APIs have separate capture and transfer rules. A normal shared pointer’s atomic count does not make the captured value’s fields race-free.
