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
- **completion with documentation**: every candidate in the completion popup
  carries the same description, so the popup explains what a member does
  before it is inserted;
- **signature help with documentation**: the active call shows what the
  callee does and what each parameter is for;
- go-to-definition, document symbols and formatting, all answered by the
  same language server.

The documentation is Markdown: the server sends it as LSP `MarkupContent`,
which VS Code renders as formatted text (a description, a `@param` list, a
signature).

## Branding

### The logo

`images/cp3-logo.svg` is the cp3 mark: a C++-style "C" with the **three**
plus signs of "C+++" (the C++ mark has two). It is hand-written vector art -
a 128x128 `viewBox` built from a rounded badge, one annular path and six
rectangles. There is no font (no text element at all), no embedded raster
image and no external reference, so it renders identically everywhere and
stays sharp at any size.

The palette is two strong colors on a self-contained badge: the badge is an
indigo-to-violet gradient (`#4F46E5` -> `#7C3AED`), the "C" is white and the
plus signs are amber (`#FBBF24`). Because the "C" always sits on the badge
rather than on the editor's background, the mark reads on a light *and* on a
dark theme.

At 128x128 the whole mark is visible; at 16x16 (the file icon) the "C"
carries the identity and the plus signs read as its texture.

`images/cp3-logo.png` is the 128x128 PNG the VS Code marketplace wants. It is
a raster of exactly that SVG - regenerate it with the script that ships
next to it:

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/render-logo.ps1
```

The script rasterizes the SVG with a locally installed Chromium browser in
headless mode (Chrome or Edge), so the PNG and the file icon match; nothing
is downloaded and no image generator is involved.

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
root, and that the artwork is what it claims to be (a 128x128 vector without
fonts, and a 128x128 PNG).

## Requirements

- VS Code 1.91 or newer.
- The cp3 toolchain, meaning a `cp3c` (or `cp3c.exe`) binary that understands
  `lsp --stdio`. If it is not on `PATH`, set `cp3.compilerPath` to its full
  path.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `cp3.compilerPath` | `cp3c` | Path to the cp3 compiler. A bare name is searched on `PATH` (on Windows `cp3c.exe` is tried too); an absolute path is used as-is. |

## Commands

| Command | What it does |
| --- | --- |
| `cp3: Restart Language Server` | Stops the running server and starts it again from the current `cp3.compilerPath`. |

## Installing

The extension ships with the cp3 toolchain. Pick whichever fits:

1. **With the cp3 installer.** `cp3-setup-0.1.0-x64.exe` installs the bundled
   `.vsix` automatically when it finds the `code` CLI.
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
   code --install-extension extensions/cp3-vscode/dist/cp3-vscode-0.1.0.vsix --force
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
cd extensions/cp3-vscode
npm install
npm run compile          # esbuild bundle -> dist/extension.js
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
```

`scripts/package.ps1` installs dependencies if needed, builds the production
bundle and runs `vsce package --no-dependencies` to produce
`dist/cp3-vscode-0.1.0.vsix`. The bundle is self-contained: the
`vscode-languageclient` dependency is bundled into `dist/extension.js`, which
is why the packaging step passes `--no-dependencies` and the `.vscodeignore`
keeps `node_modules` and the sources out of the `.vsix`.

`npm run typecheck` runs `tsc --noEmit`. The grammar is plain TextMate JSON in
`syntaxes/cp3.tmLanguage.json`.

## Known limits

- Template literal interpolation is tokenized with a `$self` recursion, so
  nested templates work; an object literal written directly inside `${...}`
  can still end the interpolation at the first `}`.
- `delete`, `any` and `yield` are reserved words in cp3 (each produces a
  specific compiler error). The grammar scopes them as reserved keywords
  rather than errors.
- The `std/events` module does not exist: `EventEmitter` is a language
  builtin, which is why the grammar highlights it and not an import.
