# Day 1 - the language

**Goal for today:** read any `.cp3` file without surprises and write a small program without looking much up. You are *not* learning internals today - no memory model, no compiler pipeline.

## Hour 0 - get a project running

```bash
cp3 init            # prompts: type → name → description → type-system; creates src/, package.json, index.cp3
cd my-app
cp3 run src/index.cp3   # transpile → compile → execute
```

That's the whole loop to start with. The CLI you'll use today is just three commands:

| Command | What it does | TypeScript equivalent |
| --- | --- | --- |
| `cp3 run <file>` | Build and execute | `ts-node file.ts` |
| `cp3 check` | Type-check only, no output | `tsc --noEmit` |
| `cp3 dev` | Watch mode, rebuild on change | `tsc --watch` + runner |

`cp3 init` also generates npm scripts, so `npm run dev`, `npm start`, and `npm run check` work like you'd expect. The whole toolchain section lives in [Day 2](02-day-2-stdlib-and-cli.md) and the full CLI reference is in [CLI §1](../cli-tooling/01-compilation-pipeline.md).

> **Why you'd read this later:** [`CLI-README §1` (compilation pipeline)](../cli-tooling/01-compilation-pipeline.md) explains *how* `.cp3` becomes a native binary. Useful for build/CI questions - skippable while you're learning the language.

## The 60-second mental model

cp3 is designed to hit **TypeScript's familiarity + native performance + automatic memory management**. Concretely:

- The syntax is ~99% JS/TS. `let`, `const`, `if`, `for`, `class`, `async`/`await` all mean what you think.
- Types are TypeScript's by default (`number`, `string`, `boolean`) - configurable per project, but you don't need to touch that.
- There is **no garbage collector and no manual memory management**. You will never write `new`/`delete`, `malloc`/`free`, or borrow-checker annotations. That's the part you're allowed to not think about.
- The compiler is strict like `tsc` but stricter in a few places. Compile errors, not runtime surprises.

## What is exactly the same as TypeScript

Skim this - don't memorize it, just note how little is different.

- **Bindings:** `let` and `var` are mutable; `const` is immutable. Block-scoped, exactly like real JS. Primitives copy, objects/arrays are by reference.
- **Primitives:** `number`, `string`, `boolean`. `number` is always a 64-bit float (like JS). Type inference works: `let age = 18;` is a `number`.
- **Functions:** `function` declarations, arrow functions, default parameters, rest parameters (`...args`), destructuring, function overloading by parameter type.
- **Classes:** `public`/`private`/`protected`, `static` members, `extends` (single inheritance), abstract classes, `this`, getters/setters. Interfaces are **structural** - you don't write `implements`, if the shape matches, it matches.
- **Control flow:** `if`, `switch` (with JS fall-through), `for` / `for...of` / `for...in`, `while` / `do...while`, `break`/`continue`, labels.
- **Operators:** `==` vs `===`, `?.`, `??`, ternary, bitwise, spread (`...`), truthy/falsy coercion (yes - `""` and `0` are still falsy).
- **Errors:** `try` / `catch` / `finally`, `throw`. No separate exception hierarchy - same as JS.
- **Async:** `async` / `await`, `Promise` with `.then`/`.catch`/`.finally`, `Promise.all`/`race`/`allSettled`. Same event-loop model as JS.
- **Enums:** number-backed and string-backed, like TS.
- **Generics:** `<T>` on classes and functions, with `<T extends Shape>` constraints.
- **Modules:** `import` / `export`, default and named imports, ES-module style.
- **Arrays & strings:** the JS methods you use - `push`, `pop`, `map`, `filter`, `reduce`, `sort`, `find`, `includes`, `flat`; `split`, `trim`, `replace`, `padStart`, `toUpperCase`, and template literals.
- **Regex:** native literals `/pattern/flags`, like JS.

For all of the above, [§2 Syntax](../language-spec/02-syntax.md) is a ~40-line read if you want the canonical list.

## What changes - the things that won't compile

This is the part worth your full attention. Five real differences. Everything else is additive.

### 1. There is no `any`

`any` does not exist. Use `unknown` when a value's type genuinely isn't known, then **narrow** it before use.

```ts
// TypeScript
function handle(x: any) { return x.foo.bar; }
```

```cp3
// cp3
function handle(x: unknown) {
  if (isUser(x)) {        // narrow first
    return x.name;
  }
}
```

> **Why you'd read this later:** [§3 Type system](../language-spec/03-type-system.md) covers `unknown`, narrowing, unions, and `null` vs `undefined`.

### 2. Plain objects have a fixed shape (this is the big one)

An object literal compiles to a real struct. That means three common JS/TS patterns are **compile errors**:

```ts
// TypeScript - all fine
const user = { name: "Ada" };
user.age = 36;              // ✗ adding a property later
delete user.name;           // ✗ deleting a property
const dict = {};
dict["key"] = 1;            // ✗ dictionary built by index
```

```cp3
// cp3
class User { name: string; age: number; }   // declare the shape up front
let user = new User(); user.name = "Ada"; user.age = 36;

// For a dynamic lookup table, use Map - not an object
import { Map } from "std/collections";
let dict = new Map<string, number>();
dict.set("key", 1);
```

Also banned: `arguments` (use rest params `...args`), and `for...in` over a plain object (use `Map`/`Set`, or `Object.keys`/`values`/`entries`, which are supported as compile-time macros).

> **Why you'd read this later:** [§8 Variable bindings](../language-spec/08-variable-bindings.md) is the authoritative list of the banned JS patterns and their replacements. This is the single most useful spec page for a TS developer - read it tonight.

### 3. Arrays are homogeneous

A `number[]` holds only numbers; `push("x")` is a compile error. If you genuinely need mixed types you must *say so* with a union:

```cp3
let numbers: number[] = [1, 2, 3];
numbers.push("hello");            // compile error

let mixed: (number | string)[] = [1, "two", 3];   // allowed, explicitly typed
```

Two more array/string differences worth knowing:

- `sort()` is **type-aware and stable** - sorting a `number[]` sorts numerically (not JS's string coercion), and equal elements keep their order.
- Strings are **UTF-8**, not UTF-16. `str[i]` returns a *byte*; use `charAt(i)` to get a *character*. (If your code is plain ASCII, you'll never notice.)

> **Why you'd read this later:** [§9 Arrays & strings](../language-spec/09-arrays-strings.md) for the exact rules and the full method list.

### 4. Mixed-type `+` is a compile error

JS's auto-coercing `+` is gone. Adding a `number` and a `string` won't compile - convert explicitly:

```ts
`total: ${count + ""}`   // TS/JS: silent coercion
```

```cp3
`total: ${String(count)}`   // cp3: explicit conversion
```

Comparisons and storage across types are still fine; only arithmetic mixing is refused.

> **Why you'd read this later:** [§5 Operators & expressions](../language-spec/05-operators-expressions.md) - short, and it also confirms `?.`/`??`/ternary/bitwise are unchanged.

### 5. No generators, no decorators (in this version)

`function*` / `yield` and decorators are out of v1. Everything else in your TS toolbox is present.

> **Why you'd read this later:** [§16 Generics](../language-spec/16-generics.md) states this and covers `<T>`, including why cp3's generics are real and unconstrained-by-package-limits.

## What's genuinely new (worth knowing on day one)

- **`unknown`** replaces `any`, as above.
- **Explicit unions** `(number | string)` are the way you model "one of a few types" - and they're backed by a real sum type, so `typeof` narrowing is reliable.
- **`ref` and `weak`** keywords exist for advanced reference handling. You will *see* them in code but **you do not need them to start** - skip [§10 Memory management](../language-spec/10-memory-management.md), [§11 `weak`](../language-spec/11-reference-cycles-the-weak-keyword.md), and [§12 Pointers & references](../language-spec/12-pointers-references.md) on day one.
- **A configurable type vocabulary.** Projects can choose `"typeSystem": "typescript" | "rust" | "cpp"` in `package.json`. Default is TypeScript, so you can ignore this until you meet a project that uses it. [§3 Type system](../language-spec/03-type-system.md) explains the prefix syntax.

## Tonight's reading path - and *why* each page earns its place

You don't need to read the spec top to bottom. Read these, in this order, and stop when each one stops teaching you something new:

| Read | Because |
| --- | --- |
| [§8 Variable bindings](../language-spec/08-variable-bindings.md) | The banned JS patterns (dynamic props, `delete`, index signatures, `arguments`, `for...in`) and what replaces them. **Read this tonight.** |
| [§3 Type system](../language-spec/03-type-system.md) | No `any`; `unknown` + narrowing; unions; `null` vs `undefined`; the configurable type vocabulary. |
| [§9 Arrays & strings](../language-spec/09-arrays-strings.md) | Homogeneous arrays, type-aware `sort()`, UTF-8 strings. |
| [§15 Classes](../language-spec/15-classes.md) | Structural interfaces, single inheritance, abstract classes - mostly confirms what you expect. |
| [§14 Async](../language-spec/14-async.md) | Confirms `async`/`await` is the JS model. One gotcha: an **unhandled rejection exits the process**. |
| [§2 Syntax](../language-spec/02-syntax.md) | The canonical 40-line syntax summary. Skim it as a checklist. |

Deliberately **skip** today: §10 (memory), §11 (`weak`), §12 (pointers), §13 (concurrency), §19 (C/C++ interop). None are required to write normal cp3, and each is a rabbit hole.

## Checkpoint: you're done with Day 1 when…

- You can explain why `user.newProp = 1` fails and what to use instead.
- You reach for `Map` (not `{}`) when you need a dictionary.
- You know `let`/`var` are mutable, `const` is not, and there's no `any`.
- You can read a `.cp3` class and say what it compiles to conceptually, without knowing *how*.

Tomorrow is the fun half: the standard library and the CLI you'll actually type.

**Next:** [Day 2 - the std library & the CLI →](02-day-2-stdlib-and-cli.md)
