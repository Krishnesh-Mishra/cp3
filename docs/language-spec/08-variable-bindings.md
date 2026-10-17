# 8. Variable bindings

| Keyword | Mutability        | Semantics                                         |
| ------- | ----------------- | ------------------------------------------------- |
| `let`   | Mutable binding   | Can be reassigned - block-scoped, like real JS    |
| `var`   | Mutable binding   | Can be reassigned - same reassignability as `let` |
| `const` | Immutable binding | Cannot be reassigned - the only immutable one     |

**Corrected from an earlier draft:** this spec previously had `let` and `const` both immutable, with `var` as the sole mutable keyword - that was backwards relative to real JavaScript, where `let` is explicitly the mutable, block-scoped alternative to `var`, and `const` is the only immutable binding. This has been fixed to match real JS exactly, since a JS developer's very first `let x = 5; x = 10;` needs to just work, matching the "syntax stays ~99% familiar" goal stated from the start of this project.

Plain `let` / `var` / `const` behave **exactly like JavaScript**, nothing new:

```cp3
let x = 10; x = 20;   // fine - let is reassignable
var y = 20; y = 30;   // fine - var is reassignable too
const z = 5; z = 10;  // compile error - const cannot be reassigned

let a = { info: 1 };
let b = a;           // b is a reference to the SAME object, like JS
b.info = 2;          // a.info is now also 2 - this is expected, JS-style
```

Primitives (`number`, `boolean`, `string`, etc.) copy by value, exactly as in JS. Objects and arrays share by reference, exactly as in JS - no hidden behavior. This keeps plain bindings dead simple and free of surprises for anyone coming from JS/TS.

## Object literals with function-valued properties - method shorthand vs. a callable-value field

An object literal can hold a property whose value is a function, but cp3 (unlike JS) distinguishes two shapes, and they compile differently:

```cp3
interface Logger {
  function log(str: string): void;   // method shorthand
}

const a: Logger = { log: (str) => print(str) };   // fixed shape, one concrete implementation
```

```cp3
interface Handler {
  onEvent: (str: string) => void;    // field with a function type
}

let b: Handler = { onEvent: (str) => print(str) };
b.onEvent = (str) => print("[override] " + str);   // reassignable - allowed, because the field says so
```

- **Method shorthand** (`log(str: string): void;`) declares a fixed method. As long as a given interface-typed binding never actually needs to hold more than one concrete implementation at runtime, the compiler compiles the object straight to a real class instance with a real C++ method - a direct call, zero overhead, no boxing, no `std::function` anywhere. This is the default and expected case for stateless, non-swappable things (this is how every `std/*` module in [STDLIB-README.md](../standard-library/00-overview.md) is written, and none of them need to be reassignable).
- **Field with a function type** (`onEvent: (str: string) => void;`) declares an ordinary property whose *value* happens to be callable, and which can be reassigned like any other field. This genuinely needs to be stored as a real callable value, not baked in as a method body - it follows the same closure rules as anywhere else ([§10](10-memory-management.md)): a non-capturing function value compiles to a plain function pointer at effectively zero cost, while a capturing one holds its captures (copies, or shared cells for reassignable `let`/`var`) in a refcounted closure object, same as any other closure.
- **Runtime polymorphism** (the same interface-typed variable or parameter holding genuinely different concrete implementations at different times - e.g. swapping a real logger for a mock) uses neither of the above directly; it reuses the type-erased wrapper the compiler already generates for structural interfaces ([§15](15-classes.md)) - the same mechanism, not a new one.

## JS patterns that don't exist in cp3 - explicit compile errors, not silent gaps

A plain cp3 object compiles to a fixed-shape C++ struct/class ([§15](15-classes.md)) - its exact set of fields is known at compile time, the same way a C++ struct's fields are fixed once declared. A handful of everyday JS patterns rely on objects being genuinely dynamic at runtime, which directly conflicts with that - and rather than let a JS developer discover this by hitting a confusing generated-C++ failure, each one is banned outright with a clear cp3-native compile error pointing at the right alternative:

- **`delete obj.prop` is banned.** A fixed-shape struct has nothing to "delete" - the field exists as long as the object does. For a genuinely removable key, use `Map<K, V>` and `map.delete(key)` ([§20](20-standard-library.md)) instead of a plain object.
- **Adding an undeclared property (`obj.newProp = 1`) is banned.** Every property has to be part of the object's type up front ([§15](15-classes.md)) - there's no equivalent of JS quietly growing an object's shape at runtime. Same fix: use `Map` for anything that's genuinely dynamic.
- **There is no index-signature object type** (TS's `{ [key: string]: number }`) **- this is the direct resolution of "how do plain objects work as dictionaries."** They don't; `Map<string, T>` is the only supported mechanism for a dynamic-keyed dictionary. A plain object's keys are always a fixed, known-at-compile-time set.
- **The `arguments` object is banned.** It's a dynamic, magic array-like value with no static C++ equivalent. Use rest parameters instead (`function sum(...nums: number[])`, already part of the language, [§17](17-functions.md)) - same practical result, fully statically typed.
- **`for...in` on a plain object/class instance is banned.** JS uses it to enumerate an object's keys at runtime, but a cp3 object's keys are already known at compile time - there's nothing to discover. `for...in` (and `for...of` with `.keys()`) stays valid for `Map`/`Set`, where the key set is genuinely runtime-determined, and for direct field access on a plain object, just reference the field by name.

**`Object.keys(obj)`/`.values(obj)`/`.entries(obj)` are supported - and this doesn't reopen the `for...in` ban above, because it isn't runtime reflection.** Since a plain object's field names are fixed at compile time, `Object.keys(obj)` on an object typed `{ a: number, b: string }` expands, at compile time, directly to the literal array `["a", "b"]` - a plain compile-time macro, not a runtime lookup into any kind of live shape/metadata table. There's no dynamic reflection machinery being introduced here; the compiler already knows the answer before it ever generates C++, the same way it already knows everything else about a plain object's shape.

Each of these produces a specific, named compile error rather than either silently doing something unexpected or generating C++ that fails to compile - consistent with the "if a question could arise, the answer should already be in the doc" principle guiding this whole spec, and with [CLI-README.md §2](../cli-tooling/02-error-diagnostics-source-mapping.md)'s promise that valid-looking cp3 never surfaces a raw C++ error at the user.

