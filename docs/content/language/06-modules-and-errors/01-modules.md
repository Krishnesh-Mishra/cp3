---
title: Modules and exports
---
# Modules and exports

Modules organize CP3 code and define explicit import boundaries. A module specifier is resolved by the CP3 module loader; it is not a request to execute an arbitrary Node package.

## Import a standard module

```ts
import console from "std/console";
import { Buffer } from "std/buffer";
```

The importer must use the form supported by that module's registry entry. Some modules expose a default namespace, others named values, and some support both. The [standard-library overview](../../standard-library/00-overview.md) lists the current import form for every registered module.

## Import project modules

Project source can import other CP3 modules using relative specifiers such as `./math.cp3`. Bare specifiers (without the `./` prefix) also resolve in the current compiler; when a package and a file both claim the same bare name, the package wins. Keep imports explicit and use the project entry and directory layout created by `cp3 init`. A missing or unresolved module is reported before C++ generation, naming the specifier at the import site.

## Default and named imports

A local module may export one default value and named declarations. An importer can request either or both:

```ts
import double, { add } from "./math.cp3";
```

The default import name is chosen by the importer. A named import must match an exported name; import aliases rename the local binding without renaming the export:

```ts
import { add as sum } from "./math.cp3";
```

Standard modules have their own registered export forms. For example, `std/console` uses a default namespace, while `std/buffer` and `std/collections` use named exports. Use the registry-backed forms from the [standard-library overview](../../standard-library/00-overview.md).

A local module may also use a namespace import — `import * as math from "./math.cp3"` — and reach the exported values as members.

## Exports

Modules can export supported declarations and values: `export function`, `export const`, `export let`, `export class`, `export interface`, `export type`, `export enum`, and `export default`. An `export default function` is legal:

```ts
export default function add(left: number, right: number): number {
  return left + right;
}
```

`export *` re-exports every named export of another module (a barrel); `export default` of an object literal is supported and its function members can be called from the importing file. Exports are live bindings: an importer that reads an exported `let` after the exporter's function mutates it observes the new value:

```ts
// counter.cp3
export let count = 0;
export function increment(): undefined { count = count + 1; }

// main.cp3
import { count, increment } from "./counter.cp3";
```

Package projects have a package-specific export contract; `cp3 init --type package` scaffolds the package entry.

## Imported classes, interfaces, aliases, and enums

A class, interface, type alias, or enum exported from one module and imported in another works in annotation and constructor positions across the boundary — the importer's compiler re-registers the declaration from the dependency:

```ts
// frame.cp3
export class Frame {
  id: number;
  constructor(id: number) { this.id = id; }
}

// main.cp3
import { Frame } from "./frame.cp3";
const f = new Frame(7);
console.log(f.id);
```

Annotated fields and parameters can name an imported class or interface (`sock: TcpSocket | undefined`), generic type arguments work across the boundary (`new Box<number>()`), and `Array<T>` is accepted in type positions. Annotations on the imported declarations resolve normally; a declaration exported as `type`, `interface`, or `class` resolves in annotation positions in the importing file. An exported object literal with arrow-function members keeps those members: the importer can call, destructure, and read them across the boundary.

## Resolution and diagnostics

The module loader considers project files, standard module names, and package metadata according to its resolution rules. A misspelled `std/...` specifier is not silently treated as a global — a bare `Math` is `CP3301`. A `type: "package"` entry module with no exports is flagged at check time (`CP3511`, a warning). A missing named export is a compile-time issue at the import site, not at link time; the diagnostic names the missing export.

## Module initialization

Imported modules are resolved and linked as part of the build pipeline. Imported modules' top-level statements run once, in dependency order, before the importing module's body.

## Module cycles

The test corpus includes circular imports. An import cycle does not by itself invalidate a program, but module-cycle initialization order is a fragile design point: prefer one-directional dependencies. Read the message at the import site when a cycle is rejected, and inspect the cycle fixtures under `tests/cases/modules`.

## Worker modules

CP3 has a language-level worker construct for CPU-bound or blocking work. A worker is a separate compilation unit with its own module state, communicating with the main thread through a typed channel. `Worker` and `Thread` are ambient; no import is needed.

The channel is typed by one annotation, `Thread<In, Out>`, on the worker's entry arrow — `In` is the main-to-worker direction, `Out` is the worker-to-main direction. `undefined` in a direction spells "this direction carries no values":

```ts
const w = new Worker((thread: Thread<number, number>) => {
  thread.on("message", (data) => {
    thread.postMessage(data * 2);
  });
});
const reply = await w.request(14);
w.terminate();
```

The file form defaults the entry to a module whose `export default` is the arrow:

```ts
// worker_impl.cp3
export default (thread: Thread<number, number>) => {
  thread.on("message", (data) => {
    thread.postMessage(data);
  });
};

// main.cp3
const w = new Worker("./worker_impl.cp3");
```

The worker side binds `on("message", listener)` and `postMessage(value)`; the main side binds `on("message", listener)`, `on("error", listener)`, `postMessage(value)`, `await w.request(value)`, and `w.terminate()`. A throw inside the worker surfaces as an `"error"` event on the main-side handle, not as a crash:

```ts
w.on("error", (err) => {
  console.log("the worker failed");
});
```

Payloads are deep-copied between the two sides: numbers, strings, booleans, arrays, records, enums, and unions/options of those are supported. A payload that is not copyable (a function, a handle, a class instance with function members) is rejected with `CP3907`. The payload must match the direction's type (`Thread<In, Out>`): sending to the main-to-worker direction a value the annotation does not cover is `CP3908`, and a send into an empty direction is `CP3906` (`request()` on an `Out`-empty worker is likewise rejected).

The inline form is an `async`-free arrow whose single parameter carries the `Thread<In, Out>` annotation; the annotation is the single source of truth for the channel. The entry is not a coroutine: an `async` worker arrow is rejected (`CP3914`). The inline arrow may not reference module-level or enclosing-function bindings (`CP3909`) — top-level functions, imports, builtins, and types stay visible, but enclosing state is not; the file form gives each worker its own module state for exactly this reason. A worker module with no default-export arrow carrying the annotation is rejected (`CP3905`).

## Module graphs

A program is a graph of modules rooted at the entry point. The compiler analyzes the whole graph before generating the native program, so a type error in an imported module is reported against that module's source with its full import path shown. Cross-package imports resolve through the package registry and installed package artifacts, not through npm.

## Related pages

- [Errors and exceptions](./02-errors.md)
- [Global builtins](../99-global-builtins.md)
- [Standard-library overview](../../standard-library/00-overview.md)
