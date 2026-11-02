# CP3 Roadmap

This file records what each release is changing, what is deliberately not
included, and why. It is the public checklist for the current release.

## v0.2.0 - TypeScript compatibility

Status: **in progress**

Theme: ordinary TypeScript class and error code should work in CP3, while the
compiler keeps emitting plain native C++ with no hidden runtime machinery.

### 1. Error model

- Add the built-in hierarchy:
  `Error`, `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`,
  `EvalError`, `URIError`, `AggregateError`.
- Every error has `message`, `name`, `stack`, and `cause`.
- `new Error(...)` and the callable form `Error(...)` work.
- The TypeScript/JavaScript options object is supported:
  `new Error("boom", { cause: originalError })` and
  `Error("boom", { cause: originalError })`.
- `AggregateError` accepts its errors iterable plus the same options:
  `new AggregateError(errors, "boom", { cause: originalError })`.
- Error options are ordinary constructor arguments, not runtime metadata:
  `cause` is stored on the error object and is available as `e.cause`.
- **Only `Error` and its subclasses may be thrown.**
  - `throw new Error("boom")` - allowed.
  - `throw new TypeError("bad value")` - allowed.
  - `throw 404`, `throw "missing"`, `throw {}` - compile error.
  - The diagnostic must say that TypeScript allows throwing any value, but
    CP3 does not, because CP3 has one catch model and one runtime contract.
- `catch (e)` binds **`Error` directly**. No narrowing is required:
  `e.message`, `e.name`, `e.stack`, and `e.cause` work.
- Runtime and standard-library failures throw `Error` objects, never raw
  strings, numbers, or `std::exception` values.
- Promise rejections and worker failures use the same `Error` contract.

### 2. TypeScript classes

The target is practical TypeScript class behavior:

- TypeScript method syntax: `greet(): string { ... }` without a `function`
  keyword.
- Constructor parameter properties:
  `constructor(public name: string, private id: number) {}`.
- `extends` with native C++ inheritance, `super(...)` constructor calls, and
  `super.method()`.
- `implements` conformance checks.
- `readonly` enforcement.
- `override` checks.
- `abstract` classes and methods.
- Getters and setters.
- `public`, `private`, and `protected` access checks.
- `static` members.
- `#private` fields and methods.
- `static { ... }` initialization blocks.
- Class expressions: `const C = class { ... }`.
- Zero-overhead `instanceof`.
- TypeScript-compatible constructor and method behavior for the supported
  surface.

Existing CP3-only class syntax (`function greet() {}`) and CP3-only
extensions such as `weak` are **not** kept in v0.2.0: this release moves to
the TypeScript spelling and the TypeScript compatibility target.

### 3. Compile-time decorators

Decorators are compile-time macros, not runtime decorators:

- `@Name` names a `comptime function`.
- The function receives the decorated class or method AST.
- It may validate the declaration and raise compile errors.
- It may inject plain CP3 code: fields, methods, constructor checks, or static
  registration.
- It runs only inside `cp3c`. The emitted binary contains only the injected
  plain CP3 code.
- No runtime metadata tables.
- No `Reflect` / `Reflect.metadata`.
- No wrapper functions around decorated methods.
- No evaluation at class-definition time.
- A decorator that wants per-call behavior (logging, timing) must inject
  statements into the method body at compile time.
- Replacing a method with an indirect-call wrapper is a compile error.

### 4. Compile-time `extends`

- `extends Name` keeps native C++ inheritance and has zero runtime cost.
- `extends expr` is accepted only when `expr` is comptime-evaluable:
  constants, `comptime` calls, and conditionals over comptime values.
- The compiler folds the expression to a concrete class and emits ordinary
  inheritance.
- Any other expression is a compile error:
  `extends expression must resolve at compile time`.
- No runtime base-class lookup, hidden class objects, or dictionary objects.

### 5. Zero-overhead `instanceof`

- `x instanceof ClassName` lowers to a static type-id comparison.
- There is one static id per class, decided by the compile-time hierarchy.
- No additional runtime type-info tables.
- `catch (e)` does not need `instanceof`; the binding is already `Error`.

### 6. Imports and packages

Known defects fixed in v0.2.0:

- Cross-module exported values and types must propagate into the importing
  unit, so type checking and code generation use the real declaration.
- Package imports must resolve and link, not fall through to codegen as an
  unknown ambient name.
- `.cp3pak` archives must record a `main` path that actually exists inside the
  archive (`index.cp3`, not `src/index.cp3`).
- `.cp3/lock.json` must store project-relative `resolved` and `target` paths,
  not machine-specific absolute paths.

### 7. TypeScript-only diagnostics

Whenever CP3 rejects something that TypeScript accepts, the diagnostic must
say all three things:

1. what the TypeScript feature is,
2. that TypeScript allows it but CP3 does not, and
3. why CP3 rejects it (static native output, no runtime metadata, no hidden
   wrappers, no dynamic class resolution, and so on).

Examples:

- Throwing a number or string: TypeScript permits any thrown value; CP3 only
  permits `Error` objects.
- A runtime decorator: TypeScript can evaluate decorators at runtime; CP3
  decorators are compile-time macros only, so the binary stays metadata-free.
- A dynamic `extends` expression: TypeScript can resolve a base class at
  runtime; CP3 requires a compile-time class so inheritance stays native C++.
- A TypeScript type-level keyword such as `keyof`, `infer`, `satisfies`,
  `infer`, or `asserts`: TypeScript computes types at compile time; CP3's type
  surface is the static native subset and rejects it with the reason.
- Other TypeScript-only type features (conditional types, mapped types,
  template-literal types, indexed-access types, `unique symbol`, variance
  annotations, `const` type parameters, `namespace`/`module` declarations):
  rejected with the same explicit explanation.

The rule is: never emit a generic "syntax error" when the construct is a known
TypeScript feature that CP3 intentionally does not support.

### 8. Verification

- Generate C++ for a decorated class and a comptime-`extends` class.
- Diff against hand-written equivalent classes.
- Output must be identical except for the injected plain CP3 code.
- Confirm that generics monomorphisation, interface dispatch, and object-churn
  behavior do not regress against the v0.2 option-1 baseline.
- Run the full compiler and runtime test suites.

### 9. Out of scope for v0.2.0

These are rejected with the diagnostic rule above:

- TypeScript runtime decorators.
- `Reflect.metadata`.
- Runtime-dynamic `extends`.
- Any feature that needs runtime type metadata or wrapper functions to work.

## v0.3.0 - Performance upgrades

Status: **coming soon**

The v0.3 line is reserved for performance work. It will start after the v0.2.0
TypeScript compatibility surface is complete and verified. The plan will be
published here before implementation starts.

Areas under consideration:

- Faster compilation for large module graphs.
- Better generated-code throughput and lower allocation churn.
- Improved monomorphisation and interface-dispatch code generation.
- Further escape-analysis and stack-allocation wins.
- Lower-overhead class construction and object layout.
- Faster startup and smaller native binaries.
- More performance regression coverage in the test suite.
