# 16. Generics

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

**This is a real C++ template under the hood, not a disguise for something else** - `identity<T>` genuinely compiles to a C++ function template, instantiated per concrete type used, same as any hand-written C++ template. This might look like it contradicts [§19](19-c-cpp-interoperability.md)/[CLI-README.md §4](../cli-tooling/04-package-manager.md)'s native-interop scope excluding templates for beta one, but the two situations aren't the same problem: bindgen's issue is reading *someone else's already-written* C++ template and having no way to know what concrete type it should be instantiated with, since a template is a blueprint with the type left unfilled. cp3's own generics don't have that problem - the cp3 frontend already knows the concrete type at every call site, because it type-checked the call itself before ever emitting a line of C++. There's no guessing involved on cp3's own side; the guessing problem only exists at the boundary where cp3 has to reverse-engineer someone else's C++.

## Other direct 1:1 mappings

The following exist identically in both TS and C++, so they need no separate design decision - just a direct compiler mapping: **getters/setters** (`get`/`set`), **function overloading** (C++ supports this natively), **tuple types** (`[string, number]` → `std::tuple`), **ternary (`? :`)**, **bitwise operators**, and **default parameter values**.

## Explicitly skipped for v1

**Generators (`function*`/`yield`)** and **decorators** (`@Decorator`) both exist in TS but have no native C++ equivalent - generators would need C++20 coroutines wired up specifically, decorators would be entirely custom-built. Both are deliberately **deferred**, not part of v1.

