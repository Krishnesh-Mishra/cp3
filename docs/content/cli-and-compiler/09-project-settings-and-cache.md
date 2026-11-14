---
title: Project settings and build cache
---
# Project settings and build cache

A CP3 project is its `package.json` and its entry point. This page explains the fields you can set, what lives under `.cp3/`, and how the incremental cache decides what to reuse.

## What `cp3 init` writes

```json
{
  "name": "hello-app",
  "version": "1.0.0",
  "description": "...",
  "author": "...",
  "type": "application",           // or "package"
  "main": "src/index.cp3",
  "typeSystem": "typescript",      // "typescript" | "cpp" | "rust"
  "scripts": {
    "start":  "cp3c run src/index.cp3",
    "verify": "cp3c check src/index.cp3",
    "build":  "cp3c build src/index.cp3"
  },
  "build": { "bundleNativeDlls": true }   // applications; packages get { "files": ["README.md"] }
}
```

## The fields you can set

| Field | Meaning |
| --- | --- |
| `name`, `version`, `description`, `author` | Project metadata; `version` and `name` flow into publish/install output. |
| `type` | `application` (default) or `package` — a package produces a `.cp3pak` on build, with `export default { ... }` as its mandatory library entry. |
| `main` | The entry file for bare commands (`cp3 run` with no argument). Overridable per command with an explicit path. |
| `typeSystem` | Type-name vocabulary: `typescript`, `cpp`, or `rust` (e.g. `u16` vs `uint16_t` unprefixed scalars). It changes checking, never codegen — output is always C++. |
| `strictNumericCasts` | Reject implicit cross-width numeric conversions that the default rules allow. |
| `build` | `bundleNativeDlls` (applications: bundle native binding DLLs next to the executable) or `files` (packages: extra files to include in the `.cp3pak`). |
| `scripts` | npm-style runnable entries; the CLI reads them directly (`cp3 start`) and comforts `pre<cmd>`/`post<cmd>` hooks for built-ins. |
| `toolchain` | A pin written by `cp3 toolchain use ... --project`: `{ "id": "clang", "version": "18", "policy": "latest" }` or `"exact"`. |
| `license`, other fields | Carried along; not used by the build. |

## Where everything lands

The project root is the nearest ancestor of the entry file containing `package.json`. Everything a build produces goes under `<root>/.cp3/`:

| Entry | Holds |
| --- | --- |
| `.cp3/build/` | Generated C++ for your modules and installed packages; nothing else compiles from it. |
| `.cp3/cache/` | Compiled objects (mirroring your source tree), stock runtime objects under `_runtime/`, the `build-manifest.txt`, the link stamp, and the link-arguments response file. |
| `.cp3/out/` | The linked executable, or a `.cp3pak` for packages. |
| `.cp3/packages/` | Installed CP3 packages (unpacked archives). |
| `.cp3/native/` | Native SDK bindings from `cp3 native-install`. |
| `.cp3/link/` | Extracted `.cp3pak` link caches (removed by `cp3 unlink`, never by `cp3 clean`). |
| `.cp3/lock.json` | Package lockfile with content hashes. |
| `.cp3/pgo/` | The auto-detected PGO profile used by release builds when present. |

`cp3 clean` removes `build`, `cache`, and `out` and keeps `native`. `cp3 clean --all` also removes `packages`, `native`, and `lock.json`. Use plain `clean` for a fresh (re)build; reach for `--all` only when you also want to reset installed dependencies.

## Incremental builds and reuse

Incremental builds (the default for `cp3 run` and `cp3 dev`) reuse prior artifacts when nothing relevant changed — across sessions. Reuse is decided from the cache manifest, keyed on the generated content, the profile, and the selected compiler identity. A change to your toolchain or the compiler itself retires affected objects; editing a file recompiles its module (plus, in the plural pipeline, anything whose interface hash moved) and relinks.

Practical notes:

- A no-change rebuild performs no native compile and no link.
- `cp3 warm` makes all of this eager — run it once before a long development session.
- The whole runtime include tree is fingerprinted as one unit: touching a runtime header invalidates every cached object, deliberately.
- A **release** build (`cp3 build`) does not reuse — it wipes `build`/`cache` first and rebuilds everything with optimization flags.
- `cp3 clean` is your reset button when something looks stale; it is a normal part of diagnosing cache problems, not an embarrassment.

## Contributor checklist

- Confirm you are invoking from the intended project root (the nearest `package.json` wins; an explicit path argument always overrides the manifest entry).
- Keep the profile fixed when comparing runs; `--incremental` and release numbers are different numbers.
- Record the toolchain identity (`cp3 toolchain current`) with any build you care about.
- Keep generated output out of source control (`.cp3/` is machine state; `cp3 clean` removes it).

## Related pages

- [CLI commands](./01-cli-commands.md)
- [Native toolchains](./05-native-toolchains.md)
- [Build profiles and PGO](./11-build-profiles-and-pgo.md)
- [How a build flows](./02-compilation-pipeline.md)
