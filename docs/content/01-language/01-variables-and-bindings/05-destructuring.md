---
title: Destructuring declarations
chapter: Chapter 1 · The language
---
# Destructuring declarations

Destructuring binds local names from an array-like value or an object-shaped value. CP3 checks the source shape and the type of each extracted value; destructuring is not a way to bypass static checking.

## Array patterns

Use an array pattern when the source is a tuple or array and positions matter:

```ts
const point: [number, number] = [12, 8];
const [x, y] = point;
```

The names are assigned according to position. A tuple gives the analyzer positional types, so `x` and `y` are numbers here. A general array has one element type rather than a distinct type for each position.

Array patterns may omit positions and may bind the remaining tuple items with a rest pattern:

```ts
const rgb: [number, number, number] = [30, 80, 120];
const [red, , blue] = rgb;
const [first, ...tail] = rgb;
```

The rest binding collects the remaining elements after the consumed positions. The test suite includes tuple rest cases, including a rest pattern with no remaining entries. Treat the result as a new collection of the remaining items; it is not a reference alias for the original tuple tail.

Array destructuring requires an array or tuple compatible source. A string is not accepted as an array source merely because it can be iterated in some JavaScript contexts. The `array_source_error` case pins this rejection.

## Object patterns

Use an object pattern to bind fields by name:

```ts
const user = { name: "Ada", active: true };
const { name, active } = user;
```

The shorthand `{ name }` means “read the source's `name` field and bind it to a local also named `name`.” A renamed property makes the source and local names explicit:

```ts
const { name: displayName } = user;
```

Here `name` is the source property and `displayName` is the new binding. The pattern does not rename or mutate the source object.

For an optional source member, the extracted local retains the optional type:

```ts
interface Person {
  name: string;
  age?: number;
}

function label(person: Person): string {
  const { name, age } = person;
  if (age === undefined) return name;
  return `${name} (${age})`;
}
```

Handle the absent case according to the type. Do not assume every optional property has a value just because the pattern binds a local name.

## Rest properties

An object rest binding collects the source fields not explicitly selected:

```ts
const person = { name: "Ada", age: 36, city: "London" };
const { age, ...profile } = person;
```

`profile` contains the remaining known fields. Interface and structural object cases are covered in `tests/cases/destructuring`, including rest in a function parameter. The rest binding has a shape derived from the source members left out of the pattern.

Use a rest pattern when the remaining fields are part of the intended value. It creates a new object-shaped result; it is not an aliasing promise that writes to the result will update the original object.

## Literal computed property names

The parser and analyzer support a computed property pattern for literal keys:

```ts
const record = { x: 7 };
const { ["x"]: value } = record;
```

The key is known at compile time. A general variable key is not equivalent to an arbitrary dynamic property lookup; the current tests include a computed-key diagnostic case. Use an ordinary named property when the key is statically known.

## Destructuring in parameters

Object patterns are also accepted in function parameter declarations when the source type is annotated:

```ts
interface Person {
  name: string;
  age?: number;
}

function show({ name, ...rest }: Person): undefined {
  console.log(name);
  console.log(rest.age);
}
```

The annotation types the input object and therefore the extracted bindings. An absent annotation may leave the analyzer without enough shape information, just as with an ordinary untyped parameter.

## Declarations without an initializer

CP3 currently lowers a destructuring declaration with no initializer to bindings initialized with `undefined`, matching the supported `let name;` behavior:

```ts
let [left, right];
console.log(left);  // undefined
console.log(right); // undefined
```

This behavior is unusual enough to call out explicitly. It does not imply that every pattern source can be omitted in every context. Prefer an explicit type and initializer when the value will be used as a real array or object:

```ts
let [first, second]: [number, number] = [0, 0];
```

The exact accepted annotation forms are bounded by the parser and test corpus. The `destructure_no_initializer` case records the current default behavior.

## Type errors and missing fields

The analyzer checks that every requested property exists on the known source shape. A typo is diagnosed at compile time:

```ts
const user = { name: "Ada" };
// const { email } = user; // missing-property error
```

It also checks source compatibility and extracted-value types. Relevant negative tests cover a missing property, a non-array source, an unsupported computed key, and a rest parameter type mismatch. Fix the source shape or pattern instead of casting away the diagnostic.

## Mutability of the new bindings

Destructuring does not choose mutability by itself. Prefix the declaration with `const`, `let`, or `var` and follow the normal binding rules:

```ts
const { name } = user; // name cannot be rebound
let { age } = user;    // age binding may be reassigned if its type allows
```

The extracted value may itself be a reference. A constant local referring to a mutable object still prevents rebinding only; see [Mutability and immutability](./03-mutability.md).

## Destructuring versus direct access

| Form | Good fit |
| --- | --- |
| `const { id } = item` | A few named fields are used repeatedly in a local scope. |
| `item.id` | Keeping ownership and origin visible is more important than shortening a name. |
| `const [head, ...tail] = tuple` | Position and tuple shape are part of the operation. |
| `item[key]` | The key is genuinely dynamic and the type system supports that indexed access. |

Destructuring is a readability feature. It does not guarantee a faster representation than member access. The generated C++ remains an implementation detail unless you are investigating performance.

## Related pages and tests

- [Bindings and declarations](./01-bindings.md)
- [Types and inference](./02-types.md)
- [Scope and initialization](./04-scope-and-initialization.md)
- Test family: `tests/cases/destructuring`
