---
title: Language APIs
---
# Language APIs

The language-level surface: the value APIs and global functions that exist without any import. If a name here works, you can call it in every CP3 program.

## Pages

- [String](./string.md) — string operations, methods and UTF-8 behavior
- [Number](./number.md) — `number` rules, native integer conversions, parsing and formatting
- [Array](./array.md) — the `Array<T>` / `T[]` surface
- [Object](./object.md) — the `Object.*` builtin: `assign`, `fromEntries`, `groupBy`, `is`, `freeze`, `keys`…
- [Typed arrays](./typed-arrays.md) — the fixed-width families, `ArrayBuffer`, `SharedArrayBuffer`, `DataView`
- [Promise](./promise.md) — `Promise<T>`, combinators, `withResolvers`, adoption
- [Collections](./collections.md) — `Map`, `Set`, and the `std/collections` ambient surface
- [EventEmitter](./event-emitter.md) — typed `EventEmitter<Events>`
- [parallel.*](./parallel.md) — `parallel.for`, `parallel.reduce`, `parallel.run`
- [Global functions](./global-functions.md) — `parseInt`, `parseTo`, `isNaN`, `structuredClone`, timers and friends

For imported modules, see the [standard library](../standard-library/00-overview.md) section instead.
