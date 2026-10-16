# 2. Syntax

~99% JS/TS-like, but statically typed and without keyword `any`.

```cp3
let name: string = "John";
var age: number = 18;

if (age >= 18) {
  console.log("Adult");
}
```

> `console` needs to be imported from the `std`library its not by default part of the language.

Type inference is supported:

```cp3
let age = 18;      // inferred: number
let name = "John";  // inferred: string
```

Type errors are compile-time:

```cp3
let age: number = 18;
age = "hello"; // compile error
```

## Comments & literals

Comments and string literals work exactly like JS:

```cp3
// single-line comment
/* multi-line
   comment */

let a: string = "double quotes";
let b: string = 'single quotes';
let c: string = `template literal with ${a} interpolation`;
```

Numeric literal formats also match JS/C++ conventions: hex (`0x1F`), binary (`0b101`), and underscore digit separators (`1_000_000`).

