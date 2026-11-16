---
title: Packages and .cp3pak
---
# Packages and `.cp3pak`

A CP3 package is a library project — `"type": "package"` in `package.json`, with `export default { ... }` as its entry contract — whose build produces a single `.cp3pak` file instead of an executable. This page describes what a `.cp3pak` contains, how `cp3 install` behaves, and the rules for `cp3 publish`.

## Authoring a package

- `"type": "package"` — selects the package workflow.
- The entry module must export a default: `export default { ... }` is mandatory for `"type": "package"`.
- `build.files` — optional extra files to include in the `.cp3pak`, e.g. `{ "files": ["README.md"] }`.
- `build.cacheBinary` — **no longer used**. The `.cp3pak` format ships no binary cache; the build warns `build.cacheBinary is ignored` and ignores the field.

## What's inside a `.cp3pak`

A current-format `.cp3pak` contains exactly:

- **the entry's relative-import closure** — every `.cp3` module reachable from the entry through relative imports, stored as plain source at its authored path (the exact folder structure from the source tree);
- **`package.json`**, verbatim as the author wrote it;
- **`cp3pak.json`**, a small manifest carrying `formatVersion: 2` and `main` — equal to the `main` declared in `package.json`.

Nothing else: no generated signatures stub, no compiled `.cpp`/`.hpp` members, no per-platform binaries, no binary cache. A package is its source, the manifest that produced it, and a format marker.

## What install does

`cp3 install` places the package under `.cp3/packages/<name>/` and records it in `.cp3/lock.json`. The package's `.cp3` sources are **compiled by the consumer's own build**, alongside the consumer's modules — nothing in a `.cp3pak` is machine code, and nothing is linked against precompiled package objects. Because a package carries source rather than binaries, the same `.cp3pak` installs on any target platform.

### The delete-source prompt

For a local package file, `cp3 install <file.cp3pak>` asks **before installing** whether to remove the file afterwards:

```text
delete source package file '<path>' after successful install? [y/N]:
```

- The default is **No** — the file is kept unless you say otherwise.
- The question is asked only on an interactive terminal. With `--json`, or in a non-interactive shell, `cp3 install` never prompts and never deletes the file.
- The file is deleted only after a **successful** install, and only when you answered yes. If the install fails, the pak is never deleted.

### Older packages

Packages built through v0.2.5 used `formatVersion: 1` — a generated `index.cp3` signatures stub plus compiled members under `pak/`, linked against the shipped C++ with no consumer-side compile of package sources. Those packages are rejected at install time:

```text
unsupported .cp3pak formatVersion
```

with the remedy `rebuild the package with a newer cp3c`. Rebuild the package from its sources with a current compiler and distribute the new `.cp3pak`.

## Distributing a package

`cp3 publish [dir]` builds the package and reports (or copies) the resulting `.cp3pak`. There is no package registry — the `.cp3pak` file itself is the distribution: install it from a local path or directory, or as an asset of a GitHub release (`cp3 install <github-url>`).

## Linking instead of installing

`cp3 link <path>` links a local package directory or `.cp3pak` into the project; `cp3 unlink <name>` removes the link; `--list` lists links and `--dry-run` previews. See [CLI commands](./01-cli-commands.md).

## Related pages

- [CLI commands](./01-cli-commands.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Native packages and ABI boundaries](./07-native-interop.md)
- [Quick start](../quick-start.md)
