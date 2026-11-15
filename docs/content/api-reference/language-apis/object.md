---
title: Object
---
# Object

`Object.*` is a language builtin — no import. It operates on object literals and class instances with statically known shapes; CP3 has no runtime property table for plain objects. `Record<string, V>` canonicalizes to `Map<string, V>`. See the [global builtins guide](../../language/99-global-builtins.md) and [arrays and objects](../../language/04-values-and-objects/01-arrays-and-objects.md) for the full model.

## Field access and enumeration

| Member | Signature | Notes |
| --- | --- | --- |
| `Object.keys` | `Object.keys(object: object): string[]` | Field names of a statically known shape, in field order. |
| `Object.values` | `Object.values(object: object): V[]` | Field values; `V` is the unified field type. |
| `Object.entries` | `Object.entries(object: object): [string, V][]` | `[name, value]` pairs in field order. |
| `Object.fromEntries` | `Object.fromEntries(entries: [string, V][]): Map<string, V>` | Builds the canonical `Map<string, V>` (the `Record<string, V>` type). Keys must be strings. |
| `Object.groupBy` | `Object.groupBy(list: T[], callback: (item: T, index: number) => string): Map<string, T[]>` | Groups items by the callback's string key (the canonical `Record<string, T[]>`). For non-string keys use `Map.groupBy` — see [Collections](./collections.md). |

## Merging and copying

| Member | Signature | Notes |
| --- | --- | --- |
| `Object.assign` | `Object.assign(target, ...sources): target` | Copies fields from every source into `target` and returns it. `null`/`undefined` sources are ignored. A fresh literal target becomes a new merged object (JS key order, later sources win); a reused binding target is mutated in place and keeps its declared shape — a source with a new field is a compile error. For new shapes prefer `{...t, ...u}`. |

Freeze is tracked only where the checker can see the `Object.freeze(...)` form directly: `Object.assign(Object.freeze(o), ...)` throws a catchable `TypeError` at runtime. A frozen object reached through a variable is not tracked.

## Equality and identity

| Member | Signature | Notes |
| --- | --- | --- |
| `Object.is` | `Object.is(a, b): boolean` | Same-value equality: `NaN` equals `NaN`, `-0` is not `+0`; reference handles compare by identity; strings and booleans compare by value. |

## Immutability flags

Freeze, seal, and preventExtensions are tracked syntactically; nested values are not frozen (not a deep freeze). Pass values explicitly where the guarantee matters.

| Member | Signature | Notes |
| --- | --- | --- |
| `Object.freeze` | `Object.freeze(o: T): T` | Marks `o` frozen (frozen implies sealed and non-extensible). Returns `o`. |
| `Object.seal` | `Object.seal(o: T): T` | Marks `o` non-extensible and non-configurable; writable fields stay writable. Returns `o`. |
| `Object.preventExtensions` | `Object.preventExtensions(o: T): T` | Marks `o` non-extensible; existing fields stay writable and configurable. Returns `o`. |
| `Object.isFrozen` | `Object.isFrozen(o: T): boolean` | True when `o` is frozen. |
| `Object.isSealed` | `Object.isSealed(o: T): boolean` | True when `o` is sealed or frozen. |
| `Object.isExtensible` | `Object.isExtensible(o: T): boolean` | False when `o` is frozen, sealed, or prevented. |

## Prototype (absence of)

CP3 objects have no prototype chain.

| Member | Signature | Notes |
| --- | --- | --- |
| `Object.getPrototypeOf` | `Object.getPrototypeOf(o: T): null` | CP3 objects have no prototype; always `null`. |
| `Object.setPrototypeOf` | `Object.setPrototypeOf(o: T, proto: null): T` | Only `null` is accepted as the prototype; returns `o`. |

## Object literals and spread

- Object literals carry reference semantics; property types are inferred from initializers; missing and excess properties are checked against declared shapes.
- Object spread `{ ...base, y: 3 }` contributes the statically known fields of `base` and overrides earlier keys. It is not a runtime deep clone — nested objects stay shared references.
- Index signatures (`[name: string]: T`) exist on interfaces and object type literals, not on arbitrary classes; for fully dynamic keys use `Map<K, V>`. Plain-object subscript is rejected (`Map<string, V>` / `Record<string, V>` subscript is supported — see [Collections](./collections.md)).
- `for...in` over plain objects is rejected (`CP3805`).

## Related pages

- [Global builtins guide](../../language/99-global-builtins.md) — full `Object.*` table and freeze notes
- [Collections](./collections.md) — `Map`, `Set`, `Object.fromEntries`, `Object.groupBy`
- [Arrays and objects](../../language/04-values-and-objects/01-arrays-and-objects.md)
