# C+++ (`.cp3`)

> A statically-typed, JavaScript/TypeScript-flavored language that compiles to native code via a C++ backend - designed so a competent JS/TS developer can be productive in roughly two days, without learning pointers, manual memory management, or CMake.

## Status

**Beta One.** The v1 language spec is considered locked for implementation - every core semantic decision below is final unless explicitly marked otherwise. Build against this document directly.

A small number of items were marked **"confirm before building this part"** inline where a real design call was made to unblock implementation but hadn't been explicitly signed off by the project lead - everything else is not up for reinterpretation. **As of the beta-one sign-off, none remain:** every previously-flagged item has been decided and recorded in §23's *"Resolved for beta one"* list, so this document is implementation-ready. If something in here seems ambiguous or under-specified in a way that isn't already flagged, stop and ask rather than assuming - do not silently pick a default for anything not already decided in this document.

**This document is the language spec only** - syntax, types, memory model, concurrency, classes, and everything else you write in a `.cp3` file. It has no CLI commands, package format, or compiler-pipeline detail in it. For the `cp3` tool itself (`run`/`check`/`dev`/`fmt`, the package manager, `.cp3pak`, native-library bindgen, diagnostics/crash-reporting internals), see the companion `CLI-README.md`. For exact standard-library function signatures, see `STDLIB-README.md`.

---

## Why C+++?

###### **JS/TS has the largest developer base and the gentlest learning curve of any mainstream language.** Most other languages - C++, Rust, Go, Java - force that developer to relearn how to declare a variable, write a loop, define a class, work with arrays. The concepts are the same everywhere; the syntax isn't. That relearning cost is the real barrier, not the underlying concepts.

**But JS/TS pays a steep runtime tax.** A plain Express server doing trivial request handling can sit at hundreds of MB of memory - not because the task is hard, but because V8 is a full JIT runtime: garbage collector, dynamic typing machinery, JIT warmup, object shapes, etc. all running underneath even the simplest code.

**Go tried to fix some of this and still ships a garbage collector.** GC pauses and GC memory overhead are exactly the category of cost C+++ is trying to avoid, not just relocate.

**Rust and C++ get you real performance with no GC - but at a steep syntax/learning cost.** Ownership/borrow-checking syntax (Rust) and manual memory/pointer syntax (C++) are precisely the "relearn everything" tax that keeps most JS/TS developers away, even though the underlying tasks (an if-statement, a for-loop, pushing to an array) aren't conceptually hard at all.

**The gap C+++ targets:** keep the syntax ~99.9% identical to JS/TS - `let`, `const`, `for`, `if`, `array.push(...)`, classes - so a JS/TS developer's existing muscle memory transfers directly, while compiling to real native code with no garbage collector . The things that are genuinely hard - the internals of Unreal Engine, Qt, OpenCV, or an ML runtime - stay hard and stay in C++. What C+++ targets is everything *around* that: the everyday application/server code that's needlessly expensive today (like the Express example above) purely because it's running on a dynamic runtime rather than being compiled.

---

## 1. Philosophy

C+++ aims to combine:

- **JavaScript/TypeScript-like syntax** - familiar to the largest pool of developers
- **Static typing** - no `any`
- **Native executable output** - real performance, not a runtime/VM
- **Automatic memory management** - no manual `new`/`delete`, no raw pointers for normal users
- **C/C++ interoperability** - access to the existing native ecosystem (Qt, Unreal, OpenCV, etc.)
- **Modern tooling** - built-in package manager, simple modules, no header files

The core bet: most of C++'s complexity (manual memory, pointers, headers, build systems) is *incidental*, not essential to getting native performance. Hide it behind a friendlier language and a smart compiler.

**cp3 is a hosted language, not a freestanding/systems one - worth stating explicitly, since "compiles to native code" invites the question.** Every program cp3 produces assumes a real OS underneath it (libuv needs `epoll`/`kqueue`/IOCP from the kernel; the managed heap needs a working allocator; `import native` needs a real dynamic linker). That's a deliberate scope boundary, not a gap: writing an OS kernel needs the opposite of everything cp3 provides - no runtime, no heap allocator handed to you, no async event loop, raw hardware/interrupt access, and the raw pointers and inline assembly this spec deliberately keeps out of normal cp3 code (§12, §14). None of that is on the beta-one roadmap, and doing it properly would mean a genuinely separate freestanding compilation target, not an extension of this one. cp3's actual target - and where it has a real advantage over the incumbents - is **hosted userspace software**: web servers, CLI tools, desktop apps, daemons, shells, terminal emulators - the exact space Node/Electron/Go/Python occupy today, not the space C/Rust/Zig occupy for kernel work.

## 2. Syntax

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

### Comments & literals

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

## 3. Type system

**Default is TypeScript's type set** - `number`, `string`, `boolean`, etc. - same as every example elsewhere in this doc. This is deliberately the baseline: it's the type vocabulary the target JS/TS developer already knows.

### Overriding the default per project

A project can pick a different type-name vocabulary entirely, set once in `package.json`:

```json
{
  "typeSystem": "typescript"   // default. Also: "rust", "cpp"
}
```

- `"typescript"` (default) - plain type names are TS-style: `number`, `string`, `boolean`.
- `"rust"` - plain type names become Rust's directly, no prefix needed: `let a: u32 = 5;`, `let b: i64 = 10;`.
- `"cpp"` - plain type names become C++'s directly: `let a: long long = 5;`.

`cp3 init` asks for this as a fourth setup question (type → name → description → **type-system pattern**), alongside the existing three.

> It is recommended to use rust types as c++ types are very old age and typescript types are unoptimised.

### Reaching a non-default system: prefix syntax

Whichever system *isn't* the project's active default is still reachable inline, via a prefix on a quoted type name:

```cp3
let a: r_'u32' = 5;         // a Rust type, used inside a TypeScript-default project
let b: c_'long long' = 10;  // a C++ type, used the same way
let c: ts_'number' = 3;     // reaching back to TypeScript types, if the project default is rust/cpp
```

**Restriction:** these prefixed type names only ever accept plain value types - never a pointer or reference form (`int*`, `int&`, etc.).

### Numeric literal syntax stays cp3's own, always

Regardless of active `typeSystem`, number literals are always written cp3's own plain way - `5`, `10`, `3.14` - never with a foreign suffix like Rust's `5u32`. Only the *type name* is ever borrowed from another system; the literal syntax itself never changes.

### Mixing different integer types

Following **C++ rules by default**: adding/subtracting/etc. two different integer types (different width and/or signedness) implicitly widens and performs the operation - no compile error, no forced cast, same convenience C++ code already relies on.

```cp3
let a: r_'u32' = 5;
let b: c_'long long' = 10;
let c = a + b;   // implicitly widened, no cast required
```

Rust-style strict checking (refusing to mix integer types without an explicit cast) is available as an **opt-in compiler flag**, not the default:

```json
{
  "strictNumericCasts": true   // opt-in - off by default
}
```

### Optional / nullable types & unions

Both `undefined` and `null` are supported as separate concepts, TS-style - matching TS exactly rather than collapsing them into one:

```cp3
let a: number | undefined;    // "not set yet"
let b: number | null;         // "deliberately set to nothing"
function greet(name?: string) { }
```

Regardless of the active `typeSystem`, the keywords `undefined` and `null` themselves never change - only type *names* are ever swapped by the `typeSystem`/prefix mechanism, not core language keywords:

```cp3
// typeSystem: "rust" - still 'undefined', never Rust's 'None'
let a: r_'u32' | undefined = undefined;

// typeSystem: "cpp" - still 'null', never C++'s 'nullptr'
let b: c_'long long' | null = null;
```

**Under the hood:** a union type (`number | string`, `T | undefined`, etc.) compiles to `std::variant`. `typeof` on a variant-backed value maps to `std::variant`'s `holds_alternative`/`index()` check (wrapped so it returns a proper name like `"number"`, not a raw index). `typeof` on an ordinary single-type variable - no union involved - is resolved entirely at compile time, no runtime check at all.

**Mixed-type rules:** values of different types (including different branches of a union) can always be **compared** (`==`)  , for the `==` to give outcome `true` the value should be same, there is also `===` like TS for exact type+value matching . **Stored** (assigned into a union-typed variable) freely. They can **not** be directly **operated on** (`+`, `-`, etc.) without an explicit conversion first - `"1" + 1` is always a compile error, whether the mismatch is between two fixed types or two branches of a runtime union; convert explicitly (`a + b.toString()`) before operating. This removes JS's `+` ambiguity (`"1" + 1` vs `1 + 1` producing different kinds of results) while still keeping comparison and storage flexible.

## 4. Enums

```cp3
enum Color { Red, Green, Blue }
let c: Color = Color.Green;

enum Status { Active = "active", Inactive = "inactive" }  // string-backed
```

Both plain (number-backed) and string-backed enums, matching TS. Maps directly to C++'s native `enum class`.

## 5. Operators & expressions

Most operators match JS/TS exactly, with static typing removing the ambiguity JS has around them:

- *`==/===`   with static typing, a mismatched-type comparison between two *fixed* types wouldn't compile in the first place - but since union types are backed by `std::variant` (§3), a variable's actual runtime-held type can still genuinely vary, so the loose (`==`, coercing) vs. strict (`===`, exact type match) distinction stays meaningful.
- **`+` on mixed types** - not allowed directly (see §3's mixed-type rules); requires an explicit conversion first, unlike JS's auto-coercing `+`.
- **Truthy/falsy in `if`** - full JS coercion behavior kept, for every type: `0`, `""`, `null`, `undefined`, `NaN` are falsy; non-empty strings, empty arrays/objects, etc. are truthy - exactly as in JS, not restricted to real `boolean` expressions only.
- **`?.` (optional chaining) / `??` (nullish coalescing)** - kept exactly as JS/TS has them.
- **Ternary (`? :`), bitwise operators, default parameter values** - identical to JS/TS, direct mapping, no changes.

## 6. Control flow

### `switch`

Exact JS behavior - fall-through by default, `break` required to stop it:

```cp3
switch (x) {
  case 1:
    console.log("one");
    // no break - falls through into case 2
  case 2:
    console.log("two");
    break;
}
```

### `for` loop variants

All three JS forms are kept, unchanged:

```cp3
for (let i = 0; i < 10; i++) { }   // classic counting loop
for (let item of array) { }         // loop over values
for (let key in object) { }         // loop over keys/indices
```

### `while` and `do...while`

Both kept, mapping directly to C++'s own `while`/`do...while` - no new engineering, same as the `for` variants above:

```cp3
while (condition) { }          // standard - checks before each iteration

do {
  // ...
} while (condition);            // runs the body once first, then checks
```

### Custom iterables - `for...of` on your own classes

A user-defined class can make itself work with `for...of` by exposing `begin()`/`end()` methods, matching C++'s own range-based `for` mechanism directly - not a custom bolt-on feature:

```cp3
class LinkedList<T> {
  function begin(): Iterator<T> { /* ... */ }
  function end(): Iterator<T> { /* ... */ }
}

let list = new LinkedList<number>();
for (let item of list) { }   // works - compiles to real C++ range-based for underneath
```

This maps directly to C++'s existing, mature `begin()`/`end()` + iterator convention (the same mechanism every STL container already uses) - no new engineering required, just exposing what C++ already provides through a cp3-friendly method pair.

### Destructuring

Full JS/TS destructuring feature set - array and object, renaming, default values, rest pattern, and destructuring in both variable declarations and function parameters:

```cp3
let [a, b] = [1, 2];
let { name, age }: { name: string, age: number } = person;
let { name: n, age: a = 18 } = person;   // rename + default
let [first, ...rest] = [1, 2, 3, 4];      // rest pattern
function greet({ name, age }: Person) { }  // destructuring in parameters
```

### Spread / rest

Both kept, JS-style syntax, mapped internally to vector operations:

```cp3
let a = [1, 2, 3];
let b = [...a, 4, 5];              // spread
let obj2 = { ...obj1, extra: 1 };
function sum(...nums: number[]) { }  // rest
myFunc(...args);                     // spread into call
```

### Arrays stay dense - no true "holes"

Unlike JS's sparse arrays (a genuine missing slot, not even `undefined`), cp3 arrays are always dense: any unfilled slot is explicitly `null`/`undefined` (a real stored value, `T | null` / `T | undefined`), and `.length` always matches the actual number of stored slots. If person directly creates a array with million objects where middle ones he didnt defined then they will be filled with `null`.

## 7. Error handling

Same model as JavaScript - `try` / `catch` / `finally`:

```cp3
try {
  riskyOperation();
} catch (e) {
  console.log("Failed:", e);
} finally {
  cleanup();   // runs whether the try succeeded or threw - always
}
```

No separate exception-type hierarchy beyond what this already gives - `throw` raises, the nearest enclosing `catch` handles it, same as JS.

**`finally` compiles via a generated C++ scope guard, not a native language feature** - C++ has no `finally` keyword, but its destructors already run unconditionally on scope exit (normal return or exception unwinding alike). The compiler generates a small internal struct whose destructor runs the `finally` block's code, and constructs one instance of it right where the `finally` block sits - the same RAII mechanism §10 already relies on for memory, applied here for arbitrary cleanup code instead of just destruction. This is why `finally` reliably runs even when the `try` block returns early or throws past the `catch`.

## 8. Variable bindings

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

### Object literals with function-valued properties - method shorthand vs. a callable-value field

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

- **Method shorthand** (`log(str: string): void;`) declares a fixed method. As long as a given interface-typed binding never actually needs to hold more than one concrete implementation at runtime, the compiler compiles the object straight to a real class instance with a real C++ method - a direct call, zero overhead, no boxing, no `std::function` anywhere. This is the default and expected case for stateless, non-swappable things (this is how every `std/*` module in `STDLIB-README.md` is written, and none of them need to be reassignable).
- **Field with a function type** (`onEvent: (str: string) => void;`) declares an ordinary property whose *value* happens to be callable, and which can be reassigned like any other field. This genuinely needs to be stored as a real callable value, not baked in as a method body - it follows the same closure rules as anywhere else (§10): a non-capturing function value compiles to a plain function pointer at effectively zero cost, while a capturing one holds its captures (copies, or shared cells for reassignable `let`/`var`) in a refcounted closure object, same as any other closure.
- **Runtime polymorphism** (the same interface-typed variable or parameter holding genuinely different concrete implementations at different times - e.g. swapping a real logger for a mock) uses neither of the above directly; it reuses the type-erased wrapper the compiler already generates for structural interfaces (§15) - the same mechanism, not a new one.

### JS patterns that don't exist in cp3 - explicit compile errors, not silent gaps

A plain cp3 object compiles to a fixed-shape C++ struct/class (§15) - its exact set of fields is known at compile time, the same way a C++ struct's fields are fixed once declared. A handful of everyday JS patterns rely on objects being genuinely dynamic at runtime, which directly conflicts with that - and rather than let a JS developer discover this by hitting a confusing generated-C++ failure, each one is banned outright with a clear cp3-native compile error pointing at the right alternative:

- **`delete obj.prop` is banned.** A fixed-shape struct has nothing to "delete" - the field exists as long as the object does. For a genuinely removable key, use `Map<K, V>` and `map.delete(key)` (§20) instead of a plain object.
- **Adding an undeclared property (`obj.newProp = 1`) is banned.** Every property has to be part of the object's type up front (§15) - there's no equivalent of JS quietly growing an object's shape at runtime. Same fix: use `Map` for anything that's genuinely dynamic.
- **There is no index-signature object type** (TS's `{ [key: string]: number }`) **- this is the direct resolution of "how do plain objects work as dictionaries."** They don't; `Map<string, T>` is the only supported mechanism for a dynamic-keyed dictionary. A plain object's keys are always a fixed, known-at-compile-time set.
- **The `arguments` object is banned.** It's a dynamic, magic array-like value with no static C++ equivalent. Use rest parameters instead (`function sum(...nums: number[])`, already part of the language, §17) - same practical result, fully statically typed.
- **`for...in` on a plain object/class instance is banned.** JS uses it to enumerate an object's keys at runtime, but a cp3 object's keys are already known at compile time - there's nothing to discover. `for...in` (and `for...of` with `.keys()`) stays valid for `Map`/`Set`, where the key set is genuinely runtime-determined, and for direct field access on a plain object, just reference the field by name.

**`Object.keys(obj)`/`.values(obj)`/`.entries(obj)` are supported - and this doesn't reopen the `for...in` ban above, because it isn't runtime reflection.** Since a plain object's field names are fixed at compile time, `Object.keys(obj)` on an object typed `{ a: number, b: string }` expands, at compile time, directly to the literal array `["a", "b"]` - a plain compile-time macro, not a runtime lookup into any kind of live shape/metadata table. There's no dynamic reflection machinery being introduced here; the compiler already knows the answer before it ever generates C++, the same way it already knows everything else about a plain object's shape.

Each of these produces a specific, named compile error rather than either silently doing something unexpected or generating C++ that fails to compile - consistent with the "if a question could arise, the answer should already be in the doc" principle guiding this whole spec, and with CLI-README.md §2's promise that valid-looking cp3 never surfaces a raw C++ error at the user.

## 9. Arrays & Strings

JS-like API, strongly typed. Backed by `std::vector<T>`:

```cp3
let numbers: number[] = [1, 2, 3];
numbers.push(4);
numbers.pop();
let x = numbers[0];

numbers.push("hello"); // compile error - type mismatch
```

Allocation, resizing, copying, and destruction are all automatic.

### Arrays are homogeneous by default - mixed types require an explicit union

Because arrays are backed by `std::vector<T>` - which can only ever hold one concrete type - **arrays cannot silently mix types just because the underlying container "is a vector."** A plain array's element type is fixed:

```cp3
let numbers: number[] = [1, 2, 3];
numbers.push("hello"); // compile error - type mismatch, number[] holds only numbers
```

If mixed types are genuinely needed, the type itself must say so explicitly, using the union mechanism already defined in §3:

```cp3
let mixed: (number | string)[] = [1, "two", 3];
```

This compiles to `std::vector<std::variant<double, std::string>>` - still statically typed, still no `any`, still no dynamic boxing. **A fully dynamic array (anything goes, no declared type at all) is not supported** - allowing that would reintroduce the same dynamic-typing cost (a boxed/`any`-style runtime value on every element) that the whole "no `any`" design goal exists to avoid.

### Array and string methods, made explicit rather than left as "etc."

Earlier drafts of this spec described array/string methods with an illustrative list ending in "etc.," which leaves an implementer guessing exactly where the line is. The full list, each backed by a direct C++ STL/C++20/bundled-library equivalent - no custom parsing logic needed anywhere in this list:

**Array**, beyond the already-shown `push`/`pop`/`map`/`filter`/`reduce`/`find`/`includes`/`flat`:

| Method | Backed by |
|---|---|
| `sort(comparator?)` | `std::stable_sort` - see the two decisions below |
| `join(sep)` | `fmt::join` (`fmt` is already bundled for `console.log`, CLI-README.md §3) |
| `indexOf(x)` | `std::find` |
| `findIndex(fn)` | `std::find_if` |
| `some(fn)` | `std::any_of` |
| `every(fn)` | `std::all_of` |
| `unique()` | `std::unique` - mutates in place, removes adjacent duplicates |
| `shuffle()` | `std::shuffle` with a seeded RNG - mutates in place |
| `binarySearch(target)` | `std::lower_bound`. **Precondition: the array must already be sorted** - this is a genuine, real footgun carried over from C++ itself (an unsorted array gives a wrong answer, not an error), so a debug build adds a sorted-check that panics with a clear message; a release build skips the check for speed, same debug/release split as array bounds-checking |

**`sort()`, two decisions made explicit rather than silently inheriting JS's own behavior:**

1. **No comparator → type-aware default, not JS's string-coercion default.** JS's own `[10, 9, 2].sort()` is a famous footgun - it sorts by *string* conversion, giving `[10, 2, 9]`. Since cp3 arrays are typed, there's no reason to inherit that: `sort()` with no comparator on a `number[]` sorts numerically, and on a `string[]` sorts lexicographically. A deliberate, named departure from JS, same spirit as UTF-8 strings and immutable `Date` - document it, don't silently diverge.
2. **Stable, not just fast.** `std::sort` isn't guaranteed stable; `std::stable_sort` is. Backing `sort()` with `std::stable_sort` matches JS's own (stable) sort semantics exactly, at a small, worthwhile performance cost.

**A comparator returns a JS-style number (negative/zero/positive), not a C++-style bool** - the compiler wraps the user's comparator, comparing its result against `0`, so `nums.sort((a, b) => a - b)` works exactly as a JS developer expects without needing to know C++ comparators return `bool`.

**String**, beyond the already-shown `split`/`trim`/`replace`/`padStart`/`padEnd`/`toUpperCase`/`toLowerCase`/`charAt`:

| Method | Backed by |
|---|---|
| `startsWith(s)` | `std::string::starts_with` - native to C++20, zero extra work |
| `endsWith(s)` | `std::string::ends_with` - native to C++20, zero extra work |
| `includes(s)` | `std::string::find` |
| `indexOf(s)` | `std::string::find` |
| `repeat(n)` | a simple loop/reserve, no library needed |
| `slice(a, b)` / `substring(a, b)` | `std::string::substr` |
| `replaceAll(a, b)` | `absl::StrReplaceAll` (Abseil, CLI-README.md §3) |
| `trimStart()` / `trimEnd()` | `absl::StripLeadingAsciiWhitespace` / `absl::StripTrailingAsciiWhitespace` (Abseil) |

### Strings are UTF-8, not UTF-16 - a deliberate, documented departure from JS

JS strings are UTF-16 internally; cp3 strings are **UTF-8**, backed by `std::string` directly. This is a real, visible difference from JS, not an implementation detail to gloss over, and it has to be stated plainly rather than discovered by surprise:

- **`str[i]` returns a raw byte, not a character.** In UTF-8, a character can take anywhere from 1 to 4 bytes - an emoji like `"😀"` is 4 bytes on its own. Indexing by byte position means `str[0]` on a string starting with a multi-byte character returns one raw byte of that character, not the character itself - silently different from JS, where `"😀"[0]` at least returns half a UTF-16 surrogate pair (also not the full character, but a different kind of "wrong"). Neither language gives you a full character from raw indexing once you're outside plain ASCII - cp3 is just explicit about it being bytes.
- **`str.charAt(i)` returns the actual visual character**, reusing JS's own existing method name rather than inventing a new one. Internally this has to walk the string respecting UTF-8's variable-width encoding (checking the leading byte to know how many bytes the character actually spans) rather than jumping directly to a byte offset - it cannot be a simple O(1) array lookup the way `str[i]` is.
- **Why UTF-8 over UTF-16:** half the memory footprint for the common case (ASCII/Latin text), and 1:1 compatibility with `std::string` and every native C/C++ API a program might interop with (§19) - a UTF-16 string would need a full re-encoding at every native boundary crossing. This is the same choice Rust and Go both made for their own default string types, for the same reasons.

## 10. Memory management

No manual `delete` / `free`, and no garbage collector. Memory is managed by **automatic reference counting (ARC) plus C++'s own RAII** - no runtime heap scan, no pauses, and no compile-time analysis pass deciding where things live:

```cp3
function test() {
  let person = createPerson();
  let numbers = [1, 2, 3];
} // both released automatically at end of scope
```

The key distinction from GC languages (Java/Go/Python/JS): a GC finds garbage by scanning memory *at runtime*, which is exactly what costs pauses and overhead. cp3 instead frees an object the instant its last reference goes away - deterministically, at a point already fixed by ordinary scope rules - using the same destructor mechanism C++ already runs at the end of every scope.

### Two kinds of values - decided, not inferred

- **Value types - plain C++ values, copied on assignment.** Primitives (`number`, `boolean`, `string`, and the other `typeSystem` scalar types, §3), enums, and unions of those. No heap allocation, no reference counting, zero cost beyond what C++ itself pays.
- **Reference types - always heap-allocated, always reference-counted.** Plain objects (`{ ... }`), arrays, class instances, `Map`/`Set`/`Buffer`, and promise/closure state. Each is held through a `std::shared_ptr` (§12). Assigning one to another variable, passing it, returning it, or storing it in an array/field copies the *handle*, never the object - exactly the JS sharing behavior §8 already specifies (`let b = a; b.info = 2;` changes `a.info` too).

The compiler does **not** try to prove that a particular object could have lived on the stack instead. The rule is purely by type: reference type → heap + refcount, always. That is a deliberate trade - a small, predictable allocation cost per object in exchange for a memory model that needs no whole-function analysis to be correct.

### How cleanup works - the one rule

Every reference-type variable compiles to a smart pointer, so C++'s ordinary RAII does all the work:

- When a variable goes out of scope, its destructor drops one reference.
- When the last reference to an object is dropped, the object is destroyed (and, recursively, drops the references *it* holds).
- Returning a value, putting it in an array or object field, capturing it in a closure, passing it as an argument, or using it on only some `if` branches are all the same operation - "take another handle" - so there is no special case for any of them.

```cp3
function inner() {
  let a = { data: 123 };
  return a;            // the caller receives a handle; the object stays alive
}

function outer() {
  const data = inner();   // stored → alive until `data` goes out of scope
  inner();                 // result discarded → released immediately after the call
}
```

This eliminates the common C/C++ failure modes (memory leaks, use-after-free, dangling pointers, double-free) for the vast majority of code, by construction - with one known, deliberate exception: reference cycles, which reference counting alone cannot reclaim (§11).

### Refcounts are atomic

Managed references use **standard atomic reference counting** - literally `std::shared_ptr` (§12). Atomic increments/decrements cost a few nanoseconds each, which is negligible for the application/server code cp3 targets, and in exchange a managed handle can never corrupt its own bookkeeping no matter which thread touches it. This is the same choice Swift's ARC makes. (An earlier draft used a custom non-atomic counter to shave that cost, at the price of forcing the compiler to prove no managed value ever crossed a thread boundary; that trade was reversed - see §23.)

Atomic refcounts protect the *counter*, not the *contents*: two threads mutating the same object's fields without synchronization is still a data race, which is why `worker` and `parallel.*` keep the rules in §13.

### Closures

A closure that captures a `const`, or a value type that is never reassigned, simply holds a copy of it (or of the handle, for a reference type). A `let`/`var` that is **captured by any closure** is stored in a shared, reference-counted cell instead, so the enclosing function and every closure that captured it see the same variable - exactly like real JS. The cell's refcount is atomic like any other managed reference, but the variable inside it is an ordinary non-atomic value (§13). This is a purely syntactic rule (captured + reassignable → shared cell); it requires no analysis of how the closure is used. Closures inherit the same cycle risk, and the same `weak` fix (§11), as any other reference.

### Coroutines and `worker` - one memory story, one unchanged rule

- **A paused async function needs nothing special.** A C++20 coroutine's frame is heap-allocated by the coroutine machinery itself and lives until the coroutine finishes or is destroyed; the function's reference-type locals are ordinary refcounted handles inside that frame, released when the frame is destroyed, and its value-type locals live in the frame directly. It is the same ARC + RAII mechanism as everywhere else, with no separate allocation path.
- **`worker`'s copy-in is still a genuine deep copy of the argument, never a shared handle.** Atomic refcounts make sharing a handle safe for the *counter*, but not for the object behind it - cp3 deliberately exposes no locks, mutexes, or atomics to users (§13), so two threads holding the same mutable object would have no safe way to coordinate. Deep copy is what gives `worker` its JS-Web-Worker-style message-passing isolation: nothing a worker can reach is reachable from anywhere else.


## 11. Reference cycles & the `weak` keyword

Reference counting frees an object when nothing strong points at it - which means two objects pointing *at each other* keep each other alive forever, even after every outside variable is gone:

```cp3
let a = {};
let b = { friend: a };
a.friend = b;      // a ↔ b: each holds the other
b = null;
a = null;          // both outside variables are gone, but a.friend still keeps b alive
                   // and b.friend still keeps a alive - neither count reaches 0: a leak
```

**Every cycle must be broken explicitly, by the programmer, with `weak`.** The compiler never infers, guesses, or auto-assigns it - in particular it does *not* look at class declaration order or at the type graph. An earlier draft auto-marked the "second" class of a parent/child cycle as weak; that was dropped because the strong/weak assignment silently depended on which class happened to be declared first, so reordering code could turn a working program into a leak (§23).

**Declared fields - a `weak` modifier on the field:**

```cp3
class Parent {
  child: Child;          // strong (the default) - Parent keeps Child alive
}
class Child {
  weak parent: Parent;   // weak - Child does NOT keep Parent alive
}
```

**Object literals / untyped links - the `{ weak: value }` wrapper:**

```cp3
let song1 = { next: null, prev: null };
let song2 = { next: null, prev: null };
let song3 = { next: null, prev: null };

song1.next = song2;
song2.next = song3;
song3.next = song1;   // loop, but 'next' keeps the list alive/ordered - leave strong

song2.prev = song1;
song3.prev = song2;
song1.prev = { weak: song3 };   // 'weak' link - doesn't keep song3 alive on its own
```

**What `weak` actually means, mechanically:** every managed value has a hidden counter - "how many strong links currently point at me." A normal link adds 1 to that counter; a `weak` link points to the value too, but does not add to the counter. When the counter reaches 0, the value is destroyed - even if some `weak` links to it still exist (they simply become unusable once that happens). Marking one side of a loop as `weak` is what lets the counter actually reach 0 once nothing outside the loop is using it, instead of the two (or more) objects propping each other up forever.

**Reading a `weak` field after its target is gone:** the read yields `T | undefined` - the ordinary optional type (§3), not a special "dangling" value. While the target is still alive the field behaves like a normal reference; once every strong link is dropped and the target is destroyed, the `weak` field reads as `undefined`, and the caller narrows before use exactly as with any other `T | undefined`. This is the decided beta-one semantics.

**Cycle diagnostics:** the compiler emits a **non-fatal warning** when it can prove a reference-type cycle has no `weak` edge anywhere in it - a "this cycle may leak; mark one side `weak`" hint. This is a diagnostic only: it never fails a build, and it is **not** inference - the compiler still never auto-assigns `weak` or flips strong/weak based on declaration order (see above, and §23). A cycle the compiler cannot prove is left alone silently.

This is not a design flaw - every language that avoids a full garbage collector hits this same wall and takes the same approach: Rust requires `Weak<T>` explicitly to break cycles; Swift's ARC requires explicit `weak`/`unowned` markers. Full GC languages (JS, Java) avoid this only by paying for a runtime reachability scan instead - the exact cost cp3 is built to avoid. `weak` compiles down to `std::weak_ptr` under the hood, reusing C++'s existing, battle-tested mechanism rather than inventing a new one.


## 12. Pointers & references

Raw pointers exist in the generated C++ but are **not exposed** to normal C+++ code. Internally, generated code is free to use `*`, `&`, `std::unique_ptr`, `std::weak_ptr`, etc. - `weak` (§11) compiles down to `weak_ptr` directly, reusing it as-is.

Reference-type values (§10) are held through **`std::shared_ptr`**, used as-is - standard atomic reference counting, no custom smart pointer. `weak` (§11) is `std::weak_ptr`. Value types (primitives, enums) are plain C++ values and involve no pointer at all.

**No raw address access exists in beta one, full stop** - there is no way, safe or unsafe, to read or write an arbitrary memory address (`0x1234`-style) from cp3 code today. This isn't an oversight; it's deliberate for the reason §10/§11 already establish: memory safety rests on every reference being a tracked, reference-counted handle, and an arbitrary address is precisely the case that model has no way to reason about (is it valid, is it owned, is it even mapped into this process?).

An advanced/unsafe mode exposing raw pointers and direct memory addresses could be introduced later as an opt-in escape hatch, separate from the core language - gated behind an explicit `unsafe { ... }` block, never required for normal cp3 code, and never silently reachable from safe code. Two concrete rules for whenever that mode is actually built, decided now rather than left for an implementer to guess:

- **A real integer type for addresses, never `number`.** `number` is always a 64-bit *double* (§3) - it cannot exactly represent every possible 64-bit pointer value. A future address type must use one of the existing raw-type escape hatches (`r_'u64'` or `c_'uintptr_t'`, §3), never the default `number`.
- **A pretty function-call syntax around a raw address does not make it safe on its own.** `let a = getAddress(x)` looks friendlier than C's `int* p = &x`, but if `a` can be stored, returned, or passed elsewhere and used *after* `x` is gone, it's exactly as dangerous as a raw pointer - the safety was never about hiding the `*`, it's about whether the reference can outlive what it points to. Any future safe-ish reference API has to be scoped and non-escaping by construction (the same pattern §19's native pointer safety rule already relies on for wrapping native pointers) - a callback that receives the reference and cannot let it leak out, not a plain value that can be assigned anywhere.

### `ref` - by-reference parameters

For the case where a function needs to modify the caller's actual variable, not a copy of it, `ref` marks a parameter as by-reference:

```cp3
function addTen(ref x: number) {
  x = x + 10;
}

let a = 5;
addTen(a);
console.log(a);   // 15 - the real 'a' was modified, not a copy
```

This is a thin wrapper over an existing, standard C++ feature - `ref x: number` transpiles directly to C++'s own reference parameter syntax (`number& x`). No new engineering required; C++ already handles this correctly.

## 13. Concurrency: `parallel.*` and workers

Two distinct concurrency models, chosen based on the shape of the task - never one mechanism trying to cover both:

- **`parallel.*`** - shared-memory, structured data parallelism, for splitting one computation across CPU cores
- **`worker`** - isolated, message-passing tasks, for independent background jobs

### `parallel.*` - shared-memory data parallelism

Backed internally by TBB (Threading Building Blocks), wrapped behind three narrow, structured operations - never raw thread creation, and never exposing locks, mutexes, or atomics to the user:

```cp3
// Apply the same operation across a range - tbb::parallel_for underneath
parallel.for(0..numbers.length, (i) => {
  numbers[i] = numbers[i] * 2;
});

// Split a computation across a range and safely combine results - tbb::parallel_reduce underneath
let total = parallel.reduce(1..100000, 0, (sum, i) => sum + i);

// Run several independent functions concurrently - tbb::parallel_invoke underneath
let results = parallel.run(
  () => sumRange(1, 100),
  () => factorial(30),
  () => alternatingSum(1, 100)
);
```

`parallel.reduce` gives each thread its own local accumulator and combines them safely at the end - the developer never manually synchronizes anything; the compiler generates the thread-local-accumulate-then-merge pattern automatically.

**Capture safety, precisely:**

**Rule 1 - closures passed to `parallel.*` always take a raw, non-owning borrow of anything captured, never a refcounted handle.** Atomic refcounts (§10) would make retaining a captured object from several threads *correct*, but it would also be slow in exactly the place speed matters: every core hitting the same refcount cache line on every iteration serializes the very parallelism `parallel.*` exists to provide. And it is unnecessary - every `parallel.*` call is **synchronous**; the calling thread blocks until every closure finishes, so any value already alive in the calling scope is *already* guaranteed to outlive the whole operation (the same reasoning the native pointer safety rule, §19, relies on for scoped views). Reading a captured array/object from many closures at once therefore costs nothing and touches no refcount.

**Rule 2 - the one named safe-write exception.** Writing `numbers[i] = numbers[i] * 2` inside `parallel.for(0..numbers.length, (i) => {...})` **is** writing to a shared captured array from multiple closures, which is otherwise banned, because the borrow in Rule 1 protects the *lifetime* of the object but not unsynchronized mutation of its *contents* (atomic refcounts don't help there). The specific case that is provably safe and explicitly allowed: **writing through `parallel.for`'s own provided loop index** - every iteration gets a distinct `i`, so no two closures can ever touch the same element; no aliasing is possible, by construction of the loop itself. **Anything else** - writing through a separately computed/stored index, calling a mutating method on a captured shared object (`sharedList.push(...)`), or writing to a field rather than an indexed element - remains a compile error, since the compiler has no general way to prove those writes are disjoint:

```cp3
let numbers = [1, 2, 3, 4, 5];

parallel.for(0..numbers.length, (i) => {
  numbers[i] = numbers[i] * 2;   // ✓ allowed - writes through the loop's own index, provably disjoint
});

let sharedList = [1, 2, 3];

parallel.run(
  () => { sharedList.push(10); },  // ✗ compile error - not an index write, can't be proven disjoint
  () => { sharedList.push(20); }
);
```

The correct pattern is always to return data and let the calling thread combine it, never mutate a captured outer variable:

```cp3
let results = parallel.run(
  () => computePartA(),
  () => computePartB()
);
let combined = results[0] + results[1];
```

**Reassigning a captured `let`/`var` inside a `parallel.*` closure is a compile error too** - the most common version of the same mistake, spelled out because it is the one a JS developer will reach for first:

```cp3
let total = 0;
parallel.for(0..1000, (i) => {
  total = total + 1;   // ✗ compile error - shared mutable capture; use parallel.reduce
});

let total2 = parallel.reduce(0..1000, 0, (sum, i) => sum + 1);   // ✓ each thread accumulates privately, merged at the end
```

Reading a captured `let`/`var`/`const` from many closures at once is fine, since nothing writes to it.

**`let`, `var`, and `const` are never atomic - no binding keyword changes how a variable is accessed across threads.** Making `let`/`var` atomic so parallel closures could update them directly was considered and rejected, for three reasons:

- **Everyone would pay for it.** Almost all code never touches a second thread, yet every `let` in every program would carry the atomic cost - against the "no runtime tax if you don't use it" promise (§1).
- **It would not make code correct.** Atomic reads and writes do not make a *sequence* safe: `if (balance > 50) { balance = balance - 50; }` run from two threads can still overdraw, because the check and the update are separate steps. Fixing that takes a lock, and cp3 deliberately exposes no locks (§13 intro). Atomic variables would only give a false sense of safety.
- **It cannot be applied uniformly.** `std::atomic` only works on small trivially-copyable types, so atomic `let` would work for `number` but not for `string`, arrays, objects, or class instances - an inconsistent rule to teach. (And ten cores updating one atomic variable still queue on the same cache line, so it would often be slower than single-threaded code.)

The supported tools are the ones above: read-only sharing, `parallel.reduce` for combining, and the disjoint loop-index write for filling arrays.

### `worker` - isolated, message-passing tasks, one real OS thread each

Rather than sharing memory between threads (which would leave two threads free to mutate the same object with no safe way to coordinate, since cp3 exposes no locks or atomics to users), `worker` avoids shared memory entirely. Each `worker` call spins up its own real OS thread, running its own independent event loop (§14) - not a separate OS process. A thread is far cheaper to spawn than a process, and the safety `worker` provides comes from the compiler's discipline around what's reachable inside it, not from OS-level process isolation. The tradeoff this accepts: a crash inside a worker thread can take the whole program down with it, the same as any other unhandled crash - full process-level fault isolation isn't part of what `worker` provides in beta one.

```cp3
// worker.cp3
export default (data) => {
  // process data, return a result
  return processedResult;
};
```

```cp3
// main.cp3
import worker from "worker.cp3";

const output = await worker(dataNeededForProcessing);
```

`worker(...)` returns a `Promise` (§14), resolving with whatever the worker file's default export returns, or rejecting if the worker file throws - a plain `try`/`catch` around the `await` catches it, exactly like any other async call (`fs.readFile`, `process.exec`). No separate event/handle mechanism - it's the same Promise shape as everything else in std.

Whatever crosses into a worker file is **copied in as a genuine deep copy**, not a shared reference - nothing else is implicitly reachable from inside it. This has to be a real copy rather than a shared handle across the thread boundary: atomic refcounts (§10) keep the *counter* safe, but two threads mutating the same object's fields would still be a data race, and cp3 deliberately gives users no locks to prevent one. This mirrors how JS Web Workers already behave (message-passing, not shared memory), so it matches the existing JS mental model.

### When to use which

| Need                                                                         | Use               |
| ---------------------------------------------------------------------------- | ----------------- |
| Same operation across a big array/range                                      | `parallel.for`    |
| Same computation, combine results (sum, max, etc.)                           | `parallel.reduce` |
| A few different, independent functions at once                               | `parallel.run`    |
| One long-running, independent background task, isolated from everything else | `worker`          |

## 14. Async

Same model as JavaScript - `async`/`await` over an event loop, not raw OS threads. The one real difference from JS: JS can keep a paused `async` function's local state alive for free because its GC tracks it automatically. cp3 has no GC, so a paused `async` function's locals live in its heap-allocated coroutine frame, where reference-type locals are ordinary refcounted handles released when the frame is destroyed (§10). Same underlying mechanism, no new kind of problem.

### Backing architecture: C++20 coroutines, driven by a single libuv event loop

```
C+++ async function
       ↓
C++20 coroutine (co_await / co_return, generated by the compiler)
       ↓
C+++ runtime (Promise/Task type, error propagation, combinators)
       ↓
libuv (single event loop: timers, async file I/O, process I/O, thread pool for blocking work)
       ↓
OS
```

- Every `async function` transpiles to a real C++20 coroutine; every `await expr` transpiles to `co_await expr`. This is the reason CLI-README.md §1 now requires a C++20-minimum toolchain.
- A single libuv event loop instance drives the whole program - the same library that backs Node.js's own event loop, chosen instead of hand-writing a cross-platform reactor (epoll/kqueue/IOCP) from scratch, for the same reason crypto wraps libsodium rather than reimplementing AES (§20). `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval` map directly onto libuv's own timer API - this is the concrete reason they stay core-language builtins rather than a std import (`STDLIB-README.md` has no `std/timers` module for exactly this reason): the moment any program uses `async`/`await` at all, libuv is already linked in, so gating timers behind a separate import wouldn't save any linkage - there's no additional library being hidden.
- **libuv is an implementation detail, never exposed to cp3 code.** No cp3 program ever sees a libuv handle, callback, or type directly - same "wrap it invisibly" rule as every other internally-wrapped library.
- A program that never touches `async`/`await`, `setTimeout`/`setInterval`, or any Promise-returning std call links no event-loop machinery at all - the "no runtime tax if you don't use it" promise from the intro holds regardless of `setTimeout` being a builtin rather than an import, since linkage is decided by whole-program usage analysis, not by whether the identifier came from an import.

**What "the C+++ runtime" in the diagram concretely is: a generated Awaiter/Bridge object per suspension point.** A C++20 coroutine's `co_await` needs an object satisfying a specific compiler-recognized shape (an "awaiter") at every suspension point - the piece that actually connects a coroutine's "please pause me" to libuv's callback-based world. The compiler generates one of these automatically for every std API that returns a `Promise` (`fs.readFile`, `setTimeout`, `fetch(...)`, etc.): it registers the real libuv callback (a timer firing, a read completing) and, when that callback runs, resumes the exact coroutine that was waiting. This is internal, generated machinery - a cp3 program never writes an awaiter itself, the same as it never touches libuv directly.

**Async inlining - the compiler doesn't pay the coroutine cost for a function that never actually suspends.** An `async function` that never hits a real `await` (or one where the compiler can prove every `await` inside it resolves synchronously) doesn't need a real, heap-backed coroutine frame at all - the compiler can inline it down to a plain function returning an already-resolved `Promise`, skipping state-machine generation entirely. This matters for binary size and compile time: a codebase can have many small `async` functions for API consistency without every single one generating a full coroutine frame, the same way a modern C++ compiler already elides needless overhead for code that doesn't need it. This is a compiler optimization, not a semantic difference a program can observe - an inlined async function still returns a real `Promise` and behaves identically to a "real" coroutine from the caller's perspective.

### `Promise<T>` - supported in full, not just `await`

`await` is the primary, everyday way to consume async work - but `Promise<T>` itself is an ordinary core-language type with the complete JS-familiar surface, not a stripped-down version of it:

```cp3
promise.then((value) => { /* ... */ });
promise.catch((err) => { /* ... */ });
promise.finally(() => { /* ... */ });

Promise.all([p1, p2, p3]);          // rejects as soon as any one rejects
Promise.race([p1, p2, p3]);         // settles as soon as any one settles
Promise.allSettled([p1, p2, p3]);   // always resolves, with each result tagged ok/error
```

These are ordinary cp3-level combinators built on top of whatever already backs a single `await` - coordinating multiple already-suspended coroutines, not a separate libuv feature.

### Unhandled rejections: fail loud, not silent

A rejected `Promise` that's never `await`ed or `.catch()`'d anywhere terminates the process, after printing through the same diagnostics subsystem as any other error (CLI-README.md §2) - a stricter default than Node's historical "print a warning and keep running," made deliberately, since silently swallowing a broken async task hides real bugs rather than surfacing them.

**This is the default for a normal program, not an absolute rule for every std module.** A module that processes a stream of independent, unrelated tasks off shared infrastructure - most obviously `std/network`'s HTTP server handling many concurrent requests (and open WebSocket connections) on one event loop - must catch each task's own error itself and respond to *that task* accordingly (e.g. an HTTP 500 for the one failing request), rather than letting it bubble up to the global unhandled-rejection handler and take the whole process down over a single bad request. This isn't a contradiction of the rule above - it's the same rule applied at the right boundary: "unhandled" means nothing in the *program* caught it, and a well-built server module is exactly the thing responsible for catching it at the request level before it becomes program-level unhandled. See `std/network` in `STDLIB-README.md` for the concrete case.

### Program exit

A cp3 program exits once the event loop has no pending work left (no outstanding timers, no in-flight I/O) - or immediately, via `process.exit()`. Same rule as Node.

### Explicitly deferred, not silently missing

No `AbortController`-style cancellation in beta one - a started async operation runs to completion or failure, it can't be told to stop early from outside. This is a real gap for some use cases (a fetch you want to abandon on user navigation, say) and is on the post-beta-one list, not forgotten.

## 15. Classes

TypeScript-like class syntax, compiled to equivalent C++ classes:

```cp3
class Person {
  private name: string;
  public age: number;

  function greet(): void {
    console.log(name);
  }
}
```

### Access modifiers

`public` / `private` / `protected` - matches TS exactly, mapped directly to C++'s native access-modifier sections.

### Static members

```cp3
class Counter {
  static count: number = 0;
  static function increment(): void { Counter.count = Counter.count + 1; }
}

Counter.increment();
console.log(Counter.count);
```

Maps directly to C++'s native `static` members.

### Inheritance - single, TS-style

```cp3
class Animal {
  name: string;
  function speak(): void { console.log("..."); }
}

class Dog extends Animal {
  function speak(): void { console.log("Woof"); }
}
```

Single inheritance only (`extends`, one parent) - matches TS exactly. Multiple *class* inheritance is deliberately not supported, to avoid C++'s diamond-problem ambiguity; multiple **interfaces** (below) cover the "combine several shapes" use case instead.

### Interfaces - structural, TS-style

```cp3
interface Shape {
  function area(): number;
}

interface Sized {
  function size(): number;
}

class Circle {
  function area(): number { return 3.14; }
  function size(): number { return 1; }
  // no 'implements' written anywhere - and this still satisfies Shape and Sized
}

function printArea(s: Shape) { console.log(s.area()); }
printArea(new Circle());   // works - Circle just happens to have the right shape
```

**Structural, not nominal** - matching real TypeScript rather than C++'s own model: any type with the right methods/fields satisfies an interface automatically, whether or not it explicitly says so. Writing `implements Shape` is still allowed (optional, purely self-documenting) but never required for the compiler to accept it.

This is a deliberate departure from C++'s own nominal model, and it's a genuinely substantial piece of engineering, not a small tweak - comparable in scope to TypeScript's own type checker. The realistic implementation path: check shape-compatibility at **compile time** (does this type have the required methods, with the required signatures), then, for cases needing runtime polymorphic storage (e.g. an array holding several different concrete types that all satisfy the same interface), generate a **type-erased wrapper** automatically - the same general technique Go's runtime interface values use (a small automatically-generated struct pairing a type descriptor with the underlying data), not raw C++ virtual inheritance, since that would force interfaces back to being nominal.

### Object literals against a declared type - excess property checking, decided

```cp3
interface SomeType {
  id: number;
  value: number[];
}

let abc: SomeType = { id: 123, value: [1, 2, 3, 4, 5, 6, 7] }; // fine - matches exactly
let bad: SomeType = { id: 123, valeu: [1, 2] };   // ✗ compile error, not silently accepted
```

Missing a required field is always a compile error, naming the field. **A field the target type doesn't declare at all is also a compile error - but only when the object is a fresh literal written directly at the point of assignment**, matching TypeScript's own "excess property checking" rather than plain structural width-subtyping. This exists specifically to catch the typo above (`valeu` instead of `value`) - without it, a fresh literal with a misspelled field would just look like "one extra, harmless field" to a purely structural checker and compile silently, which is a far worse outcome than a clear error naming the mismatch. This check applies only to the literal written in place; a value already sitting in a variable and passed along afterward is checked with ordinary structural compatibility (extra fields allowed), the same distinction TypeScript itself draws:

```cp3
let extra = { id: 123, value: [1, 2], another: true };
let abc: SomeType = extra; // fine - not a fresh literal, ordinary structural match, extra field ignored
```

**One caveat when `SomeType` is a `class` rather than an `interface`:** this only works when every field involved is public. A class with any `private`/`protected` field cannot be satisfied by a bare object literal at all - there's no constructor being called, so there's no way to legitimately set an inaccessible field, and the compiler rejects it outright with a clear message naming the inaccessible field, rather than silently ignoring privacy or producing a confusing generated-C++ failure.

### Abstract classes/methods

```cp3
abstract class Shape {
  abstract function area(): number;   // no body - subclass MUST implement
  function describe(): void {          // has a body - inherited as-is
    console.log("A shape");
  }
}

let s = new Shape();      // compile error - can't instantiate an abstract class directly
class Circle extends Shape {
  function area(): number { return 3.14; }
}
```

Same pure-virtual mechanism as interfaces, mixable with regular methods in one class.

### `this` binding

Methods are **always correctly bound** to their instance, regardless of how they're called or detached - this is the natural, free C++ behavior, and deliberately *not* JS's quirk where a detached method call can lose its `this` (the reason JS/TS developers often reach for arrow-function class fields as a workaround). Reproducing JS's broken behavior would cost real, ongoing compiler complexity for no benefit, so cp3 doesn't.

## 16. Generics

Angle-bracket syntax, TS/Java-style, for both classes and functions:

```cp3
class Box<T> {
  value: T;
}

let a = new Box<number>();
let b = new Box<string>();

function identity<T>(x: T): T {
  return x;
}
```

Generic constraints (`function foo<T extends Shape>`) map to C++ templates/concepts - a direct mapping, no new mechanism needed.

**This is a real C++ template under the hood, not a disguise for something else** - `identity<T>` genuinely compiles to a C++ function template, instantiated per concrete type used, same as any hand-written C++ template. This might look like it contradicts §19/CLI-README.md §4's native-interop scope excluding templates for beta one, but the two situations aren't the same problem: bindgen's issue is reading *someone else's already-written* C++ template and having no way to know what concrete type it should be instantiated with, since a template is a blueprint with the type left unfilled. cp3's own generics don't have that problem - the cp3 frontend already knows the concrete type at every call site, because it type-checked the call itself before ever emitting a line of C++. There's no guessing involved on cp3's own side; the guessing problem only exists at the boundary where cp3 has to reverse-engineer someone else's C++.

### Other direct 1:1 mappings

The following exist identically in both TS and C++, so they need no separate design decision - just a direct compiler mapping: **getters/setters** (`get`/`set`), **function overloading** (C++ supports this natively), **tuple types** (`[string, number]` → `std::tuple`), **ternary (`? :`)**, **bitwise operators**, and **default parameter values**.

### Explicitly skipped for v1

**Generators (`function*`/`yield`)** and **decorators** (`@Decorator`) both exist in TS but have no native C++ equivalent - generators would need C++20 coroutines wired up specifically, decorators would be entirely custom-built. Both are deliberately **deferred**, not part of v1.

## 17. Functions

```cp3
function add(a: number, b: number): number {
  return a + b;
}

function createPerson(): Person {
  return new Person();
}
```

No C++-style declaration complexity; the compiler determines the efficient underlying representation. Function overloading (multiple functions with the same name, different parameter types) is supported directly - C++ already handles this natively.

## 18. Modules & imports

Instead of dumping everything into a global `std::` namespace, imports resolve to namespaced module objects:

```cp3
import console from "std/console";
console.log("Hello");

import Math from "std/math";
let x = Math.sqrt(25);
```

```cp3
import "cpp/iostream" as io;
import "cpp/filesystem" as fs;

io.println("Hello");
fs.createDirectory("test");
```

These module objects don't necessarily need to exist at runtime - resolution can happen entirely at compile time.

### Prelude files: `_prelude.cp3`, `_prelude.same.cp3`, `_prelude.weak.cp3`

A folder can contain a special prelude file whose contents (typically `import` statements, but not limited to that) are automatically injected at the top of every `.cp3` file in scope - removing the need to repeat the same imports in every file of a folder.

Three variants, differing only in how far the injection reaches:

**`_prelude.cp3` - strong.** Injects into every `.cp3` file in the same folder **and every subfolder, recursively, no matter how deep**. Nothing can stop it from reaching further down the tree.

```
src/
  _prelude.cp3        ← reaches utils.cp3, helpers/math.cp3, helpers/deep/calc.cp3 - everything under src/
  utils.cp3
  helpers/
    math.cp3
    deep/
      calc.cp3
```

**`_prelude.same.cp3` - folder-only.** Injects only into `.cp3` files directly in the same folder - does **not** cascade into subfolders at all.

```
src/
  _prelude.same.cp3   ← reaches utils.cp3 only
  utils.cp3
  helpers/
    math.cp3           ← does NOT get this injection
```

**`_prelude.weak.cp3` - recursive, but stoppable.** Like the strong version, it cascades into subfolders - but if a subfolder contains **any** prelude file of its own (even a completely empty one), that acts as a wall: the weak injection stops there and does not reach any file at or beyond that subfolder.

```
src/
  _prelude.weak.cp3   ← reaches utils.cp3 and helpers/math.cp3
  utils.cp3
  helpers/
    math.cp3            ← gets src/'s weak prelude (no wall here)
    walled/
      _prelude.cp3         ← empty or not, its mere presence walls off this subtree
      thing.cp3              ← does NOT get src/'s weak prelude
      deeper/
        x.cp3                  ← also walled off - the block applies to the whole subtree past the wall
```

**Multiple prelude files in one folder are allowed and stack together, including strong + weak at once** - confirmed not a conflict. Injection order: **`_prelude.same.cp3` first, then `_prelude.cp3` (strong), then `_prelude.weak.cp3`** - folder-local content first, then the two recursive variants. When strong and weak coexist, each simply follows its own already-defined rule independently: strong's content keeps recursing unconditionally no matter what's below it; weak's content also recurses, but still stops wherever a deeper folder plants its own prelude file - the two don't interact or override each other, they just both apply, strong's injection landing before weak's.

## 19. C/C++ interoperability

C+++ is meant to plug into the existing native ecosystem rather than reinvent it:

```cp3
import native "opencv" as cv;
```

The compiler generates the necessary `#include`s and linker configuration. **This shows import syntax, not a claim that all of OpenCV works today** - OpenCV's real API leans heavily on operator overloading and templated types (`cv::Mat`'s `operator()`, `Mat_<T>`), and beta one's native-interop scope explicitly excludes both (below). A binding limited to OpenCV's plain free functions with simple parameter types is realistic for beta one; the full class-based API is post-beta-one bindgen work (`CLI-README.md` §6).

### Larger libraries (Qt, Unreal Engine, etc.)

These aren't rewritten - they're wrapped:

```
C+++ bindings
   ↓
C++ API
   ↓
Unreal Engine / Qt / etc.
```

```cp3
import "ue5" as UE;
let actor = new UE.Actor();
```

The C+++ developer only ever sees C+++.

### Native pointer safety rule

`import native` cannot make every C++ pointer automatically safe just by crossing the boundary - a binding layer has to classify *what kind* of pointer it's actually dealing with, and wrap each kind differently. Five shapes cover the real cases:

| Native C++ shape | cp3-facing wrapper |
|---|---|
| **Owned pointer** you must free (`Thing* create(); void destroy(Thing*);`) | An ordinary object whose destructor calls the matching free function automatically - the user never sees `Thing*`. |
| **Borrowed pointer + known length** (`const uint8_t* getData(size_t* len);`) | A scoped view handed to a callback (`data.withBytes((bytes) => {...})`) - the compiler rejects any attempt to store, return, or capture `bytes` beyond the callback - a narrow restriction on this one scoped-view type (the same borrow-only idea `parallel.*` captures rely on, §13), not a general lifetime-analysis pass. |
| **Borrowed pointer for one call only** (`void process(const uint8_t*, size_t);`) | The same scoped-view pattern, with the binding author responsible for guaranteeing the native function doesn't retain the pointer past the call - a binding that lies about this breaks safety, same as any FFI boundary in any language. |
| **Unknown/untyped `void*`** | Cannot be made safe without the binding author supplying real metadata (what it is, how it's freed, its size). Without that metadata, it's `unsafe`-only (§12). |
| **Device/non-CPU pointers** (e.g. CUDA device memory) | Wrapped as an opaque buffer object (`cuda.allocBuffer(...)`, `.copyFrom(...)`, `.copyTo(...)`) - the actual device pointer never appears as a cp3 value, since it isn't even a valid address in this process's own memory space to begin with. |

This gives three concrete levels, not just an assertion that "pointers are hidden":

1. **Safe normal language** - `let`/`const`/arrays/objects, no addresses, no pointers, ever (this is what essentially all cp3 code is).
2. **Safe advanced memory** - native-interop's scoped views (the table above) - a real interop tool, still fully memory-safe because the reference can't outlive its owner.
3. **`unsafe` escape hatch** - raw addresses and untyped pointers, explicitly gated (§12), for the cases that genuinely can't be wrapped any other way.

The underlying rule, stated plainly: **native pointers are allowed inside generated C++; they are never allowed as a value a normal cp3 program can hold.** If a native API can't be classified into one of the safe shapes above, the binding must require an explicit `unsafe` block to touch it - that's a deliberate, visible cost, not a gap in the design.

## 20. Standard library

Core data-type operations (array/string methods, regex literals) are built into the language itself - no import needed. Everything that reaches beyond core types into OS/runtime functionality is an explicit import:

```
std/
├── console   (.log/.warn/.info/.input)
├── JSON      (.stringify/.parse, generic .parse<T> with runtime shape validation)
├── Math
├── fs        (Node-style, Sync-suffixed + async + stream variants)
├── Date      (immutable - every "change" method returns a new Date)
├── process   (.exec buffered, .spawn event-based, .argv, .env, .exit)
├── path
├── os
├── events    (EventEmitter-style: .on/.emit/.off, pure cp3, no native dependency)
├── crypto    (randomUUID, sha256/512, AES256 - libsodium-backed)
├── env       (.load(path), .get<T>(key, default) - built on top of process)
├── collections (Map/Set, 1:1 wrapping std::unordered_map/std::unordered_set)
├── buffer    (raw binary data handling)
└── network   (fetch - exact JS shape, not axios - plus WebSocket/WebSocketServer and an HTTP server, all backed by uWebSockets)
```

```cp3
import console from "std/console";
import Math from "std/math";

console.log(Math.sqrt(25));
```

**Written in cp3 itself, not hand-written C++.** The standard library is authored as ordinary `.cp3` source, same language as everything else - no separate C++ codebase to maintain in parallel. Where a module genuinely needs to reach real OS/C++ functionality underneath (actual file handles, math intrinsics, cryptographic primitives), it does so through the same **native import** bridge used for wrapping external C++ libraries (§19) - e.g. `fs` internally uses `import native "cpp/filesystem"` to reach real file APIs; `crypto` internally uses `import native "libsodium"` for hashing and AES-256 encryption.

**`std/network` - reinstated for beta one, evolved further from an initial `std/server`.** A previous draft of this doc removed server support entirely, reasoning it would mean wrapping Drogon (a large, template-heavy C++ framework - a bad fit given beta one's native-interop scope explicitly excludes templates, §19). That reasoning was sound *against Drogon specifically*, but dropping the capability along with it was a strategic overreach: it left the language's own headline pitch (the Express-vs-cp3 comparison in the intro) with nothing to actually demonstrate on day one. The fix isn't to wrap Drogon - it's to build directly on what beta one already requires, and consolidate further than the first reinstatement did: `fetch`, an HTTP server, and WebSocket support now live together in one `std/network` module, using the same libuv event loop that `async`/`await` already mandates for the client side, plus **uWebSockets** (widely regarded as among the fastest HTTP/WebSocket C++ libraries available, handling both in the same event loop) for the server/WebSocket side. No heavy general-purpose C++ web framework - full API and scope in `STDLIB-README.md`.

**`std/webview` remains dropped** - nothing in this reconsideration applies to it; it could still become a third-party package later, same as before.

### `setTimeout` / `setInterval` / `clearTimeout` - core language builtins, not std

Because these only mean anything in terms of the event loop the language itself already provides (§14, Async), they are **global builtins, no import required** - same as JS - rather than a std module:

```cp3
let id = setTimeout(() => console.log("later"), 1000);
clearTimeout(id);

let intervalId = setInterval(() => console.log("tick"), 500);
clearInterval(intervalId);
```

**Full module-by-module detail (exact function signatures, what's deferred) lives in the companion `STDLIB-README.md`**, tracked separately so the core language spec here isn't blocked on every std function being named.

**Bundled with the compiler - never a package to install.** Unlike a third-party dependency, `std/*` ships built into the `cp3` toolchain itself. `import console from "std/console"` works immediately in any project, with no `cp3 install`, no entry in `package.json`, and no `.cp3/packages/` folder involved - it's always available, the same way importing from `std::` needs no setup in C++.

## 21. Design goal: 2-day onboarding for JS/TS devs

To hit this, C+++ deliberately avoids requiring beginners to learn, upfront:

- Pointers
- References (beyond the eventual `ref` escape hatch)
- `new` / `delete`, `malloc` / `free`
- Header files
- CMake / build system configuration
- Template syntax
- Manual ownership or borrow-checking rules

Instead, the vocabulary is meant to feel like: `let`, `var`, `const`, `weak`, `ref`, `try`/`catch`/`finally`, `function`, `class`, `extends`, `implements`, `abstract`, `enum`, `import`, `array`, `string`, `switch`, `for`/`for...of`/`for...in`, `while`/`do...while`.

## 22. What C+++ combines

```
        JavaScript
            │
      familiar syntax
            │
            ▼
      ┌───────────┐
      │   C+++    │
      └───────────┘
       │    │    │
       │    │    └── C/C++ ecosystem access
       │    │
       │    └─────── automatic memory management
       │
       └──────────── static typing
            │
            ▼
        C++ backend
            │
            ▼
        native code
```

Target: **TypeScript's familiarity + C++'s native performance + automatic memory management + a modern package ecosystem.**

## 23. Open design questions

The concept is considered technically sound; what remains is specification and real engineering, not a blocking unknown.

**Resolved since the initial draft** (kept here for history, language decisions only - see `CLI-README.md` for tooling/package-manager history): **`let`/`var`/`const` mutability corrected** - an earlier draft had `let` and `const` both immutable with only `var` mutable, which was backwards relative to real JS; fixed so `let` and `var` are both reassignable and `const` is the sole immutable binding, matching real JS exactly (§8) · reference-cycle handling (originally auto for different-field cycles + `weak` for same-field cycles - **superseded: every cycle is broken with an explicit `weak`**, §11) · closures capturing plain variables (a captured, reassignable `let`/`var` lives in a shared refcounted cell, shared across closures, same cycle rules, §10) · threading model (worker files, copy-in only, no shared memory, no user-facing atomics or locks) · async model (JS-style event loop; a paused function's state lives in its heap-allocated coroutine frame) · base type system (TS default, overridable via `typeSystem`, `r_'...'`/`c_'...'`/`ts_'...'` prefix escape hatches, C++-style implicit numeric widening by default with an opt-in strict flag, literal syntax always cp3's own) · optional/nullable types (`undefined` and `null` both supported, unions backed by `std::variant`) · mixed-type arithmetic rules (compare/store freely, operate requires explicit conversion) · error handling (JS-style `try`/`catch`, no separate exception hierarchy) · `ref` by-reference parameters (§12) · generics (angle-bracket syntax, classes and functions) · class model (single inheritance, multiple interfaces, `abstract`, access modifiers, `static`, always-bound `this`) · enums (plain and string-backed) · operators (`==`/`===` both kept, JS-style truthy/falsy in `if`, `?.`/`??` kept) · control flow (`switch` with JS fall-through, all 3 `for` variants, full destructuring, spread/rest, dense arrays only - no sparse holes) · comments and literal syntax (JS-style) · generators and decorators explicitly **skipped for v1** · prelude files (`_prelude.cp3`/`.same.cp3`/`.weak.cp3` - strong/folder-only/stoppable-recursive injection, §18; empty files still wall off weak injection; multiple prelude files per folder allowed and stack) · standard library architecture (authored in cp3 itself via native import, not hand-written C++; bundled with the compiler, never a separate install - §20) · std lib expanded (`path`, `os`, `events` [pure cp3], `crypto` [libsodium-backed: `randomUUID`, `sha256`/`sha512`, `AES256`/`AES256Decrypt`], `env` [built on `process`]; `setTimeout`/`setInterval`/`clearTimeout` placed as core language builtins, not std, since they're tied to the event loop) · `std/server` (Drogon) and `std/webview` both considered, then **deliberately removed** from the core plan entirely - not deferred, just dropped; either could become an ordinary third-party package later, with no official status assumed · `JSON.parse<T>` runtime-validates against `T`'s shape and throws (catchable via existing try/catch) on mismatch - this requires generics to carry **reified runtime type metadata** for classes/interfaces used this way, a real new engineering requirement beyond what plain compile-time-only generics needed before · `Date` made immutable (every "change" method returns a new `Date`, avoiding JS's classic mutable-`Date` footguns) · `process.exec` (buffered, single result) vs `process.spawn` (event-based streaming) split, matching Node's distinction exactly · toolchain detection & auto-install (`cp3 init`/`build`/`run` check for an available C++ toolchain first, offer to install Clang/LLVM directly if none found, CLI-README.md §1) · package `export default` statement made mandatory for `"type": "package"` (build fails without it), distinct from the still-optional reserved `default` key within it (§19) · `Map`/`Set` included, 1:1 wrapping `std::unordered_map`/`std::unordered_set` · `buffer` included as its own std module · interfaces made **structural** (TS-style, no explicit `implements` required - a real subsystem comparable in scope to TypeScript's own type checker, implemented via compile-time shape-checking plus an automatically-generated type-erased wrapper for polymorphic storage, §15) · custom classes can plug into `for...of` via `begin()`/`end()` methods, mapping directly to C++'s existing range-based `for` mechanism (§6) · `number` confirmed to always mean a 64-bit double under the TS-default type system, with no automatic inference to a narrower integer type - real integer performance requires explicitly reaching for the `r_'...'`/`c_'...'` escape hatches (§3) · **concurrency model expanded to two distinct mechanisms**: `parallel.*` (`parallel.for`/`parallel.reduce`/`parallel.run`, shared-memory structured data parallelism backed by TBB, capture rules: closures always take a non-owning borrow rather than a refcounted handle - originally because a shared non-atomic refcount was a race, now because atomic refcount contention would serialize the parallel loop and the synchronous call already guarantees lifetime - and disjoint index-writes through a loop's own provided index are the one named safe-write exception, §13) alongside the existing `worker` (isolated, message-passing, §13) · **memory management strategy simplified (see the "Memory-model simplification" entry below - supersedes the earlier escape-analysis-first design)**: value types on the stack, reference types always heap-allocated behind atomic-refcounted `std::shared_ptr`, cleanup by C++ RAII, explicit `weak` for cycles (§10, §11)

**Resolved for beta one** (closing out items that were previously open - language decisions; see `CLI-README.md` for tooling/package-manager items):

- **`while` and `do...while` added to §6 - a genuine, previously-missing control-flow gap, not a minor omission.** Earlier drafts defined `switch` and all three `for` variants but never mentioned `while` at all, despite it being fundamental JS and despite the doc's own vocabulary list implicitly assuming it existed. Maps directly to C++'s native `while`/`do...while` - zero new engineering, same as the `for` variants.
- **`finally` added to §7 - the same class of gap as `while`.** `try`/`catch` alone isn't "the same model as JavaScript" without it; `finally` is used constantly for cleanup. Compiles via a generated C++ scope guard (a small internal struct whose destructor runs the `finally` code) - the same RAII mechanism §10 already relies on for memory, reused here for arbitrary cleanup rather than just destruction.
- **Array and string method lists made explicit, closing a real "etc." gap** (§9): `join`, `indexOf`, `findIndex`, `some`, `every`, `unique`, `shuffle`, `binarySearch` for arrays; `startsWith`, `endsWith`, `includes`, `repeat`, `slice`/`substring`, `replaceAll`, `trimStart`/`trimEnd` for strings - each mapped to a specific STL/C++20/bundled-library backing, not left to an implementer's guess. `sort()` decided as two explicit departures from naive JS parity: a type-aware default (numeric for `number[]`, lexicographic for `string[]`, avoiding JS's own string-coercion sort footgun) and `std::stable_sort` (matching JS's stable sort, not plain `std::sort`).
- **`Object.keys(obj)`/`.values(obj)`/`.entries(obj)` added as compile-time macros** (§8) - expanding directly to the object's known field list at compile time, not runtime reflection, so this doesn't reopen the `for...in`-on-plain-objects ban logged elsewhere in this history; it's the direct, safe resolution of "how do these JS globals work given that ban."
- **`std/network` replaces the earlier `std/fetch` + `std/server`, consolidated further and with `fetch` switched to the exact JS shape.** `fetch(url, options)` now matches real JS `fetch` exactly (no more axios-style `.get()`/`.post()`/`.custom()`), and WebSocket/`WebSocketServer` support is added alongside the HTTP server, all backed by uWebSockets internally. Full API and beta-one scope in `STDLIB-README.md`.
- **cp3 confirmed explicitly as a hosted language, not a freestanding/systems one** - every program assumes a real OS underneath (libuv needs kernel-provided `epoll`/`kqueue`/IOCP, the managed heap needs a working allocator, `import native` needs a real dynamic linker). Writing an OS kernel is explicitly out of scope, not a gap to fill later - it would need a genuinely separate freestanding compilation target, not an extension of this one (§1).
- **Static linking confirmed as cp3's default build output** - every bundled library (libuv, fmt, Abseil, uWebSockets, libsodium, TBB, PCRE2, XXHash, CRC32C, Boost.Lockfree) is baked into a single executable by default, not shipped as separate shared libraries. A third-party `import native` dependency may still need dynamic linking if that's the only form the vendor ships (common for closed-source SDKs) - that's a property of the third-party library, not something cp3 works around. Full detail in `CLI-README.md` §1.

- **`parallel.*`'s capture-safety rule fixed twice over, correcting a real self-contradiction and a real data-race gap.** *(Rationale for the non-owning borrow later revised when refcounts became atomic - see the memory-model simplification entry below; the rule itself and the disjoint-index exception are unchanged.)* The rule as previously stated directly contradicted its own `parallel.for` example (which writes to a shared captured array from multiple closures - exactly what the rule said was banned), and separately missed that capturing even an *immutable* managed object still touches its non-atomic refcount from multiple threads, a genuine race independent of the data's own mutability. Fixed: closures never take a managed/refcounted handle on a capture at all - always a non-owning borrow, safe because `parallel.*` is synchronous and the calling scope's lifetime already spans the whole call (same reasoning as the native pointer safety rule, §19); and the one named safe-write exception is writing through `parallel.for`'s own provided loop index, since distinct iterations are provably disjoint by construction - everything else stays a compile error (§13).
- **~~Escape analysis confirmed to require a genuine multi-pass compiler~~ - superseded.** Escape analysis was dropped entirely; see the memory-model simplification entry below. Kept only for the historical trail.
- **Async internals made concrete**: the "C+++ runtime" layer in the async architecture diagram is a compiler-generated Awaiter/Bridge object per suspension point, connecting `co_await` to libuv's callback model - generated automatically per std API, never written by hand. Also added **async inlining**: an `async function` that never actually suspends gets compiled down to a plain function returning an already-resolved `Promise`, skipping coroutine-frame generation entirely - a compiler optimization, not an observable semantic difference (§14).
- **`console.log`'s `Printable` reconciled with `fmt`, fixing a real inconsistency between two earlier decisions.** `console.log(...args)` compiles to a zero-cost C++ variadic template (monomorphized per call site, same mechanism as generics, §16), not a runtime heterogeneous array. Built-in primitives route straight into `fmt` with no `toString()` call at all; user classes may implement `toString()` (an ordinary structural interface) to customize their printed form, with a default representation generated only for classes actually passed to `console.log` somewhere in the program - usage-based, the same reification approach already used for `JSON.parse<T>`, not a `toString()` proactively synthesized for every class regardless of use (`STDLIB-README.md`).

- **Object literals against a declared type get TypeScript-style excess property checking, closing a real gap.** A fresh object literal assigned directly to a typed target (`let x: T = {...}`) is checked for fields `T` doesn't declare, not just missing ones - specifically to catch typos (`valeu` instead of `value`) that plain structural width-subtyping would otherwise silently accept as "one harmless extra field." A value already sitting in a variable and passed along afterward still gets ordinary structural checking (extra fields allowed) - same distinction TypeScript itself draws. Also closes a related question: a `class` with any `private`/`protected` field cannot be satisfied by a bare object literal at all, since there's no constructor call through which to legitimately set an inaccessible field (§15).

- **Module resolution**: follows plain ES module semantics directly - named imports/exports are live bindings, `export * from` re-exports are supported, and circular imports between two `.cp3` files resolve the same way JS/ESM already does (a value referenced before its module finishes initializing is `undefined` at that point, not an error) - no cp3-specific deviation.
- **Cross-thread error propagation from `worker`**: `worker(...)` returns a `Promise` (§14), same as `fs.readFile`/`process.exec` - a thrown error inside a worker file simply rejects that promise, catchable with a plain `try`/`catch` around the `await`. No separate event/handle mechanism; see §13 for the corrected call shape. Superseded a previous draft of this decision that used an event-based "error" handle, which didn't match `worker`'s actual (synchronous-looking, `await`-based) calling convention.
- **Native interop scope for beta one**: `import native` supports free functions and simple, non-templated, non-overloaded classes/structs where every parameter and return type is a primitive, `string`, or array/`Buffer`. Templates, function overloading on the C++ side, and operator overloading are **not supported in beta one** - this is a known, explicit limitation, not an oversight; expanding native interop coverage is post-beta-one work. (Std modules that reach native C++ internally - `fs`, `crypto`, etc. - are hand-written against this same bridge and are unaffected by this limitation, since none of them need templates/overloads to implement their documented signatures.)
- **Qt / Unreal binding surface**: explicitly **out of scope for beta one** entirely. `import "ue5"`/Qt-style bindings remain a real example of what native interop is *for* longer-term, but no specific class-by-class binding surface ships as part of beta one.
- **Generics runtime reification** (for `JSON.parse<T>`): usage-based, not universal - the compiler emits a shape descriptor (field names + type tags) only for the specific classes/interfaces that actually appear as `T` in a `JSON.parse<T>` call somewhere in the compiled program. A type that's never used with `.parse<T>` stays fully compile-time-erased, same cost as a plain C++ template - there is no blanket "all generics become reified" cost.
- **Small operator/statement confirmations**: yes to all, JS-identical - compound assignment (`+=`/`-=`/etc.), nullish/logical assignment (`??=`/`||=`/`&&=`), the exponent operator (`**`), and labeled `break`/`continue` are all in, unchanged from JS.
- **Standard library exact function signatures**: nailed down in `STDLIB-README.md` (companion doc, updated alongside this one for beta one) - no longer just shape/examples.
- **Raw TCP/UDP socket networking beyond HTTP/WebSocket (`std/network`)**: explicitly **deferred past beta one** - not included, not partially started, don't build against it.
- **Async/await backing architecture**: C++20 coroutines (`co_await`/`co_return`), driven by a single libuv event loop - the same library backing Node's own event loop. Never exposed to cp3 code directly. Full detail and diagram in §14.
- **`setTimeout`/`setInterval`/`clearTimeout`/`clearInterval` confirmed as core-language builtins, not a std import** - now with the concrete reason spelled out: they map directly onto libuv's own timer API, which is already linked the moment any program uses `async`/`await` at all, so gating them behind an import wouldn't save any linkage (§14).
- **`Promise<T>` full surface confirmed**: `.then`/`.catch`/`.finally` plus static `Promise.all`/`.race`/`.allSettled`, all built as ordinary cp3-level combinators on top of the same coroutine+libuv foundation - `await` is the primary/everyday form, but it was never meant to be the *only* one (§14).
- **Unhandled Promise rejections terminate the process** (after printing through the CLI-README.md §2 diagnostics subsystem) - a stricter default than Node's historical silent-warning behavior, chosen deliberately (§14).
- **`worker` confirmed as one real OS thread per call, not a separate OS process** - cheaper to spawn, safety comes from the compiler's copy-in discipline rather than OS-level isolation; the accepted tradeoff is that a worker-thread crash can take the whole process down with it (§13).
- **`worker`'s call shape corrected to be Promise-returning** (`await worker(...)`, rejects on a thrown error) - an earlier draft of this doc had it returning an event-emitting handle, which didn't match its own documented (synchronous-looking) calling convention; fixed to match every other async std API (§13).
- **~~Managed references clarified as *not* literally `std::shared_ptr` (custom non-atomic refcount)~~ - superseded.** Managed references are now literally `std::shared_ptr` with standard atomic refcounting; see the memory-model simplification entry below.
- **Coroutine frames use the ordinary C++20 coroutine frame allocation, with reference-type locals as plain refcounted handles inside it** - not a second, separate allocation path. One memory story for the whole language (§10).
- **`std/server` reinstated for beta one, superseding an earlier "removed entirely" decision.** *(Superseded further, not just here - see the `std/network` consolidation entry above, which folds this into a single module alongside `fetch`/WebSocket and switches the backend from llhttp to uWebSockets. Kept here for the historical trail.)* The original removal reasoning (avoid wrapping Drogon - too large, too template-heavy for beta-one's native-interop scope) was correct against Drogon specifically, but dropping the module entirely went too far, since it left the language's own core pitch (native-speed Express-equivalent) with nothing to demonstrate. Fixed architecture: libuv (already mandatory once `async` exists) + llhttp (Node's own HTTP parser, small and proven) + pure-cp3 routing/request/response logic - no heavy framework dependency reintroduced. `std/webview` stays dropped; nothing about this reconsideration applies to it (§20).
- **`std/server` request handlers must isolate their own errors, as an explicit, deliberate carve-out from the global "unhandled rejection kills the process" rule** - still true verbatim under `std/network`'s HTTP server, just no longer under the name `std/server` specifically. One failing request must become a 500 response, not a process-level crash. Stated as a general principle (task-level isolation for any std module multiplexing independent work over one event loop), not a one-off special case (§14).
- **`std/memory`/arena concept considered and rejected.** An earlier draft proposed a `useArena((arena) => {...})` bulk-allocation module for hot-loop performance cases. Cut entirely - not deferred, not "post-beta-one," removed as a concept. If a genuine bulk-allocation need shows up later, it can be reconsidered from scratch rather than resurrected from this draft.
- **Native pointer safety rule formalized** (§19): five concrete pointer shapes (owned / borrowed-with-length / borrowed-for-one-call / untyped `void*` / device pointers) each mapped to a specific safe wrapper pattern, plus a three-level model (safe normal language → safe advanced memory/scoped views → explicit `unsafe`). Replaces a previous one-line assertion ("native pointers are never exposed") with an actual, checkable rule.
- **Raw address access confirmed as fully absent from beta one** - no safe *or* unsafe way to read/write an arbitrary address exists yet; a future `unsafe`-gated version must use a real integer type (`r_'u64'`/`c_'uintptr_t'`) for addresses, never `number` (§12, since `number` is a 64-bit double and can't exactly represent every 64-bit pointer value).
- **`mLet`/`mConst`/`mVar` considered and explicitly rejected, not just never proposed.** The idea (a "shared mutable memory cell" primitive binding, so `mLet x = 5; const y = x; x += 5;` would print `10` via `y`) is already fully achievable today with a plain object wrapper (`let x = { value: 5 }`, §8 - objects already share by reference), at zero new keywords, zero new compiler semantics, and zero added surface for every future reader of cp3 code to learn. A second family of binding keywords for a `.value`-typing savings was judged not worth the ongoing cost (type system + escape analysis + tooling + documentation, all doubled) against the "~99% JS-identical syntax" goal (§1). Logged here specifically so it isn't silently re-proposed later without this context.
- **String encoding decided: UTF-8, not UTF-16** (§9, now retitled "Arrays & Strings"). A real, documented behavior difference from JS, not an implementation footnote: `str[i]` returns a raw byte (UTF-8 is variable-width, 1-4 bytes per character), and `.charAt(i)` - reusing JS's own method name - returns the actual character, walking the encoding correctly rather than doing an O(1) byte lookup. Chosen over UTF-16 for half the memory footprint on common text and 1:1 compatibility with `std::string`/native C++ APIs at every interop boundary (§19) - the same choice Rust and Go both made for the same reasons.
- **`any` in function parameters, secretly compiled to a C++ template, considered and explicitly rejected.** The idea: let `function add(a: any, b: any)` type-check trivially in cp3 and generate a real C++ template underneath. Rejected because it reopens exactly the failure mode CLI-README.md §2's whole diagnostics subsystem exists to prevent - `add(1, "hello")` would type-check fine in cp3, then fail inside the generated C++ template with an opaque, un-mapped instantiation error, since the cp3 frontend would have already waved the call through without knowing whether the eventual instantiation is even valid. It also directly contradicts "static typing, no `any`" (§1) by reintroducing dynamic-style typing under a different name. The underlying use case is already fully solved, more safely, by generics (§16) - `function add<T>(a: T, b: T): T` gives the same call-site flexibility with real cp3-level type-checking at the call, catching `add(1, "hello")` before any C++ is ever generated. Logged here so `any`-as-templates isn't silently re-proposed later without this context.
- **Explicitly banned JS patterns, closing a real gap**: `delete obj.prop`, adding an undeclared property to an object, `arguments`, and `for...in` on a plain object/class instance are all compile errors now, not silent gaps a JS developer would otherwise discover the hard way (§8). This also directly resolves a previously-open question - how do plain objects work as dynamic dictionaries (TS's `{ [key: string]: T }`) - with a firm answer: they don't; `Map<K, V>` is the only supported mechanism for genuinely dynamic keys, since a cp3 object's shape is fixed at compile time the same way a C++ struct's is (§15).
- **Confirmed explicitly: cp3's own generics compile to real C++ templates**, and this does not contradict native-interop bindgen excluding templates (§19/CLI-README.md §4) - the two are different problems. Bindgen fails on *someone else's* existing C++ templates because it has no way to know what concrete type an unfilled blueprint should be instantiated with. cp3's own generics don't have that ambiguity, because the cp3 frontend already resolved the concrete type at every call site through its own type-checking, before any C++ is emitted at all (§16).

- **Memory-model simplification - escape analysis, non-atomic refcounts, and auto-weak all dropped (§10, §11, §12, §13).** After a design review, the original "escape analysis first, managed references only as a fallback" model was judged to be carrying far more machinery than the problem needed, and the spec now says:
  - **Reference types (objects, arrays, class instances, and so on) are always heap-allocated and reference-counted; value types (primitives, enums) are plain stack values.** No escape-analysis pass decides where something lives. C++ RAII (smart-pointer destructors at scope end) does all cleanup, so memory management no longer requires a whole-function analysis pass or an IR just for lifetimes. *(The compiler still needs a real type-checking frontend for structural interfaces, excess-property checks, generics, source maps and the rest - this entry is about memory management only.)* Rejected rationale for the old design: JS code shares objects so heavily that most objects would have been promoted to the heap anyway, so "stack by default" bought little for objects while costing a lot of compiler complexity.
  - **Refcounts are atomic - managed references are literally `std::shared_ptr`.** The old custom non-atomic pointer saved a few nanoseconds per operation but forced the compiler to prove no managed value ever crossed a thread boundary (including through native code, which it cannot see into). The cost was judged negligible for cp3's target workloads. Atomic counts do not make object *contents* thread-safe, so `worker` keeps its deep-copy-in rule and `parallel.*` keeps its borrow-only captures and single disjoint-index write exception (§13).
  - **Every cycle is broken by an explicit `weak`; auto-weak inference from class declaration order is gone.** The old rule silently flipped strong/weak when classes were reordered, a leak-by-refactor hazard. New syntax: a `weak` field modifier on declared fields (`weak parent: Parent`); the existing `{ weak: value }` wrapper is kept for object-literal links (§11).
  - **Escape analysis is not on the roadmap.** Stated like the `std/memory` and `mLet` entries: removed as a concept, not deferred. If a real allocation-cost problem shows up later, it should be reconsidered from scratch with measurements rather than resurrected from the old draft.

- **`let`/`var`/`const` confirmed non-atomic; assigning to a captured `let`/`var` inside `parallel.*` is an explicit compile error (§13).** The idea of making `let`/`var` atomic so parallel closures could update them was considered and rejected (everyone pays the cost, atomics don't make check-then-update sequences safe, and `std::atomic` can't cover strings/arrays/objects). `parallel.reduce` and the disjoint loop-index write remain the only sanctioned ways to combine or write results. Logged so it isn't silently re-proposed without this context.

- **No separate `struct`/value type - `class` is the only user-defined type, with JS reference semantics (§10).** Decision: **match JavaScript.** In JS, objects are always reference types on the heap, and only primitives (`number`/`string`/`boolean`) are value types; cp3 does the same, so no `struct` keyword is added. The accepted consequence is stated plainly because it is a real tradeoff, not an oversight: a small value-like record (a point, a color, a vector) is heap-allocated and reference-counted exactly as a JS object literal would be - there is no zero-allocation, copy-semantics user type, and this is intentional JS parity, not a gap left to fill.

- **Closure-capture rule confirmed as the JS-identical, analysis-free rule (§10).** A `let`/`var` captured by *any* closure lives in a shared, reference-counted cell, so the enclosing function and every closure that captured it observe the same variable - exactly like JS; `const` and never-reassigned captures are copied. It is purely syntactic (captured + reassignable → shared cell) and needs no escape analysis. This is the confirmed replacement for the dropped "promote only if the closure escapes" rule.

- **No `Atomic<T>` escape hatch - `parallel.reduce` stays the only sanctioned way to combine results (§13).** The suggestion was considered and rejected: it would reverse §13's guarantee that cp3 never exposes locks, mutexes, or atomics to users, and atomics would not even solve the motivating case (a check-then-update sequence still races). Nothing in the spec defines it; it is explicitly not part of beta one.

- **`weak` read semantics and cycle diagnostics decided (§11).** Reading a `weak` field after its target has been destroyed yields `T | undefined` (the ordinary optional type; the caller narrows before use). The compiler emits a **non-fatal warning** when it can prove a reference-type cycle has no `weak` edge - a "this may leak" hint. It still never *infers* or auto-assigns `weak`; warning is not inference.

**Still genuinely open / not required for beta one** (implementation-detail engineering, not spec gaps - safe to proceed against the rest of this doc while these get worked out during the build):

- [ ] ~~Escape analysis implementation details~~ - removed; escape analysis is no longer part of the design (§10, §23).
- [ ] Native interop type-mapping details beyond the beta-one scope specified above (templates, overloads, operator overloading) - post-beta-one. Compiler engineering to actually build this (the `cp3 native-install` bindgen tool) is tracked in `CLI-README.md`.

**Guiding principle:** define C+++ semantics independently first, and treat C++ as *only* the initial implementation backend - this keeps the language decoupled from C++'s complexity while still reusing its mature, battle-tested compiler infrastructure.
