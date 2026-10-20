# 5. Package format: authoring & `.cp3pak`

## 5.1 `cp3 init`

```bash
cp3 init
```

Prompts: project **type** (`application` or `package`) → **name** → **description** → **type-system pattern** (TypeScript / C++ / Rust - see [README.md §3](../language-spec/03-type-system.md)). Generates `src/`, `package.json`, and a hello-world entry file (`index.cp3` by default).

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

**`cp3 recompile`** - adds more platform binaries to an already-built `.cp3pak`, incrementally, from a different machine:

```bash
cp3 recompile mathkit.cp3pak
```

Run on a machine of a platform not yet cached (e.g. a Mac, after the package was originally built on Windows) - it reads the C++ source already inside the file, compiles it for the current machine's platform, and appends that binary into the same `.cp3pak`. No source re-fetch needed; the file simply grows to cover more platforms over time, one real machine per platform, with no cross-compilation toolchain required anywhere.

**Optional: `excludeSource`, an explicit opt-out from the default guarantee above.** The default is that a `.cp3pak` always carries its C++ source as a universal fallback ([§5.3](05-package-format-authoring-cp3pak.md)) - that default doesn't change. But for an author who has already committed to `cacheBinary` covering every platform they intend to support, carrying the source too makes the downloadable archive dead weight nobody needs: a heavy native-wrapping package (OpenCV-style bindings, say) could have tens of megabytes of generated C++ that a consumer receiving a matching precompiled binary will never touch or compile.

```json
"build": {
  "cacheBinary": ["windows-x64", "linux-x64", "mac-arm64"],
  "excludeSource": true
}
```

- **`index.cp3` is never excluded, regardless of this setting** - the IDE/LSP needs it for autocomplete and type-checking, and it's tiny (signatures only, no bodies) compared to the C++ source this option is actually meant to drop.
- **`cp3 build` refuses to produce a package with `excludeSource: true` and an empty or missing `cacheBinary` list** - that combination would make the package permanently uninstallable for every platform, which is never the intent; it's a build-time validation error, not a runtime surprise for whoever installs it later.
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
cp3 build      # → basickit.cp3pak
cp3 publish    # push to the registry, or:
cp3 link ./basickit.cp3pak   # local/private use
```

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

