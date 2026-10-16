# 6. Control flow

## `switch`

Exact JS behavior — fall-through by default, `break` required to stop it:

```cp3
switch (x) {
  case 1:
    console.log("one");
    // no break — falls through into case 2
  case 2:
    console.log("two");
    break;
}
```

## `for` loop variants

All three JS forms are kept, unchanged:

```cp3
for (let i = 0; i < 10; i++) { }   // classic counting loop
for (let item of array) { }         // loop over values
for (let key in object) { }         // loop over keys/indices
```

## `while` and `do...while`

Both kept, mapping directly to C++'s own `while`/`do...while` — no new engineering, same as the `for` variants above:

```cp3
while (condition) { }          // standard — checks before each iteration

do {
  // ...
} while (condition);            // runs the body once first, then checks
```

## Custom iterables — `for...of` on your own classes

A user-defined class can make itself work with `for...of` by exposing `begin()`/`end()` methods, matching C++'s own range-based `for` mechanism directly — not a custom bolt-on feature:

```cp3
class LinkedList<T> {
  function begin(): Iterator<T> { /* ... */ }
  function end(): Iterator<T> { /* ... */ }
}

let list = new LinkedList<number>();
for (let item of list) { }   // works — compiles to real C++ range-based for underneath
```

This maps directly to C++'s existing, mature `begin()`/`end()` + iterator convention (the same mechanism every STL container already uses) — no new engineering required, just exposing what C++ already provides through a cp3-friendly method pair.

## Destructuring

Full JS/TS destructuring feature set — array and object, renaming, default values, rest pattern, and destructuring in both variable declarations and function parameters:

```cp3
let [a, b] = [1, 2];
let { name, age }: { name: string, age: number } = person;
let { name: n, age: a = 18 } = person;   // rename + default
let [first, ...rest] = [1, 2, 3, 4];      // rest pattern
function greet({ name, age }: Person) { }  // destructuring in parameters
```

## Spread / rest

Both kept, JS-style syntax, mapped internally to vector operations:

```cp3
let a = [1, 2, 3];
let b = [...a, 4, 5];              // spread
let obj2 = { ...obj1, extra: 1 };
function sum(...nums: number[]) { }  // rest
myFunc(...args);                     // spread into call
```

## Arrays stay dense — no true "holes"

Unlike JS's sparse arrays (a genuine missing slot, not even `undefined`), cp3 arrays are always dense: any unfilled slot is explicitly `null`/`undefined` (a real stored value, `T | null` / `T | undefined`), and `.length` always matches the actual number of stored slots. If person directly creates a array with million objects where middle ones he didnt defined then they will be filled with `null`.

