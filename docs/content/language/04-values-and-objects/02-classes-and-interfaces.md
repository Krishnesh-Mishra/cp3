---
title: Classes and interfaces
---
# Classes and interfaces

Classes define object shapes and behavior. Interfaces define the methods and fields a value must supply to satisfy a contract. The semantic analyzer checks class declarations, member access, inheritance limits, and interface conformance.

## A class

```ts
class Counter {
  value: number;

  constructor(initial: number) {
    this.value = initial;
  }

  increment(): number {
    this.value = this.value + 1;
    return this.value;
  }
}

const counter = new Counter(0);
```

Fields have declared types. A constructor initializes instance state; methods use `this` to refer to the current instance. Constructor parameters and return values are checked like ordinary function types.

A field declared without an initializer and never assigned in any constructor is a compile error (`CP3709`); write `field!: Type` to defer definite assignment, or give the field an initializer.

## Optional class fields

A field declared with `?` is optional: it defaults to `undefined`, reads as `T | undefined`, and assigning `undefined` to it is fine:

```ts
class Item {
  name: string;
  note?: string;

  constructor(name: string) { this.name = name; }
}

const it = new Item("x");
if (it.note !== undefined) {
  console.log(it.note.length);
}
```

## Private members

A field or method whose name begins with `#` (e.g. `#value: number`) is a private member. It is accepted by the compiler, participates in ordinary member checking with the same access boundary as the `private` modifier — code outside the class cannot read it — and is excluded from virtual dispatch. Both forms work for instance members and for `static` members:

```ts
class Counter {
  #value: number = 1;

  increment(): undefined {
    this.#value = this.#value + 1;
  }

  current(): number {
    return this.#value;
  }
}
```

The `private` modifier and the `#` spelling share the same access rules; either spelling may be combined with `static`.

## Interfaces

```ts
interface Named {
  name: string;
  display(): string;
}

class Person implements Named {
  name: string;
  constructor(name: string) { this.name = name; }
  display(): string { return this.name; }
}
```

The compiler verifies that an implementing class supplies the required members with compatible types. It reports a missing member (`CP3701`) and the class declaration that failed to satisfy the interface. Interface implementation is distinct from class inheritance: an interface supplies a contract, not a base object layout.

An interface declared in one module and imported in another works in annotation positions, conformance checks, and member access in the importing file:

```ts
// shapes.cp3
export interface Point { x: number; y: number }

// main.cp3
import { Point } from "./shapes.cp3";
const p: Point = { x: 1, y: 2 };
```

Interfaces and object type literals accept index signatures (`[name: string]: T`, `[n: number]: T`); see [Object shapes](../01-variables-and-bindings/02-types.md).

## Object literals and structural shapes

An object literal may satisfy a declared interface when its properties have the expected types. The checker has explicit excess-property and missing-property diagnostics for typed object literals. A value first stored in a variable may be checked differently from a fresh literal because excess-property checks are designed to catch likely mistakes at construction sites.

## Accessors

A class may declare TS-style accessors:

```ts
class Temperature {
  private raw: number = 0;

  get celsius(): number {
    return this.raw;
  }

  set celsius(value: number) {
    this.raw = value;
  }
}
```

The getter's declared return type governs reads of the property; the setter receives the assigned value as its one parameter. `this` is in scope in both bodies, and the bodies are ordinary method bodies. An accessor pair is a member of the class, not a backing field — there is no separate implicit `field`-plus-accessor duality. Reads and writes through accessors go through the accessor implementation.

## Static blocks

A class body may contain one or more `static { ... }` blocks:

```ts
class Registry {
  static all: Map<string, number>;

  static {
    Registry.all = new Map();
  }
}
```

A static block runs its statements once; `this` is not in scope inside it — static members are reached through the class name. It is checked in the enclosing class scope.

## Constructor parameter properties

A constructor parameter with an access modifier (or `readonly`) declares an instance field and initializes it from the argument:

```ts
class Point {
  constructor(public x: number, readonly y: number) {}
}
```

`new Point(1, 2)` creates a `Point` with `x = 1` and `y = 2`; `p.y = 3` outside the constructor is an error because `readonly` fields cannot be assigned after construction. A parameter property must be a simple identifier (not a destructuring pattern) and needs a type annotation or a default value; it may not duplicate an existing member.

## Inheritance and overrides

The compiler supports the class inheritance forms implemented in the class model. Multiple class inheritance is rejected. Overrides are checked against the base member shape, and duplicate or invalid declarations receive diagnostics.

Overriding a method changes what calls through a base handle reach: a method with an override anywhere in the program is reached through the base handle's static type (interface handles included), which is the dispatch TypeScript programmers expect:

```ts
class Animal {
  greet(): string { return "base-greet"; }
}

class Dog extends Animal {
  greet(): string { return "dog-greet"; }
}

const a: Animal = new Dog();
console.log(a.greet()); // dog-greet
```

`super.method()` remains a base-qualified call. A class that returns `this` from a method is a supported pattern, including across module boundaries.

## Abstract classes

The analyzer tracks abstract class and member forms and rejects instantiation of a class that is not concrete (`CP3413`). A concrete subclass must implement every abstract member of its bases (`CP3701` otherwise). Follow the diagnostic's member list when completing an implementation.

## Inheritance across modules

An `export class` declared in one module and imported in another is nominal, not just structural: `new Imported()`, `instanceof`, extends, implements, and annotation positions all work in the importing file, including for generic classes:

```ts
// frame.cp3
export class Frame { id: number; constructor(id: number) { this.id = id; } }

// main.cp3
import { Frame } from "./frame.cp3";
const f = new Frame(7);
console.log(f.id);
```

## Strong reference cycles

A strong reference cycle among ordinary (cycle-capable) classes — two class instances each holding the other — is reclaimed automatically when the whole island becomes unreachable; it is not an error to write one. `weak` fields remain supported as a manual, earlier-release escape hatch with the same semantics as before, now an optimization rather than a requirement. Classes whose fields the compiler cannot prove reclaimable keep the previous `CP3401` diagnostic, in which case a `weak` edge is required in source. The semantics of cycles and `weak` are documented with the runtime: see [No garbage collector](../../internal-engineering/02-no-garbage-collector.md).

## Runtime identity and storage

`new C()` creates a reference-shaped class instance; two bindings to the same instance observe each other's mutations. That is the only storage fact an application needs from this page.

## Avoid assuming JS class parity

CP3 does not inherit every dynamic JavaScript class feature. The type checker rejects unsupported member patterns and decorators. Use declared fields and methods, and let `cp3 check` tell you whether a form is part of the current language surface.

## Related pages

- [Bindings and declarations](../01-variables-and-bindings/01-bindings.md)
- [Types and inference](../01-variables-and-bindings/02-types.md)
- [Mutability and immutability](../01-variables-and-bindings/03-mutability.md)
- [Global builtins](../99-global-builtins.md)
