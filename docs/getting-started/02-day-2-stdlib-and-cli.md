# Day 2 - the std library & the CLI

**Goal for today:** stop looking things up. Know which standard-library module to import for a given job, know the five CLI commands you'll actually type, and ship a small working tool.

## Part 1 - the standard library you'll use every day

Two rules to internalize before the module list:

1. **Array, string, regex, and timer built-ins need no import.** They're core language features (Part 2).
2. **Everything that touches the OS or runtime needs an explicit `import`.** A module is imported as a namespaced object - no global `std::` dumping.

```cp3
import console from "std/console";
console.log("hello");

import Math from "std/math";
let x = Math.sqrt(25);        // 5
```

### The modules you'll reach for first

| Import | Use it for | Key calls |
| --- | --- | --- |
| `import console from "std/console"` | All I/O: printing and input | `log`, `info`, `warn`, `error`, `input(prompt)`, `readKey()`, `assert`, `clear`, `hide`/`show`, `on`/`off` (keep listening: `keypress`, `line`, `close`) |
| `import JSON from "std/json"` | Serialize / parse | `JSON.stringify(v)`, `JSON.parse<T>(s)` |
| `import Math from "std/math"` | Math | `sqrt`, `pow`, `random`, `floor`, `ceil`, `round`, `clamp`, constants |
| `import fs from "std/fs"` | Files & dirs | `readFileSync`, `writeFileSync`, `existsSync`, `mkdirSync`, `readdirSync`, `statSync` + async twins |
| `import path from "std/path"` | Path manipulation | `join`, `resolve`, `basename`, `dirname`, `extname` |
| `import process from "std/process"` | Args, env, running things | `process.argv`, `env`, `exit(code)`, `exec(cmd)`, `spawn(cmd)` |
| `import Date from "std/date"` | Timestamps (immutable) | `Date.now()`, `new Date()`, formatting/parsing |
| `import { Map, Set } from "std/collections"` | The dictionary/set you *should* be using | full `Map`/`Set` APIs |

A few exact signatures worth committing to memory:

```cp3
// console - note it's an imported object, not a language global
console.log(...args);                 // stdout
console.error(...args);               // stderr
console.warn(...args);                // stderr
console.assert(cond, ...args);        // logs to stderr only if false; does NOT throw or exit
let name = await console.input("Name? ");

// JSON.parse<T> validates at runtime and throws if the shape doesn't match
let cfg = JSON.parse<Config>(text);

// fs has sync and async forms
let text = fs.readFileSync("notes.txt");
fs.writeFileSync("out.txt", text);
if (fs.existsSync("data")) { /* ... */ }

// process.argv mirrors node's: [binary, script, ...args]
let args = process.argv;
```

> **Why you'd read this later:** the complete, signature-level reference is [stdlib §04 Imported std modules](../standard-library/04-imported-std-modules.md). It's long - use it as a lookup table, not a read-through.

## Part 2 - built-ins that need no import

These are part of the language, not modules:

- **Array methods** - `push`, `pop`, `map`, `filter`, `reduce`, `sort`, `find`, `includes`, `flat`, and the rest.
- **String methods** - `split`, `trim`, `replace`, `padStart`/`padEnd`, `toUpperCase`/`toLowerCase`, `charAt`, etc.
- **Regex** - native `/pattern/flags` literals, like JS.
- **Timers** - `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` as global builtins tied to the event loop.
- **`sleep(ms)`** - returns `Promise<void>`; the idiomatic `await sleep(1000)`.
- **`parseInt(str)` / `parseFloat(str)`** - globals (not string methods), returning `number`.
- **`Object.keys` / `Object.values` / `Object.entries`** - supported, implemented as compile-time macros.

```cp3
await sleep(500);                       // no import
setTimeout(() => console.log("later"), 1000);
let n = parseInt("42");                  // 42
let keys = Object.keys(someObject);
```

> **Why you'd read this later:** [stdlib §03 Built into the language](../standard-library/03-built-into-the-language-no-import-needed.md) is the one-page list.

## Part 3 - networking in one import

Everything network-related comes from `std/network`, tree-shaken by what you actually import:

```cp3
import { fetch } from "std/network";

let res = await fetch("https://api.example.com/items");
if (res.status === 200) {
  let items = await res.json();
}
```

- `fetch` mirrors the JS API. **Gotcha:** it does *not* throw on a non-2xx status - check `res.status` yourself, exactly like the browser.
- **Servers:** `import { createServer } from "std/network"`.
- **WebSockets:** `import { WebSocket, WebSocketServer } from "std/network"`.

Because `async`/`await` is the JS model over a single event loop, all your promise intuition transfers directly. The one cp3-specific rule to remember: **an unhandled promise rejection terminates the process** - attach a `.catch` or wrap in `try`/`catch`.

> **Why you'd read this later:** [§14 Async](../language-spec/14-async.md) for the full async/await contract, plus [stdlib §04](../standard-library/04-imported-std-modules.md) for `fetch`/server/WebSocket signatures.

## Part 4 - the `cp3` CLI you'll actually type

You met `run`/`check`/`dev` on Day 1. Here's the full set, grouped by when you'd use it.

### Everyday

| Command | What it does |
| --- | --- |
| `cp3 init` | Scaffold a project: prompts type → name → description → type-system; writes `src/`, `package.json`, `index.cp3` |
| `cp3 run <file>` | Build and execute |
| `cp3 check` | Parse + type-check only (your `tsc --noEmit`) |
| `cp3 dev` | Watch mode: rebuild on change |
| `cp3 fmt` | Format source files |
| `cp3 build` | Produce a native `.exe` (application) or a `.cp3pak` (package) |
| `cp3 clean` | Wipe `.cp3/build/`; add `--all` to also clear `.cp3/packages/` |

### Dependencies & packages

| Command | What it does |
| --- | --- |
| `cp3 install <name-or-url>` | Install a published package (pure cp3, or one with native bindings baked in) |
| `cp3 install` | Install everything listed in `package.json` |
| `cp3 native-install <sdk>` | *Author-side:* generate bindings from a C/C++ SDK. Not the everyday path - most people just `cp3 install` a package someone already published |
| `cp3 exec <package> [args]` | Run a package's bundled CLI tool |
| `cp3 publish` | Publish your package |
| `cp3 link <file.cp3pak>` | Link a local package (your `npm link`) |
| `cp3 recompile <file.cp3pak>` | Rebuild a package from source |

> `cp3 install` and `cp3 native-install` are **not** two names for the same thing. `install` consumes an already-published package; `native-install` is what an *author* runs once to wrap a native library before publishing. Details in [CLI §4 Package manager](../cli-tooling/04-package-manager.md).

### The project it manages

```
my-app/
├── package.json         # manifest
├── src/
│   └── index.cp3        # entry point ("main")
└── .cp3/                # generated: packages/, native/, build/, lock.json  → gitignore this
```

`package.json` is a small superset of npm's:

```json
{
  "name": "mathkit",
  "version": "1.0.0",
  "description": "basic math helpers",
  "type": "application",
  "main": "src/index.cp3",
  "typeSystem": "typescript",
  "build": { "files": ["README.md"], "cacheBinary": ["windows", "linux"] }
}
```

- `"type": "application"` → `cp3 build` makes a `.exe`. `"type": "package"` → makes a `.cp3pak`.
- `"main"` - entry point; defaults to `index.cp3`/`main.cp3`. The compiler starts here and bundles only what's reachable through `import`.
- `"typeSystem"` - the project's type vocabulary (default `"typescript"`).

The only thing you add to `.gitignore` is `.cp3/`.

> **Why you'd read this later:** [CLI §1 pipeline](../cli-tooling/01-compilation-pipeline.md), [CLI §4 package manager](../cli-tooling/04-package-manager.md), and [CLI §5 package format](../cli-tooling/05-package-format-authoring-cp3pak.md) are your reference when you publish or wire up CI.

## Part 5 - put it together: a small CLI tool

Everything from today in one ~20-line program. This counts the words in each file passed on the command line:

```cp3
import console from "std/console";
import fs from "std/fs";
import path from "std/path";

function countWords(text: string): number {
  return text.split(/\s+/).filter((w) => w.length > 0).length;
}

function main(): void {
  let files = process.argv.slice(2);        // drop binary + script
  if (files.length === 0) {
    console.error("usage: wordcount <file> [file...]");
    process.exit(1);
  }

  let total = 0;
  for (let file of files) {
    if (!fs.existsSync(file)) {
      console.warn("skip (missing):", file);
      continue;
    }
    let words = countWords(fs.readFileSync(file));
    console.log(`${path.basename(file)}: ${words} words`);
    total += words;
  }
  console.log(`total: ${total} words`);
}

main();
```

Notice what *isn't* there: no `any`, no dynamic object, no coercion, no memory management, no build config. It reads like the TypeScript you already write - and it compiles to a native executable.

Run it:

```bash
cp3 run src/index.cp3 README.md package.json
cp3 build            # → a standalone .exe you can ship
```

## Where to go next

- **Daily reference:** [stdlib §04 Imported std modules](../standard-library/04-imported-std-modules.md) - bookmark it.
- **Depth on the language:** start from the [language spec overview](../language-spec/00-overview.md).
- **When you ship:** [CLI §4](../cli-tooling/04-package-manager.md) and [CLI §5](../cli-tooling/05-package-format-authoring-cp3pak.md).
- **Still curious about the "how":** [§19 C/C++ interop](../language-spec/19-c-cpp-interoperability.md) and the pipeline doc are there whenever you want them - but you never *needed* them to be productive.

## Checkpoint: you're done with Day 2 when…

- You know `console`, `JSON`, `Math`, `fs`, `path`, `process`, and `Map`/`Set` are imports, while arrays/strings/regex/timers are not.
- You can scaffold (`cp3 init`), run (`cp3 run`), type-check (`cp3 check`), and build (`cp3 build`) a project from memory.
- You know `fetch` doesn't throw on 4xx/5xx, and an unhandled rejection kills the process.

That's the whole on-ramp. Everything past this is depth, not prerequisites.

**Keep the [TypeScript → C+++ cheat sheet](03-cheatsheet.md) handy.**
