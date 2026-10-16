# 3. Type system

**Default is TypeScript's type set** — `number`, `string`, `boolean`, etc. — same as every example elsewhere in this doc. This is deliberately the baseline: it's the type vocabulary the target JS/TS developer already knows.

## Overriding the default per project

A project can pick a different type-name vocabulary entirely, set once in `package.json`:

```json
{
  "typeSystem": "typescript"   // default. Also: "rust", "cpp"
}
```

- `"typescript"` (default) — plain type names are TS-style: `number`, `string`, `boolean`.
- `"rust"` — plain type names become Rust's directly, no prefix needed: `let a: u32 = 5;`, `let b: i64 = 10;`.
- `"cpp"` — plain type names become C++'s directly: `let a: long long = 5;`.

`cp3 init` asks for this as a fourth setup question (type → name → description → **type-system pattern**), alongside the existing three.

> It is recommended to use rust types as c++ types are very old age and typescript types are unoptimised.

## Reaching a non-default system: prefix syntax

Whichever system *isn't* the project's active default is still reachable inline, via a prefix on a quoted type name:

```cp3
let a: r_'u32' = 5;         // a Rust type, used inside a TypeScript-default project
let b: c_'long long' = 10;  // a C++ type, used the same way
let c: ts_'number' = 3;     // reaching back to TypeScript types, if the project default is rust/cpp
```

**Restriction:** these prefixed type names only ever accept plain value types — never a pointer or reference form (`int*`, `int&`, etc.).

## Numeric literal syntax stays cp3's own, always

Regardless of active `typeSystem`, number literals are always written cp3's own plain way — `5`, `10`, `3.14` — never with a foreign suffix like Rust's `5u32`. Only the *type name* is ever borrowed from another system; the literal syntax itself never changes.

## Mixing different integer types

Following **C++ rules by default**: adding/subtracting/etc. two different integer types (different width and/or signedness) implicitly widens and performs the operation — no compile error, no forced cast, same convenience C++ code already relies on.

```cp3
let a: r_'u32' = 5;
let b: c_'long long' = 10;
let c = a + b;   // implicitly widened, no cast required
```

Rust-style strict checking (refusing to mix integer types without an explicit cast) is available as an **opt-in compiler flag**, not the default:

```json
{
  "strictNumericCasts": true   // opt-in — off by default
}
```

## Optional / nullable types & unions

Both `undefined` and `null` are supported as separate concepts, TS-style — matching TS exactly rather than collapsing them into one:

```cp3
let a: number | undefined;    // "not set yet"
let b: number | null;         // "deliberately set to nothing"
function greet(name?: string) { }
```

Regardless of the active `typeSystem`, the keywords `undefined` and `null` themselves never change — only type *names* are ever swapped by the `typeSystem`/prefix mechanism, not core language keywords:

```cp3
// typeSystem: "rust" — still 'undefined', never Rust's 'None'
let a: r_'u32' | undefined = undefined;

// typeSystem: "cpp" — still 'null', never C++'s 'nullptr'
let b: c_'long long' | null = null;
```

**Under the hood:** a union type (`number | string`, `T | undefined`, etc.) compiles to `std::variant`. `typeof` on a variant-backed value maps to `std::variant`'s `holds_alternative`/`index()` check (wrapped so it returns a proper name like `"number"`, not a raw index). `typeof` on an ordinary single-type variable — no union involved — is resolved entirely at compile time, no runtime check at all.

**Mixed-type rules:** values of different types (including different branches of a union) can always be **compared** (`==`)  , for the `==` to give outcome `true` the value should be same, there is also `===` like TS for exact type+value matching . **Stored** (assigned into a union-typed variable) freely. They can **not** be directly **operated on** (`+`, `-`, etc.) without an explicit conversion first — `"1" + 1` is always a compile error, whether the mismatch is between two fixed types or two branches of a runtime union; convert explicitly (`a + b.toString()`) before operating. This removes JS's `+` ambiguity (`"1" + 1` vs `1 + 1` producing different kinds of results) while still keeping comparison and storage flexible.

