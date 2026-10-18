# TypeScript → C+++ cheat sheet

One page. Keep it open while you port code.

## Stays the same

| Concept | Notes |
| --- | --- |
| `let` / `var` / `const` | Same as real JS: `let`/`var` mutable, `const` immutable, block-scoped |
| `number`, `string`, `boolean` | `number` is a 64-bit float, like JS |
| Type inference | `let age = 18;` → `number` |
| Functions | Arrow fns, defaults, rest (`...args`), destructuring, overloading by type |
| Classes | `public`/`private`/`protected`, `static`, `extends`, abstract, `this`, getters/setters |
| Interfaces | **Structural** - no `implements` needed |
| `switch` fall-through, `for`/`for...of`/`for...in`, `while`/`do...while` | Unchanged |
| `==`/`===`, `?.`, `??`, ternary, bitwise, spread | Unchanged |
| Truthy/falsy coercion | `0`, `""`, `null`, `undefined`, `NaN` falsy - like JS |
| `try`/`catch`/`finally`, `throw` | Same model, no exception hierarchy |
| `async`/`await`, `Promise.all`/`race`/`allSettled` | Same event-loop model |
| Enums | Number- and string-backed |
| Generics `<T>` | With `<T extends Shape>` constraints |
| `import`/`export`, default + named | ES-module style |
| Regex literals | `/pattern/flags` |
| Array & string methods | `map`, `filter`, `reduce`, `sort`, `split`, `trim`, `padStart`, … |
| Comments, numeric literals | `//`, `/* */`, `0x1F`, `0b101`, `1_000_000`, template literals |

## Won't compile / behaves differently

| TypeScript | C+++ | Why |
| --- | --- | --- |
| `any` | `unknown` + narrow | `any` doesn't exist |
| `const u = {}; u.x = 1;` | Declare the shape: `class U { x: number }` | Plain objects have a fixed shape |
| `delete obj.prop` | - | Banned |
| `obj["dynamicKey"]` to build a dict | `import { Map } from "std/collections"` | No index-signature object type |
| `arguments` | Rest params `...args` | Banned |
| `for (k in plainObject)` | `Map`/`Set`, or `Object.keys/values/entries` | Banned on plain objects |
| `number[]` accepting strings | `(number \| string)[]` for mixed | Arrays are homogeneous |
| `count + ""` | `String(count)` | Mixed-type `+` is a compile error |
| `str[i]` as a character | `charAt(i)` for a character | Strings are UTF-8; `str[i]` is a byte |
| `sort()` string-coercing | `sort()` is numeric & stable | Type-aware |
| `function*` / `yield`, decorators | - | Out of v1 |
| Unhandled promise rejection | Exits the process | Attach `.catch` or `try`/`catch` |
| `fetch` throwing on 4xx/5xx | Check `res.status` yourself | Browser-like |

## Extra in C+++ (not in TS)

| Thing | Do you need it on day one? |
| --- | --- |
| `unknown` as the safe `any` | Yes |
| Explicit unions `(number \| string)` | Yes - this is your "one of a few types" |
| `ref` / `weak` keywords | **No** - advanced reference handling; skip [§10](../language-spec/10-memory-management.md)-[§12](../language-spec/12-pointers-references.md) for now |
| Configurable type vocabulary (`"typeSystem": "rust" \| "cpp"`) | No - default is TypeScript |
| Automatic memory management, native binary output | You get it for free; you don't operate it |

## Standard library quick map

| Need | Import |
| --- | --- |
| Print / read input | `import console from "std/console"` |
| JSON | `import JSON from "std/json"` |
| Math | `import Math from "std/math"` |
| Files & dirs | `import fs from "std/fs"` |
| Paths | `import path from "std/path"` |
| Args, env, subprocess | `import process from "std/process"` |
| Dates | `import Date from "std/date"` |
| Dictionary / set | `import { Map, Set } from "std/collections"` |
| HTTP, servers, WebSockets | `import { fetch, createServer, WebSocket, WebSocketServer } from "std/network"` |
| Events | no import - `EventEmitter` is a language builtin (see §14) |
| Crypto | `import crypto from "std/crypto"` |
| Env vars (typed) | `import { env } from "std/env"` |
| Binary data | `import { Buffer } from "std/buffer"` |
| OS info | `import os from "std/os"` |

**No import needed:** array methods, string methods, regex, `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval`, `sleep(ms)`, `parseInt`/`parseFloat`, `Object.keys`/`values`/`entries`, `new EventEmitter()`.

## CLI quick map

| Command | Use |
| --- | --- |
| `cp3 init` | Scaffold a project |
| `cp3 run <file>` | Build + run |
| `cp3 check` | Type-check only |
| `cp3 dev` | Watch + rebuild |
| `cp3 fmt` | Format |
| `cp3 build` | Native `.exe` (app) or `.cp3pak` (package) |
| `cp3 clean [--all]` | Clear generated output |
| `cp3 install [pkg]` | Install dependencies |
| `cp3 exec <pkg> [args]` | Run a package's CLI |
| `cp3 publish` | Publish your package |
| `cp3 link <file.cp3pak>` | Link a local package |

```
my-app/
├── package.json
├── src/index.cp3        # "main"
└── .cp3/                # generated → gitignore
```

---

**Full references:** [Day 1 - the language](01-day-1-the-language.md) · [Day 2 - stdlib & CLI](02-day-2-stdlib-and-cli.md) · [language spec](../language-spec/00-overview.md) · [stdlib §04](../standard-library/04-imported-std-modules.md) · [CLI §1](../cli-tooling/01-compilation-pipeline.md)
