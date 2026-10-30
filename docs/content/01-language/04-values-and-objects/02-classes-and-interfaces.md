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

## Inheritance and overrides

The compiler supports the class inheritance forms implemented in the class model. Multiple class inheritance is rejected. Overrides are checked against the base member shape, and duplicate or invalid declarations receive diagnostics. Interface implementation is distinct from class inheritance: an interface supplies a contract, not a base object layout.

## Abstract classes

The analyzer tracks abstract class and member forms and rejects instantiation of a class that is not concrete. Follow the diagnostic’s member list when completing an implementation.

## Runtime identity and storage

Class instances are reference-shaped values in the ordinary runtime representation. `new C()` normally lowers to `Ref<C>` and `makeRef`, which uses `std::allocate_shared` with CP3's pooled allocator. Under a conservative compilation-unit gate, some eligible classes use `LocalRef<C>` and `makeLocalRef` instead. A separate narrow escape-analysis pass can replace selected local objects with stack/value storage; see [Escape analysis](#02-runtime/02-escape-analysis). A source declaration alone does not imply either representation.

## Avoid assuming JS class parity

CP3 does not inherit every dynamic JavaScript class feature. The type checker rejects unsupported member patterns and decorators. Use declared fields and methods, and let `cp3 check` tell you whether a form is part of the current language surface.
