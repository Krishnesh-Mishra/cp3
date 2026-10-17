# Built into the language - no import needed

These are core language features, not standard library modules, because they operate on core types (array, string) or the core runtime (the event loop) that every program uses constantly:

- Array methods - push, pop, map, filter, reduce, sort, find, includes, flat, etc.
- String methods - split, trim, replace, padStart/padEnd, toUpperCase/toLowerCase, etc.
- Regex - native pattern literal syntax (/pattern/flags), like JS - not an imported module.
- setTimeout / setInterval / clearTimeout / clearInterval - global builtins, no import, tied directly to the language's own event loop ([README.md §14](../language-spec/14-async.md)), same as JS.
- sleep(ms) - a global builtin returning `Promise<void>`, a thin convenience wrapper over `setTimeout` for the extremely common `await sleep(1000)` pattern. Not its own mechanism - same event loop, same guarantees.
- parseInt(str) / parseFloat(str) - global builtins (matching JS's own placement as globals, not string methods), backed directly by `std::strtol`/`std::strtod`. Return `number` (always a 64-bit double, [README.md §3](../language-spec/03-type-system.md)), same as every other numeric value in the language.
- Object.keys(obj) / Object.values(obj) / Object.entries(obj) - compile-time macros expanding directly from a plain object's known field list, not runtime reflection ([README.md §8](../language-spec/08-variable-bindings.md) - this is the direct resolution of "how do these work given `for...in` on plain objects is banned").

