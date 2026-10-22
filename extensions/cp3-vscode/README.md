# cp3 for VS Code

Language support for [cp3](../../docs/language-spec/00-overview.md), the
statically typed, JS/TS-shaped language that compiles through C++:

- syntax highlighting for `.cp3` files (keywords, types and builtins,
  comments, strings, template literals with `${}` interpolation, numbers,
  operators);
- comment/bracket/indent behavior for `.cp3` documents;
- live compiler diagnostics: the extension starts the language server built
  into the compiler, `cp3c lsp --stdio`, and shows its diagnostics as you
  type.

The scope of this round is deliberately small: highlighting plus the
compiler's own diagnostics. Hover, completion, go-to-definition, formatting
and rename are not implemented yet - neither in this extension nor in the
server.

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
