---
title: Conditions and loops
chapter: Chapter 1 · The language
---
# Conditions and loops

CP3 provides familiar structured control flow. Conditions are checked by the analyzer; loop declarations follow the language’s binding and scope rules.

## `if` and `else`

```ts
if (count > 0) {
  console.log("items available");
} else {
  console.log("empty");
}
```

Use boolean expressions for conditions. The compiler reports a condition that does not satisfy the supported condition type rather than silently converting an arbitrary object to a boolean.

## `while`

```ts
let index = 0;
while (index < items.length) {
  process(items[index]);
  index = index + 1;
}
```

A `while` loop reevaluates its condition before each iteration. Make the progress update visible and ensure the loop can terminate. `break` exits the nearest loop; `continue` begins the next iteration.

## `do...while`

```ts
let attempts = 0;
do {
  attempts = attempts + 1;
} while (!ready());
```

A `do...while` loop executes its body once before checking the condition, then re-evaluates it after each iteration. `break` exits the loop; `continue` skips to the condition check. The condition is checked in a boolean context, like `if`.

## `switch`

```ts
switch (mode) {
  case "safe":
    console.log("safe mode");
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

The initializer, condition, and update run in that order. `let` and `const` loop bindings are scoped to the loop form supported by the compiler. Do not use JavaScript’s `for...in` over ordinary object values: CP3 rejects that dynamic enumeration pattern. Use a typed array or an explicitly supported collection operation.

## `for...of`

`for...of` is supported for the iterable/value shapes implemented by the front end, such as arrays and supported collection iteration. Its behavior is not a blanket guarantee that every JavaScript iterable protocol is available. If the checker rejects a receiver, use the array or collection API documented for its type.

### `for await`

Prefixing the loop with `await` binds the awaited value instead of the raw element: a promise-typed element resolves to its value (`undefined` for `Promise<void>`), anything else is its own value. `for await` is legal only inside an `async` function (or at the top level):

```ts
for await (const line of lines) {
  console.log(line); // line is the element type, with promises resolved
}
```

This is not an async-iterator protocol; it is the ordinary `for...of` shape with the bound value awaited. See [Async and concurrency](../../02-runtime/07-async-and-concurrency.md) for the coroutine semantics.

## Labels and nested loops

CP3 supports the labeled break/continue forms accepted by the parser and semantic checker. A label must identify a valid enclosing iteration target. The analyzer reports a label that is missing or targets a non-iteration statement.

## Boolean operators and short-circuiting

`&&` and `||` combine boolean expressions with short-circuit evaluation. The right side may not execute when the left side determines the result. `!` negates a boolean expression. `??` selects a fallback for nullish (`null` or `undefined`) values and has different behavior from `||`.

## Return and unreachable paths

`return` exits the current function. A non-void function must provide a value on every required path. CP3 checks for returns outside a function, missing return paths, and unreachable statements.

## Compiler optimization note

The compiler recognizes a narrow set of proven loop patterns, including specific typed-array and DataView loops. That does not change CP3 control-flow semantics. If a pattern is uncertain, code generation keeps the checked runtime operation. The [optimization pipeline](../../04-compiler-cli/03-optimization-pipeline.md) lists the actual proofs; do not rely on a source loop being vectorized without inspecting compiler output and a backend report.
