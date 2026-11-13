# cp3 for VS Code

Language support for [cp3](../../docs/language-spec/00-overview.md), the
statically typed, JS/TS-shaped language that compiles through C++:

- syntax highlighting for `.cp3` files (keywords, types and builtins,
  comments, strings, template literals with `${}` interpolation, numbers,
  operators);
- comment/bracket/indent behavior for `.cp3` documents;
- live compiler diagnostics: the extension starts the language server built
  into the compiler, `cp3c lsp --stdio`, and shows its diagnostics as you
  type;
- **hover with documentation**: hovering a name shows its type *and* what it
  does - the doc comment of a `.cp3` declaration (`/** ... */` or `/// ...`,
  see [the language spec](../../docs/language-spec/24-doc-comments.md)) or the
  standard library's own description of a member such as `console.log` or
  `Math.abs`;
- **completion with documentation**: documented declarations and standard
  library members show their Markdown descriptions beside the candidate;
- context-aware member completion for object literals, interfaces, classes,
  standard-library values and typed `std/network` callbacks;
- workspace auto-import completion for exported CP3 declarations, including
  named re-exports and unsaved open files. The nearest matching project module
  is ranked first and accepting a result adds its import;
- **the codebase-aware suggestion engine**: member completion for the
  standard library ranked by how *this workspace* uses it (sin/cos rise
  above rare members in math-heavy code), named-export auto-import
  (`Map`, `URL`, `Buffer`...), module-member call snippets (`cons` offers
  `console.log(...)` with the import inserted) and workspace symbol ranking
  learned from your own code's unigrams and adjacent-token pairs;
- a curated set of CP3 snippets for declarations, classes, control flow,
  imports, collections, promises, documentation and HTTP handlers;
- **signature help with documentation**: the active call shows what the
  callee does and what each parameter is for;
- go-to-definition, document symbols and formatting, all answered by the
  same language server.

The documentation is Markdown: the server sends it as LSP `MarkupContent`,
which VS Code renders as formatted text (a description, a `@param` list, a
signature).

## Smart suggestions

On top of the language server, the extension runs a local suggestion engine
that learns your codebase and offers the candidates you are actually about
to write. It is a ranking and offering layer - parsing, types and
diagnostics remain the server's job, and nothing leaves your machine.

**What it knows:**

- a background index of every `**/*.cp3` file in the workspace (bounded,
  debounced, re-scanned on save and on watch events; unsaved open documents
  always win over disk) with three statistics: identifier frequency
  (*"how often is `combine` used here?"*), adjacent token pairs
  (*"after `const` comes `value` in this codebase"*, a unigram/bigram model)
  and member-use frequency (*"`Math.sin` is called 40 times here, `Math.acosh`
  never"*);
- the documented closed standard library surface (`std/math`, `std/console`,
  `std/process`, `std/fs`, `std/path`, `std/os`, `std/date`, `std/json`,
  `std/crypto`, `std/time`, `std/readline`, `std/hash`, `std/io`, `std/url`,
  `std/text`, `std/buffer`, `std/collections`, `std/env`) - member completion
  after `x.` only offers names the compiler accepts;
- the document's own imports, so `import m from "std/math"` completes
  `m.sin` correctly, and `Math.` completes it without any import at all
  (accepting inserts `import Math from "std/math";` for you);

**What it offers:**

- `Math.` / `console.` / `process.` ... member completion ranked by corpus
  frequency (log/sin rise to the top of a math-heavy project), falling back
  to a documented prior for the hot members;
- named-export auto-import (`Map`, `Set`, `URL`, `TextEncoder`, `Buffer`...)
  ranked by usage; accepting merges the name into an existing
  `import { Map } from "std/collections";` if you already have one;
- module-member call snippets: typing `cons` offers `console.log(...)`
  (with the import inserted on accept), typing `sin` offers `Math.sin(...)`;
  matched members are labelled by the member name when the query matches the
  member, or `module.member` when it matches the module;
- workspace symbols (your declared exports) ranked by usage, excluding the
  ones the current file declares itself and filtering matches against what
  you typed;
- everything ordered deterministically (exact prefix, camel-hump, substring,
  fuzzy subsequence - re-ordered within each class by corpus and bigram
  evidence), never fighting the server, capped, and disabled entirely with
  `cp3.suggestCodebase: false`.

## Branding

### The logo

`images/cp3-logo.svg` is the cp3 wordmark: a white **CP** with a purple
gradient **3** (`#8F5BFF` -> `#7C4DF0`). It is hand-written vector art - no
font (no text element at all), no embedded raster image and no external
reference - letterboxed onto a square `viewBox` (`0 0 1460 1460`) so the same
artwork fills the 128x128 marketplace icon slot and the 16x16 file-icon slot
without cropping. The artwork itself is unchanged from the original supplied
banner; only the canvas was made square.

`images/cp3-logo-light.svg` is the same wordmark for light themes: the
near-white `#F2F2F4` C/P revert to near-black (`#111111`) and the gradient
"3" stays exactly as it is. `icons/cp3-icons.json` maps `.cp3` to the
near-white wordmark by default (used on dark themes) and to the near-black
wordmark under the `light` theme association (applied by VS Code on light
themes). The marketplace icon `images/cp3-logo.png` is the 128x128 raster of
the **light** wordmark - the store page renders on white, so the blackish
letters are the readable choice there; pass `-Variant dark` to the render
script to rasterize the near-white variant instead.

`images/cp3-logo.png` is regenerated with the script that ships next to it:

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/render-logo.ps1
```

The script rasterizes the SVG with a locally installed Chromium browser in
headless mode (Chrome or Edge), so the PNG and the file icon match; nothing
is downloaded and no image generator is involved. (The `-Variant` switch
picks `light` - the default - or `dark`.)

### The file icon

The extension contributes an icon theme, **cp3** (`icons/cp3-icons.json`),
that extends the built-in `vscode-seti` theme and maps the `.cp3` extension
to the logo. Enable it once:

1. `Ctrl+Shift+P` -> **Preferences: File Icon Theme** (or **Preferences:
   Color Theme** -> **File Icon Theme**).
2. Pick **cp3**.

Every `.cp3` file then shows the cp3 mark in the explorer and in the tab. The
theme only adds the `.cp3` entry - every other file keeps the icon the base
theme gives it.

`npm run smoke` includes a `brand-smoke` check that verifies the manifest is
valid JSON, that the contributed paths resolve relative to the extension
root and the theme file, that both wordmark SVGs keep the purple-gradient
"3" (and their respective dark/light letter colors) with no fonts or
external references, that the icon theme's light override re-points `.cp3`
at the light wordmark, and that the PNG is a 128x128 raster.

## Requirements

- VS Code 1.91 or newer.
- The cp3 toolchain, meaning a `cp3c` (or `cp3c.exe`) binary that understands
  `lsp --stdio`. The extension searches `PATH` first; when opening a CP3
  source checkout, it can also find that checkout's `build/bin/cp3c`. For any
  other install location, set `cp3.compilerPath` to the full path.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `cp3.compilerPath` | `cp3c` | Path to the cp3 compiler. A bare name is searched on `PATH` (on Windows `cp3c.exe` is tried too), then in parent folders for a CP3 source checkout's `build/bin/cp3c`; an explicit path is used as-is. |
| `cp3.suggestWorkspaceExports` | `true` | Offer exported symbols from CP3 files in the workspace and add the corresponding import when accepted. |
| `cp3.suggestCodebase` | `true` | Enable the codebase-aware suggestion engine: stdlib member ranking (`Math.` -> sin/cos first), named-export auto-import, module-member call snippets (`cons` -> `console.log(...)`), workspace symbol ranking. Off: only the server and snippets. |

## Commands

| Command | What it does |
| --- | --- |
| `CP3: Run Current File` | The **play button in the editor's toolbar (top-right corner)** of a `.cp3` file. Saves and runs it through the configured toolchain (`<cp3.compilerPath> run <file>`) and streams output to the `cp3 Run` output channel — no terminal panel is opened. Non-zero exits show a `Show Output` notification. |
| `cp3: Restart Language Server` | Stops the running server and starts it again from the current `cp3.compilerPath`. |
| `CP3: Organize Imports` | Sorts a contiguous group of regular imports. Side-effect and multiline imports are left untouched. |
| `cp3: Apply cp3 Manifest Schema to package.json` | Re-associates the workspace `package.json` with the bundled cp3 manifest schema (normally applied automatically when the workspace root manifest is a cp3 project). |

The last command covers the cp3 project manifest: a `package.json` with a
`.cp3` `main`, `typeSystem`, or `cp3c` scripts is a cp3 project, not an npm
one. When the extension activates in such a workspace it records the
bundled schema (`schemas/cp3-package.schema.json`) in the workspace
`json.schemas` setting for `package.json`, so the manifest validates and
completes with cp3 fields (`typeSystem`, `main`, `scripts`, ...) instead of
the npm schema. Only that one entry (replaced idempotently) is ever
written, and only for detected cp3 workspaces; npm workspaces keep the npm
schema.

The run button is also available from the Command Palette and the editor context menu. It only appears when a CP3 file is the active editor.

The editor context menu also exposes **Format Document** for CP3 files. It
uses the formatter provided by `cp3c lsp`; the same command is available from
the Command Palette.

## Installing

The extension ships with the cp3 toolchain. Pick whichever fits:

1. **With the cp3 Windows MSI.** `cp3-0.2.4-windows-x64.msi` installs the
   toolchain and stages the bundled `.vsix`; run `cp3 editor-plugin` to install
   it into a detected editor.
2. **From an existing cp3 install.**
   ```
   cp3 editor-plugin
   ```
   The wizard detects VS Code, VS Code Insiders, Cursor, Antigravity and
   VSCodium. The scriptable form is `cp3 editor-plugin install --editor code`;
   `cp3 editor-plugin export --out <dir>` writes the bundled `.vsix` for an
   editor on another machine.
3. **From this folder, by hand.**
   ```
   code --install-extension dist/extensions/cp3-vscode/dist/cp3-vscode-0.2.4.vsix --force
   ```

There is no marketplace publish in this round.

## When the server cannot start

If `cp3.compilerPath` does not resolve to an executable, the extension shows
one warning - `cp3: language server not found` - with **Open Settings** and
**Retry** buttons. It does not retry on its own and does not restart the
server in a loop. The server is started again only by:

- changing `cp3.compilerPath`;
- running the `cp3: Restart Language Server` command;
- pressing **Retry** in the warning;
- reloading the window.

The same is true when the binary exists but immediately fails: one warning,
no automatic restart. The `cp3 Language Server` output channel (View ->
Output) records what was tried.

## Building and packaging from source

```
cd dist/extensions/cp3-vscode
npm install
npm run compile          # esbuild bundle -> dist/extension.js
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
```

`scripts/package.ps1` installs dependencies if needed, builds the production
bundle and runs `vsce package --no-dependencies` to produce
`dist/cp3-vscode-0.2.4.vsix`. The bundle is self-contained: the
`vscode-languageclient` dependency is bundled into `dist/extension.js`, which
is why the packaging step passes `--no-dependencies` and the `.vscodeignore`
keeps `node_modules` and the sources out of the `.vsix`.

`npm run typecheck` runs `tsc --noEmit`. The grammar is plain TextMate JSON in
`syntaxes/cp3.tmLanguage.json`.

## Highlighting conventions

The grammar does not invent its own colours: it maps cp3 onto the TextMate
scope names the built-in Dark+ and Light+ themes already colour, so a cp3
file reads exactly like the TypeScript file next to it.

| What | Scope |
| --- | --- |
| a local variable | `variable.other.readwrite.cp3` |
| a parameter | `variable.parameter.cp3` |
| a function or method name | `entity.name.function.cp3` |
| a class name | `entity.name.class.cp3` |
| an interface, enum or type alias | `entity.name.type.*.cp3` |
| a type annotation | `keyword.operator.type.annotation.cp3` |
| a property after a receiver | `variable.other.property.cp3` |
| control flow (`if`, `for`, `return`, `try`, ...) | `keyword.control.cp3` |
| storage (`let`, `const`, `function`, `class`, ...) | `storage.type.cp3` |
| member modifiers (`private`, `static`, `abstract`, ...) | `storage.modifier.cp3` |
| a builtin class (`Promise`, `Array`, ...) | `support.class.builtin.cp3` |
| a builtin namespace (`console`, `Math`, `JSON`) | `support.class.builtin.cp3` + `support.namespace.cp3` |
| `this` | `variable.language.this.cp3` |
| numbers, strings, comments, operators | `constant.*`, `string.*`, `comment.*`, `keyword.operator` |

`npm run smoke:grammar` checks these scopes through the real TextMate engine
(`vscode-textmate` plus `vscode-oniguruma`, the pair VS Code uses).

## Known limits

- Template literal interpolation is tokenized with a `$self` recursion, so
  nested templates work; an object literal written directly inside `${...}`
  can still end the interpolation at the first `}`.
- `delete`, `any` and `yield` are reserved words in cp3 (each produces a
  specific compiler error). The grammar scopes them as reserved keywords
  rather than errors.
- The `std/events` module does not exist: `EventEmitter` is a language
  builtin, which is why the grammar highlights it and not an import.
- The suggestion engine ranks and offers; it does not type-check. Member
  completion for user-defined classes, interfaces and typed object literals
  is the language server's job, not the engine's (the engine only ranks the
  candidates the server and its own catalogs offer). Pair/frequency statistics
  are computed from your workspace's own code - there is no bundled usage
  profile and nothing is sent anywhere.
