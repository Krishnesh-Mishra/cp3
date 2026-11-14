---
title: Classes and interfaces
chapter: Chapter 1 · The language
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

The run-mode fixtures under `tests/cases/classes/01/params` (`private_hash_field_ok`) and `tests/cases/classes/01/static` (`static_block_ok`, a `static #hidden` field) exercise both forms and pass. The `private` modifier and the `#` spelling share the `isPrivate` member fact in the compiler, so access-modifier rules apply to either spelling.

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

The compiler verifies that an implementing class supplies the required members with compatible types. It reports a missing member and the class declaration that failed to satisfy the interface.

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

The compiler supports the class inheritance forms implemented in the class model. Multiple class inheritance is rejected. Overrides are checked against the base member shape, and duplicate or invalid declarations receive diagnostics. Interface implementation is distinct from class inheritance: an interface supplies a contract, not a base object layout.

## Abstract classes

The analyzer tracks abstract class and member forms and rejects instantiation of a class that is not concrete. Follow the diagnostic’s member list when completing an implementation.

## Runtime identity and storage

Class instances are reference-shaped values in the ordinary runtime representation. `new C()` normally lowers to `Ref<C>` and `makeRef`, which uses `std::allocate_shared` with CP3's pooled allocator. Under a conservative compilation-unit gate, some eligible classes use `LocalRef<C>` and `makeLocalRef` instead. A separate narrow escape-analysis pass can replace selected local objects with stack/value storage; see [Escape analysis](../../internal-engineering/02-escape-analysis.md). A source declaration alone does not imply either representation.

## Avoid assuming JS class parity

CP3 does not inherit every dynamic JavaScript class feature. The type checker rejects unsupported member patterns and decorators. Use declared fields and methods, and let `cp3 check` tell you whether a form is part of the current language surface.
