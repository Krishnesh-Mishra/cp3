# 15. Classes

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

## Access modifiers

`public` / `private` / `protected` - matches TS exactly, mapped directly to C++'s native access-modifier sections.

## Static members

```cp3
class Counter {
  static count: number = 0;
  static function increment(): void { Counter.count = Counter.count + 1; }
}

Counter.increment();
console.log(Counter.count);
```

Maps directly to C++'s native `static` members.

## Inheritance - single, TS-style

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

## Interfaces - structural, TS-style

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

## Object literals against a declared type - excess property checking, decided

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

## Abstract classes/methods

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

## `this` binding

Methods are **always correctly bound** to their instance, regardless of how they're called or detached - this is the natural, free C++ behavior, and deliberately *not* JS's quirk where a detached method call can lose its `this` (the reason JS/TS developers often reach for arrow-function class fields as a workaround). Reproducing JS's broken behavior would cost real, ongoing compiler complexity for no benefit, so cp3 doesn't.

