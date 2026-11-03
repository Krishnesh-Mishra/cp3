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

## Counted `for`

```ts
for (let index = 0; index < 10; index = index + 1) {
  console.log(index);
}
```

The initializer, condition, and update run in that order. `let` and `const` loop bindings are scoped to the loop form supported by the compiler. Do not use JavaScript’s `for...in` over ordinary object values: CP3 rejects that dynamic enumeration pattern. Use a typed array or an explicitly supported collection operation.

## `for...of`

`for...of` is supported for the iterable/value shapes implemented by the front end, such as arrays and supported collection iteration. Its behavior is not a blanket guarantee that every JavaScript iterable protocol is available. If the checker rejects a receiver, use the array or collection API documented for its type.

## Labels and nested loops

CP3 supports the labeled break/continue forms accepted by the parser and semantic checker. A label must identify a valid enclosing iteration target. The analyzer reports a label that is missing or targets a non-iteration statement.

## Boolean operators and short-circuiting

`&&` and `||` combine boolean expressions with short-circuit evaluation. The right side may not execute when the left side determines the result. `!` negates a boolean expression. `??` selects a fallback for nullish (`null` or `undefined`) values and has different behavior from `||`.

## Return and unreachable paths

`return` exits the current function. A non-void function must provide a value on every required path. CP3 checks for returns outside a function, missing return paths, and unreachable statements.

## Compiler optimization note

The compiler recognizes a narrow set of proven loop patterns, including specific typed-array and DataView loops. That does not change CP3 control-flow semantics. If a pattern is uncertain, code generation keeps the checked runtime operation. The [optimization pipeline](../../04-compiler-cli/03-optimization-pipeline.md) lists the actual proofs; do not rely on a source loop being vectorized without inspecting compiler output and a backend report.
