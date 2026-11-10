---
title: Create and run your first project
chapter: Welcome
---
# Create and run your first project

This walkthrough creates the normal application project, checks its source, runs it, and builds a release executable. CP3 can also scaffold a package; that is a library project with exports rather than a program entry point.

## 1. Create an application

```sh
cp3 init hello-app --yes
cd hello-app
```

`--yes` accepts scaffold defaults so the command works in scripts and non-interactive shells. Without it, an interactive terminal asks for the project type, name, a description, an author, and the type-system vocabulary; the two choice questions are arrow-key menus. The command creates `package.json` and `src/index.cp3`. It leaves files already present in the destination untouched unless `--force` is passed.

The generated application source is intentionally small:

```ts
import console from "std/console";

console.log("Hello from hello-app!");
```

The scaffold manifest contains a `main` entry, a `typeSystem` selection, standard command scripts, and a `build` section. The `type` field describes CP3's application or package mode; it does not activate Node's module loader.

## 2. Know the project files

```text
hello-app/
├── package.json
└── src/
    └── index.cp3
```

`package.json` is CP3 project metadata. The CLI reads it to select the source entry and compile settings. `src/index.cp3` is the entry file created by `cp3 init`. You can add more `.cp3` modules and import them using CP3 module rules.

Generated application build state is stored under `.cp3/build`; output executables are placed under `.cp3/out` by the project build workflow. These paths are not source files. Do not edit generated C++ as a substitute for fixing CP3 source or compiler code.

## 3. Select the type-name vocabulary

The scaffold defaults to TypeScript-style type names. A project may choose another supported vocabulary in `package.json`:

```json
{
  "name": "hello-app",
  "type": "application",
  "main": "src/index.cp3",
  "typeSystem": "typescript"
}
```

The accepted `typeSystem` values are `typescript`, `cpp`, and `rust`. The setting chooses how supported unprefixed native scalar type names are interpreted; it does not change the generated backend, which remains C++. For example, `i32` is not the spelling used in every mode. See [Project type-system selection](../01-language/07-native-types/01-project-type-system.md) and [Prefixed native types](../01-language/07-native-types/02-prefixed-types.md).

You can also select the value when scaffolding:

```sh
cp3 init hello-app --yes --type-system rust
```

The CLI rejects an unsupported choice rather than silently treating an arbitrary string as a type system.

## 4. Check the source

```sh
cp3 check src/index.cp3
```

`check` loads modules, parses the source, resolves names, and runs semantic and type analysis. It is useful as a quick feedback step and does not execute the program. It also does not prove that native C++ compilation and linking will succeed; those occur during a build or run.

When a check fails, read the first diagnostic and its source location. CP3 error codes are stable identifiers that are useful for searching tests and documentation. The [diagnostics guide](../04-compiler-cli/06-diagnostics.md) explains the local help page and what a code does and does not guarantee.

## 5. Run the entry

```sh
cp3 run src/index.cp3
```

`run` compiles as needed, links the executable, and starts it. You may omit the path when the CLI can find the project manifest and its `main` value. Run the command from the intended project directory so the nearest `package.json` is the one used.

The first run may take longer because the native compiler must build generated sources and runtime support. Later runs can reuse compatible cached build products. A changed source dependency, compiler, toolchain, target, or build option can invalidate some of that cache. See [Project settings and build cache](../04-compiler-cli/09-project-settings-and-cache.md).

## 6. Build an executable

```sh
cp3 build src/index.cp3 --release
```

The build command compiles and links without launching the program. The release profile uses optimized native compilation options available in the installed CP3 version and selected toolchain. Record the profile and native compiler version when comparing performance.

Use `--debug` when you want unoptimized code and debug information. Use `--incremental` for a development build that prioritizes reuse and short rebuilds. The exact supported options are available from:

```sh
cp3 help
```

See [Build profiles and PGO](../04-compiler-cli/11-build-profiles-and-pgo.md) for what profile names mean in this compiler.

## 7. Understand the scaffold scripts

The default manifest includes commands named `start`, `verify`, and `build`. They invoke `cp3c run`, `cp3c check`, and `cp3c build` against the configured entry. They are convenience metadata; CP3 does not require npm to execute them. Inspect the generated command text before relying on it in a package manager or shell integration.

The application scaffold enables `bundleNativeDlls` in its build section. Package scaffolds instead list files to include in the package archive and generate a default export object. Packaging behavior is distinct from building and running an application.

## 8. Project versus compiler build

There are two separate builds:

- **Build the CP3 compiler:** from the CP3 repository, CMake compiles compiler and runtime targets into the development build tree.
- **Build a CP3 program:** the `cp3` CLI checks a project, generates C++, calls the native application toolchain, and links an executable in that project's output tree.

Rebuilding the compiler is necessary after changing compiler source. It is not necessary for an ordinary `.cp3` source edit. The application build cache and the repository's CMake object files are separate.

## 9. Common first-project problems

| Symptom | What to check |
| --- | --- |
| `cp3` is not recognized | Confirm the launcher is installed and on `PATH`; in a repository checkout, use the built `cp3c` executable. |
| Entry file not found | Check the current directory, explicit source path, and `main` field in the nearest manifest. |
| Import not found | Check exact module spelling and whether it is a local module or an exported `std/*` module. |
| Type mismatch | Check the declared type, inferred type, `typeSystem`, and any numeric conversion settings. |
| Native compiler unavailable | Run `cp3 toolchain` and configure a supported compiler. |
| Native compile or link error | Preserve generated source and the full command; `cp3 check` does not run these later stages. |
| Program exits with an error | Distinguish a CP3 exception from a process exit status and inspect the source path shown by the runtime. |

## 10. A small next exercise

Add a typed function, call it from the entry, and check the project again:

```ts
function total(left: number, right: number): number {
  return left + right;
}

console.log("total:", total(20, 22));
```

The parameter and return annotations are checked by CP3. Change one argument to a string and run `cp3 check` to see the type diagnostic before native code generation.

## Related pages

- [For JavaScript and TypeScript developers](03-javascript-typescript-developers.md)
- [Bindings and declarations](../01-language/01-variables-and-bindings/01-bindings.md)
- [Type-system selection](../01-language/07-native-types/01-project-type-system.md)
- [CLI commands](../04-compiler-cli/01-cli-commands.md)
- [Compilation pipeline](../04-compiler-cli/02-compilation-pipeline.md)
