# 5. Package format: authoring & `.cp3pak`

## 5.1 `cp3 init`

```bash
cp3 init
```

Prompts: project **type** (`application` or `package`) → **name** → **description** → **type-system pattern** (TypeScript / C++ / Rust - see [README.md §3](../language-spec/03-type-system.md)). Generates `src/`, `package.json`, and a hello-world entry file (`index.cp3` by default).

Interactive prompts appear only on a terminal and only without `--yes`. For scripts and CI, pass the answers explicitly:

```bash
cp3 init --yes --type package --type-system rust
```

`--type-system` accepts `typescript` (the default), `cpp`, or `rust`; the chosen value is written to `package.json` as `"typeSystem"`. Without a terminal or `--type-system`, the default is `typescript`.

**`package.json`**

```json
{
  "name": "mathkit",
  "version": "1.0.0",
  "description": "basic math helpers",
  "type": "package",
  "main": "src/index.cp3",
  "typeSystem": "typescript",
  "build": {
    "files": ["README.md"],
    "cacheBinary": ["windows", "linux"]
  }
}
```

- `"type": "application"` → `cp3 build` produces a native `.exe`.
- `"type": "package"` → `cp3 build` produces a `.cp3pak` (see below).
- `"main"` - the entry point; defaults to `index.cp3` (or `main.cp3`) if omitted. The compiler starts here and bundles only whatever is actually reached through `import`.
- `"typeSystem"` - which type-name vocabulary is the project default ([README.md §3](../language-spec/03-type-system.md)).
- `"toolchain"` - optional C++ compiler family, version and policy for this project. The preferred form is an object such as `{ "id": "gcc", "version": "16.1.0", "policy": "latest" }`; `latest` uses the newest installed version of the family and records the selected version, while `exact` pins that version for CI, releases, benchmarks or prebuilt native libraries. The legacy string `"clang"` and `{ "id": "clang" }` mean `latest`; a version without a policy keeps the older exact behavior. The field may also be an explicit path to a compiler driver. Written by `cp3 toolchain use <id[@version]> --project` or by the interactive `cp3 toolchain` selector's "This project" scope; the writer preserves every other byte of `package.json`. Resolution order is `--toolchain`, then `CP3_TOOLCHAIN`, then this field, then the global config, then automatic detection ([§1](01-compilation-pipeline.md)). Prefer a standard id so the project stays portable; a machine-specific path is only written when explicitly forced.
- `"build.files"` - optional extra files (README, license, etc.) to include in the package output alongside the code.
- `"build.cacheBinary"` - optional. See [§5.4](05-package-format-authoring-cp3pak.md).
- `"build.bundleNativeDlls"` - optional, default `true`. Relevant for `"type": "application"` projects specifically (a `"package"` doesn't produce a final deployable executable itself, so there's nothing to bundle alongside). When `true`, `cp3 build` copies every non-system shared library an `import native` dependency needs into the output folder next to the executable (`.dll`/`.so`/`.dylib`, auto-detected per platform) - full detail and when to set it `false` in [§1](01-compilation-pipeline.md).
- `"engines"` - optional, e.g. `{ "cp3": "^0.9.0" }`, npm-style semver range. Declares which `cp3` compiler versions this package is known to work with. Consumed by `cp3 install`'s dependency-version warning (if a package declares a range the installed compiler doesn't satisfy, install still proceeds but warns) and by `cp3 install <github-url> --auto`'s compatibility selection ([§4](04-package-manager.md)) when a release has multiple `.cp3pak` candidates.
- `"bin"` - optional, e.g. `{ "cp3ui": "src/cli.cp3" }`. Maps a command name to an ordinary `.cp3` file inside the package that is a runnable **program**, not a library export - it reads `process.argv`, does work, and exits. This is distinct from `"main"`: `"main"` is the library API a consumer `import`s; `"bin"` is a separate entry a consumer *runs*, via `cp3 exec` ([§1](01-compilation-pipeline.md)). A `.cp3pak` is fundamentally a library ([§5.3](05-package-format-authoring-cp3pak.md)) with no runnable output of its own - `"bin"` is what lets a package ship a build-time tool (a JSX transformer, a scaffolder, a codegen step) without that tool needing to be a whole separate published application. Most packages have no runnable tool and omit `"bin"` entirely; a package may declare more than one entry if it exposes more than one tool.

## 5.2 Authoring - the export object

A package's entry file exports a single default object; every function or value it offers is a property on it:

```cp3
// src/index.cp3
function add(a: number, b: number): number { return a + b; }
function sub(a: number, b: number): number { return a - b; }

export default { add, sub, PI: 3.14159 }
```

- Every key on the object becomes a named export.
- One optional reserved key, `default`, is the function invoked if the package itself is imported and called directly.
- There's no separate hand-maintained type file - the compiler extracts types straight from this same source (see [§5.3](05-package-format-authoring-cp3pak.md)).

**The `export default { ... }` statement itself is mandatory for `"type": "package"`** - if the main/index file has no `export default` at all, `cp3 build` fails and no `.cp3pak` is produced. (This is distinct from the optional `default` *key* inside the exported object, mentioned above, which controls whether the package is directly callable - that key itself stays optional.)

## 5.3 What's actually inside a `.cp3pak`

Building a package does **not** produce a compiled binary by default - it produces **C++ source**, transpiled from the cp3 source. The single downloadable `.cp3pak` file itself is a compressed archive (so the network transfer is small), but that compression exists **only** for the transfer - `cp3 install` decompresses it exactly once, and everything written to disk afterward is plain, ordinary, uncompressed files:

```
.cp3/packages/
  mathkit/
    package.json
    index.cp3       ← auto-generated: signatures/types only, no function bodies
    pak/
      *.cpp / *.hpp  ← plain, uncompressed C++ - decompressed once at install, not touched again
```

- **`index.cp3`** is generated automatically by the compiler by stripping the real source down to just its exported signatures and types - the author never writes or maintains this by hand. This is what an editor/LSP reads for autocomplete and type-checking, instantly, without needing to compile anything.
- **`pak/`** holds the actual logic as ordinary C++ - the consumer's own build compiles this like any other C++ source when they build their project, reading it straight off disk exactly like any other source file. **`cp3 build` never decompresses anything, ever** - that work happened once, at install time, not on every build; this is worth stating explicitly since "the `.cp3pak` file is compressed" could otherwise be misread as "the compiler has to decompress before every build," which would be real, repeated, unnecessary overhead this design specifically avoids. Shipping source (not a binary) means a package naturally works on whatever OS/architecture the consumer is building on - no per-platform binary is required just to install and use a package.

## 5.4 Optional: precompiled binary cache

Since every consumer compiling every dependency's C++ from scratch is real overhead, an author can opt in to shipping precompiled binaries alongside the source:

```json
"build": {
  "cacheBinary": ["windows", "linux", "mac"]
}
```

- When set, `cp3 build` additionally compiles the generated C++ into a real binary for whichever platforms are listed **that the build machine can actually target**, and bundles it into `pak/` next to the source.
- On `cp3 install`, if a cached binary matching the consumer's platform is present, it's used directly (fast path, no compile). If not present, the compiler falls back to compiling the included C++ source itself.
- Cross-compiling for every OS from one machine isn't realistic (each OS's toolchain differs), so `cacheBinary` only produces binaries for platforms the current build machine can already target - it does not force cross-compilation.

### 5.4.1 Platform ids and how they are normalized

A platform id is either a **family** name or an **arch-qualified** name:

| Form | Examples | Meaning |
| --- | --- | --- |
| family | `windows`, `linux`, `mac` | any architecture of that family |
| arch-qualified | `windows-x64`, `linux-x64`, `mac-arm64` | exactly that family and architecture |

Ids are normalized explicitly, so the same platform is always spelled the same way in `package.json`, in the archive layout and in the cache manifests:

- the case is folded (`Windows-X64` = `windows-x64`);
- the family aliases `win`, `win32`, `win64`, `winnt`, `mingw` fold to `windows`; `macos`, `macosx`, `osx`, `darwin`, `apple` fold to `mac`;
- the architecture aliases `x86_64`, `x86-64`, `amd64` fold to `x64`; `aarch64` folds to `arm64`; `i386`/`i486`/`i586`/`i686`/`ia32` fold to `x86`; `arm`, `armv6`, `armv7`, `armv7l`, `armhf` fold to `arm32`;
- an id that names no known family or architecture is a **manifest error**: `cp3 build` refuses it with a clear message rather than silently ignoring it.

`build.cacheBinary` accepts two forms:

- **`true`** - "the platform this build runs on". It resolves to the current machine's canonical id (e.g. `windows-x64`) at build time, and the resolved id is recorded in the archive, so the archive means the same thing on every machine.
- **an array of ids** - the platforms the package claims. A family id (`windows`) is satisfied by any cached artifact of that family; an arch-qualified id (`windows-x64`) only by that exact platform.

A family claim is *not* a cross-compilation request: `cp3 build` only ever produces the artifact of the platform it runs on. A claim for another platform is simply not satisfied yet - which is what `cp3 recompile` is for.

### 5.4.2 The archive layout

A package with a binary cache carries these members in addition to the usual ones (§5.3):

```
cp3pak.json                    container manifest; adds "cachePlatforms" and
                               "cacheClaims" when the package ships a cache
package.json
index.cp3                      never dropped
pak/<module>.cpp               the transpiled C++ (dropped by excludeSource)
pak/<module>.hpp               the transpiled C++ header (always kept)
pak/<platform>/cache.json      the cache manifest of that platform
pak/<platform>/<module>.o      the compiled artifact of that module
```

`<platform>` is the canonical arch-qualified id (`windows-x64`, `linux-x64`, `mac-arm64`). `cache.json` records the platform, the C++ ABI the artifact was built for, the artifact kind, the source and header hashes of every artifact, and the compiler identity (family, exact version, driver path) that built it.

The artifact itself is a plain object file, compiled from the package's own transpiled C++ with the same toolchain driver the rest of the build uses. It is deliberately **not** an LTO object, so it links into a consumer build that does not link with LTO, and the package's entry `main` is renamed while it is compiled, so the artifact is a library object and never collides with the consumer program's own entry point.

**`cp3 recompile`** - adds more platform binaries to an already-built `.cp3pak`, incrementally, from a different machine:

```bash
cp3 recompile mathkit.cp3pak
```

Run on a machine of a platform not yet cached (e.g. a Mac, after the package was originally built on Windows) - it reads the C++ source already inside the file, compiles it for the current machine's platform, and appends that binary into the same `.cp3pak`. No source re-fetch needed; the file simply grows to cover more platforms over time, one real machine per platform, with no cross-compilation toolchain required anywhere.

- Every other member of the archive is preserved, and so is every other platform's cache.
- The command is idempotent: an archive whose cache for the current platform is already current (same source, same header, same compiler) is left byte-identical.
- An archive that carries no C++ source (because `excludeSource` dropped it) is refused with a clear message - there is nothing left to compile.

**Optional: `excludeSource`, an explicit opt-out from the default guarantee above.** The default is that a `.cp3pak` always carries its C++ source as a universal fallback ([§5.3](05-package-format-authoring-cp3pak.md)) - that default doesn't change. But for an author who has already committed to `cacheBinary` covering every platform they intend to support, carrying the source too makes the downloadable archive dead weight nobody needs: a heavy native-wrapping package (OpenCV-style bindings, say) could have tens of megabytes of generated C++ that a consumer receiving a matching precompiled binary will never touch or compile.

```json
"build": {
  "cacheBinary": ["windows-x64", "linux-x64", "mac-arm64"],
  "excludeSource": true
}
```

- **`index.cp3` is never excluded, regardless of this setting** - the IDE/LSP needs it for autocomplete and type-checking, and it's tiny (signatures only, no bodies) compared to the C++ source this option is actually meant to drop. The C++ headers are kept too, so the archive still describes the package's interface.
- **`cp3 build` refuses to produce a package with `excludeSource: true` and an empty or missing `cacheBinary` list** - that combination would make the package permanently uninstallable for every platform, which is never the intent; it's a build-time validation error, not a runtime surprise for whoever installs it later.
- **The source is dropped only once the archive really carries the caches the package claims.** If `build.cacheBinary` names a platform this machine cannot target, `cp3 build` keeps the source (and says so) so `cp3 recompile` can add that platform's cache from another machine; the source is dropped as soon as the last claimed platform is cached - by the build itself, or by the `cp3 recompile` that adds the final cache.
- **The real, accepted cost:** a platform outside the author's `cacheBinary` list has no fallback at all for this package. `cp3 install` fails with a clear, specific message rather than a raw linker error:

```
✗ Installation failed for 'heavy-math-lib@1.0.0'
  No precompiled binary found for your platform (linux-arm32).
  The author excluded the C++ source from this package (excludeSource: true).
  Ask the author to add support for your platform, or to drop excludeSource.
```

This is a deliberate, visible trade an author opts into - smaller, faster installs for the platforms they've covered, at the cost of zero fallback for the ones they haven't - not a silent gap in the "install once, run anywhere" story most packages still get by default.


## 5.5 Importing and using a package

```cp3
import mathkit from "mathkit";

let s = mathkit.add(2, 3);   // 5
console.log(mathkit.PI);
```

```cp3
import { add, PI } from "mathkit";

let s = add(2, 3);
```

Both forms resolve against the same installed package - `import x from` binds the whole export object, `import { a, b }` destructures specific keys off it. Deliberately mirrors ES module ergonomics.

## 5.6 Example: a minimal user-authored package

```bash
cp3 init
# type: package, name: basickit
```

```cp3
// src/index.cp3
function add(a: number, b: number): number { return a + b; }
function sub(a: number, b: number): number { return a - b; }
function mul(a: number, b: number): number { return a * b; }
function div(a: number, b: number): number { return a / b; }

export default { add, sub, mul, div }
```

```bash
cp3 build      # → .cp3/out/basickit.cp3pak
cp3 publish    # build + report/copy the artifact; beta one has no registry push
cp3 publish --out ./dist   # copy basickit.cp3pak into ./dist
cp3 link ../basickit       # local/private use from a source checkout
```

`cp3 publish` builds the package exactly as `cp3 build` does and reports the absolute `.cp3pak` path, name, version, size, and SHA-256; `--out` copies it, and `--dry-run` only reports. To share the package, attach that `.cp3pak` to a public GitHub release. Consumers can then install it with `cp3 install https://github.com/<owner>/<repo>` (see [§4](04-package-manager.md)).

Consuming project:

```json
{ "dependencies": { "basickit": "1.0.0" } }
```

```cp3
import { add, mul } from "basickit";

let total = add(4, 5);
let product = mul(4, 5);
```

The consumer's editor shows full type info for all four functions immediately after `cp3 install` - via the auto-generated `index.cp3` - before any build ever runs.

