---
title: Functions and closures
chapter: Chapter 1 · The language
---
# Functions and closures

Functions are named values with checked parameters and a declared result. Use annotations at the function boundary even when local expressions can be inferred; the signature is the contract callers compile against.

## Declaration and return type

```ts
function add(left: number, right: number): number {
  return left + right;
}
```

The function returns `number`. Every return path must satisfy that result type. A function declared with `void` is used for work that does not return a value:

```ts
function report(message: string): undefined {
  console.log(message);
}
```

## Optional parameters and defaults

An optional parameter is marked with `?` and may be omitted:

```ts
function label(name?: string): string {
  return name ?? "anonymous";
}
```

Default parameter values are also supported by the parser and semantic checker. Keep optionality explicit in signatures so a caller can see when omission is valid.

## Function expressions and arrow functions

Arrow functions are used for callbacks and closures:

```ts
const doubled = values.map((value) => value * 2);
const isReady = (state: string): boolean => state === "ready";
```

An arrow expression can capture names from the surrounding function. Its parameter and result types are inferred when the surrounding call provides a contextual function type; annotate a callback where inference does not establish the intended shape.

## Closures

A closure keeps access to names from its enclosing scope. A captured `const` or never-reassigned binding can be copied into the closure representation. A captured binding that is reassigned must remain shared between the outer function and the closure; current code generation represents that case with a runtime cell.

```ts
function makeCounter(): () => number {
  let count = 0;
  return () => {
    count = count + 1;
    return count;
  };
}
```

In this example, the returned closure outlives the call to `makeCounter`. `count` must therefore have a lifetime that extends with the closure. This is why escaping closures are not ordinary stack-local captures.

See [Closure lifetime and capture costs](../../internal-engineering/04-closures.md) for storage and allocation details.

## Generic functions

Generic parameters let a function preserve an input type in its output:

```ts
function identity<T>(value: T): T {
  return value;
}

const answer = identity<number>(42);
```

The compiler monomorphizes supported generic instantiations into concrete native forms. Generic syntax and constraints are described in [Generics](../05-generics/01-generics.md).

## Async functions

An `async` function participates in CP3’s promise/event-loop runtime. Use `await` only in a supported async context:

```ts
async function readName(): Promise<string> {
  const line = await console.input("Name: ");
  return line;
}
```

Async arrows work the same way: call an async arrow and you get a promise of its result.

```ts
const readName = async (): Promise<string> => {
  return await console.input("Name: ");
};

async function main() {
  const name = await readName();
}
```

The body's `return` is checked against the unwrapped value type, so a promise expression is awaited before being returned — `return await somePromise`, exactly like the value coercions in ordinary functions.

Async methods on classes are supported as well (`async greet(): Promise<void> { ... }`); calling one yields a promise of its annotation, and the `return` statements inside are checked against the unwrapped value type.

An async annotation that is not a `Promise<T>` is tolerated as a value-annotation form: `async function f(): T` behaves like `Promise<T>`. Async does not mean a new operating-system thread is created for every call. It means the result and continuation are scheduled through the runtime. See [Async and concurrency](../../internal-engineering/07-async-and-concurrency.md).

## Function errors

The checker validates argument count, argument types, callability, and return types. Common diagnostics are in the `CP34xx` and `CP35xx` ranges. If a generated help page ranks a neighboring page first, use the diagnostic’s exact source excerpt as the primary evidence.
