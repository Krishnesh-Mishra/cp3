---
title: Conditions and loops
---
# Conditions and loops

CP3 provides familiar structured control flow. Conditions are checked by the analyzer; loop declarations follow the language's binding and scope rules.

## `if` and `else`

```ts
import console from "std/console";

if (count > 0) {
  console.log("items available");
} else {
  console.log("empty");
}
```

Conditions support truthy semantics with JavaScript-like rules: `0`, `-0`, `NaN`, `""`, `null`, and `undefined` are falsy; objects and non-zero numbers are truthy. Truthy/falsy conditions are also narrowing operations: `if (x)` narrows `x` to its truthy subset in the true branch, and `if (!x)` narrows it in the false branch.

## Narrowing in branches

A union may be used more precisely after a supported guard establishes which alternative is present. The supported guards include:

- `typeof x === "number"` / `"string"` (and the `!=` forms), including `typeof x === "object"` (which narrows to the object-like members of the union, `null` included);
- `x === undefined` / `x !== undefined` / `x === null` / `x !== null` (and the loose `==` forms, which match only the same flavor);
- `if (x)` / `if (!x)` for nullish unions;
- assignment narrowing: after `v = 4`, a later read of `v` is `number`;
- member, `this.x`, and nested-chain receivers: `if (node.next !== null) { node.next.value }`;
- `bus.isValidEventName(name)`, which narrows a `string` name to the Events map's declared event keys (see [Global builtins](../99-global-builtins.md)).

```ts
function firstPresent(box: { a: number } | undefined): number {
  if (box === undefined) {
    return -1;
  }
  return box.a; // box is { a: number } here
}
```

The two arms of an `if` are merged the way TypeScript merges them: after the `if`, the compiler sees the union of what the arms concluded; an arm that returns, throws, breaks, or continues never reaches the merge point. CP3 does not implement literal-tag discriminated-union narrowing (there is no implicit partition of a union of object shapes on `x.kind === "a"`).

## `while`

```ts
import console from "std/console";

let index = 0;
while (index < items.length) {
  process(items[index]);
  index = index + 1;
}
```

A `while` loop reevaluates its condition before each iteration. Make the progress update visible and ensure the loop can terminate. `break` exits the nearest loop; `continue` begins the next iteration. A `while` condition is a narrowing site: `while (node !== undefined)` narrows `node` inside the loop body.

## `do...while`

```ts
let attempts = 0;
do {
  attempts = attempts + 1;
} while (!ready());
```

A `do...while` loop executes its body once before checking the condition, then re-evaluates it after each iteration. `break` exits the loop; `continue` skips to the condition check. The condition is checked in a truthy context, like `if`.

## `switch`

```ts
switch (mode) {
  case "safe":
    mode = "fast";
    break;
  case "fast":
    console.log("fast mode");
    break;
  default:
    console.log("unknown mode");
}
```

The discriminant is evaluated once; each `case` expression is checked against it. A clause with no statements falls through into the next clause (JavaScript fall-through). Use `break` to stop at the end of a clause. Each clause runs in its own lexical scope, so a `let` declared in one clause is not visible in another. A `switch` with a `default` whose remaining clauses all terminate every path counts as a terminating statement for return-path checking.

## Counted `for`

```ts
for (let index = 0; index < 10; index = index + 1) {
  console.log(index);
}
```

The initializer, condition, and update run in that order. `let` loop bindings are scoped to the loop, and a fresh one is created per iteration — a closure created inside the loop body captures that iteration's binding. Do not enumerate an ordinary object with JavaScript's `for...in` over plain objects: CP3 rejects that pattern (`CP3805`). Use a typed array, a `Map`, or a supported collection operation instead.

## `for...of`

`for...of` is supported for the iterable shapes implemented by the front end, such as arrays and supported collection iteration. Its behavior is not a blanket guarantee that every JavaScript iterable protocol is available. Arrays, `Map`s, `Set`s, and typed arrays are the ordinary cases; if the checker rejects a receiver, use the array or collection API documented for its type. A narrowed binding (see above) iterates correctly at sites such as `for (const x of narrowed) { x.length }`.

### `for await`

Prefixing the loop with `await` binds the awaited value instead of the raw element: a promise-typed element resolves to its value (a promise of nothing resolves to `undefined`), anything else is its own value. `for await` is legal only inside an `async` function or at the top level:

```ts
async function main(): Promise<undefined> {
  const lines: Promise<string>[] = [fetchLine()];
  for await (const line of lines) {
    console.log(line); // line is a string, with promises resolved
  }
}
```

This is not an async-iterator protocol; it is the ordinary `for...of` shape with the bound value awaited.

## `for...in` over collections

`for...in` walks the runtime key set of a collection, not the static shape of an object:

- an array yields its indices as `number`;
- a `Map` yields its keys (in insertion order);
- a `Set` yields its insertion indices as `number`;
- a plain object or a class instance is rejected (`CP3805`) because its key set is known at compile time — access fields by name, or use `Map<K, V>` when the keys are dynamic.

```ts
for (const index in [10, 20, 30]) {
  console.log(index); // 0, 1, 2 — each is a number
}
```

## Labels and nested loops

CP3 supports the labeled `break`/`continue` forms accepted by the parser and semantic checker:

```ts
outer:
for (let i = 0; i < 3; i++) {
  for (let j = 0; j < 3; j++) {
    if (i === 1 && j === 1) break outer;
  }
}
```

A label must identify a valid enclosing statement. A label on a `block` may only be broken out of — `continue` to a block label is rejected with `CP3508` ("does not denote an iteration statement") — and an unresolved label is `CP3306`. A `break` or `continue` outside a loop is `CP3502`/`CP3503`.

## Boolean operators and short-circuiting

`&&` and `||` combine expressions with short-circuit evaluation. The right side may not execute when the left side determines the result. `!` negates a truthy/falsy expression. `??` selects a fallback for nullish (`null` or `undefined`) values and has different behavior from `||`; chains such as `a ?? b ?? 42` are accepted.

Loose equality has a CP3-specific rule: `==` against a `null`/`undefined` literal matches only the same flavor (`null == undefined` is `false`, unlike JS), and the compiler flags the pattern with a warning (`CP3520`). Use `===` for a literal check.

## Return and unreachable paths

`return` exits the current function. A non-`undefined`-returning function must provide a value on every required path; the analyzer reports returns outside a function (`CP3501`) and missing return paths (`CP3504`). A `return` inside `finally` does not override the pending completion — the value is discarded and the compiler flags the pattern with a warning (`CP3510`). See [Errors and exceptions](../06-modules-and-errors/02-errors.md).

## Related pages

- [Bindings and declarations](../01-variables-and-bindings/01-bindings.md)
- [Functions and closures](../02-functions/01-functions-and-closures.md)
- [Global builtins](../99-global-builtins.md)
