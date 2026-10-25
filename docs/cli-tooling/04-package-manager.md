# 4. Package manager

```bash
cp3 install ./basickit.cp3pak      # a local .cp3pak file or an unpacked package directory
cp3 install https://github.com/someuser/somelib  # the latest release of a public repo
cp3 install                        # installs everything already listed in package.json
```

Beta one has **no registry server**. A bare package name such as
`cp3 install some-utility-package` exits with a usage error and points at the
three supported sources: a local path, a GitHub release URL, or `cp3 link`.
The command never guesses that a package name is a directory on disk.

**`cp3 install` and `cp3 native-install` solve two different problems - they are not two names for the same thing.** This is worth stating plainly, since both commands touch "getting a C/C++ library working in cp3" and it's an easy pair to mix up otherwise:

- **`cp3 install <name-or-url>`** - installs a package **someone has already published** as a `.cp3pak`: either pure cp3 code, or a native library that someone already ran `cp3 native-install` on, packaged up, and shared ([§5](05-package-format-authoring-cp3pak.md) covers exactly how an author does that, including baking in generated bindings). This is the everyday path - most of the time, using a native library means installing an already-published package for it, the same as `npm install` assumes someone already published the package.
- **`cp3 native-install <sdk-path-or-name>`** (below) - generates bindings **from scratch, from raw C++ headers**, for a library nobody has packaged for cp3 yet, or that you're the one packaging. This is a less common, more involved path, and it's specifically for library *authors* bridging a new C/C++ library into the cp3 ecosystem for the first time - not the default way an application developer pulls in a dependency.

Qt and Unreal Engine specifically are **not realistic beta-one examples for either command** - full binding surfaces for both are explicitly out of scope for beta one (operator overloading, templates, and framework-specific machinery like Qt's MOC all need post-beta-one bindgen work, [§6](06-open-tooling-questions.md)). They're mentioned elsewhere in this spec as the long-term motivating use case for native interop, not as something `cp3 install qt` actually does today.

## Project layout: one hidden tooling directory, not several scattered ones

```
MyProject/
├── package.json
├── src/
│   └── index.cp3
└── .cp3/                  ← the only hidden/generated path - one line in .gitignore
    ├── packages/          ← installed .cp3pak packages (from `cp3 install`)
    ├── link/              ← extracted `.cp3pak` link cache (from `cp3 link`)
    ├── native/            ← generated native bindings (from `cp3 native-install`, below)
    │   └── opencv/
    │       ├── index.cp3  ← auto-generated declaration file (types/signatures only)
    │       └── lib/       ← recorded paths to the system's .lib/.so/.dll - not the binaries themselves
    ├── build/              ← generated C++ output (§1), one .cpp/.hpp pair per .cp3 file
    └── lock.json           ← pins exact resolved versions (replaces the earlier standalone `cp3.lock`)
```

An earlier draft of this doc put `.packages/`, `.build/`, and `cp3.lock` directly in the project root, alongside `package.json` and `src/`. Consolidating them under one `.cp3/` directory is a pure tooling/UX improvement, not a language-semantics change - core language rules are locked (see [README.md](../language-spec/00-overview.md)'s status note), project tooling layout isn't - it keeps the root clean, reduces `.gitignore` to a single `.cp3/` line instead of three separate entries, and means an IDE only ever needs to index `src/` and the lightweight generated `index.cp3` declaration files, never the generated C++ or full native headers.

**`package.json`**

```json
{
  "name": "MyGame",
  "version": "1.0.0",
  "dependencies": {
    "ue5": "5.8.0",
    "qt": "6.9.0"
  }
}
```

`.cp3/lock.json` pins exact resolved versions.

## Installing directly from a public GitHub repository

Alongside installing a named package, `cp3 install` also accepts a public GitHub repo URL directly:

```bash
cp3 install https://github.com/someuser/somelib
```

This checks that repo's **latest non-prerelease GitHub release** for `.cp3pak` assets attached to it:

- **Exactly one `.cp3pak` asset found** → downloaded and installed directly, no prompt.
- **More than one** (e.g. separate packages for different purposes in one release) → an interactive selector lists them by filename, and the user picks one - unless `--auto` is passed:

```bash
cp3 install https://github.com/someuser/somelib --auto
```

  `--auto` skips the prompt and picks automatically: it reads each candidate `.cp3pak`'s `package.json` `"engines"` field (below) and selects the one compatible with the currently-installed `cp3` compiler version, silently skipping any that require a version it doesn't have. If more than one candidate is still compatible after filtering, or if none declare an `"engines"` field at all, it falls back to the interactive selector rather than guessing - `--auto` resolves an unambiguous compatibility question, it doesn't invent an answer to an ambiguous one.
- **None found** → a clear error naming the repo and release tag, not a silent failure or a generic "not found."

This is scoped specifically to GitHub (using GitHub's own release API) for beta one, not a generic "any git URL" feature - keeping the implementation surface small and predictable. Downloads use the system HTTP client (`curl.exe` on Windows, `curl` on POSIX), never a bundled third-party library. `CP3_GITHUB_API_URL` overrides the API base for tests, and `CP3_GITHUB_TOKEN`, when set, adds an authorization header to API requests only; the token is never printed.

Whatever gets installed this way is recorded in `.cp3/lock.json` with the exact repo, release tag, asset filename, download URL, and a SHA256 hash of the downloaded bytes:

```json
{
  "lockfileVersion": 1,
  "packages": {
    "mathkit": {
      "version": "1.2.0",
      "source": "github",
      "repo": "someuser/mathkit",
      "tag": "v1.2.0",
      "asset": "mathkit-1.2.0.cp3pak",
      "resolved": "https://github.com/someuser/mathkit/releases/download/v1.2.0/mathkit-1.2.0.cp3pak",
      "integrity": "sha256:9f2c..."
    }
  }
}
```

Since a GitHub release isn't a curated registry with its own integrity guarantees the way `.cp3pak`'s normal install path can rely on, pinning the hash at install time means a later `cp3 install` (or a CI run) can detect if the same tag's asset ever silently changes, rather than trusting it blindly every time. Reinstalling the same repo/tag/asset with a different hash fails with an explanation; `--force` accepts the new bytes and updates the pin.

## Local links: `cp3 link` and `cp3 unlink`

`cp3 link` points a consuming project at a package you are developing locally. It records a lock entry without going through a registry:

```bash
cp3 link ../mathkit              # a package directory, used in place
cp3 link ./mathkit.cp3pak        # an artifact, extracted into .cp3/link/mathkit/
cp3 link ./mathkit.cp3pak --name mathkit-dev --force
cp3 link --list [--json]
cp3 unlink mathkit
```

- **Directory packages are not copied.** The lock records `"source": "link"`, `"kind": "directory"`, the absolute resolved path in both `resolved` and `target`, and the package version. Edits are visible to the next build.
- **A `.cp3pak` is immutable input.** `cp3 link` extracts it into `.cp3/link/<name>/` (a link cache, separate from `.cp3/packages/`) and records `"kind": "artifact"`, the artifact path, and its SHA-256. Re-running `cp3 link` on the same artifact replaces the cache when the bytes changed, and reuses the existing extraction when they did not. A cache path owned by a different link source is only replaced with `--force`.
- `cp3 link --list` prints name, version, kind, source, and target.
- `cp3 unlink <name>` removes only the lock entry and, for an artifact link, the `.cp3/link/<name>/` cache. It never deletes your source directory or the original `.cp3pak`.
- `cp3 install` (with no arguments) leaves linked entries untouched. Installing a package under a linked name fails unless `--force` is passed, which replaces the link.

## Publishing in beta one: artifact only

```bash
cp3 publish [<dir>] [--out <dir|file>] [--force] [--dry-run] [--json]
```

`cp3 publish` runs exactly the same package build as `cp3 build` for a `"type": "package"` project. The default output is `.cp3/out/<name>.cp3pak`; `--out` copies it to a directory or an explicit file. It prints the absolute artifact path, name, version, size, and SHA-256, and refuses to overwrite an existing `--out` target without `--force`. `--dry-run` reports what would be built and copied without building anything.

There is no upload path in beta one: **publishing means producing an artifact**. Attach the `.cp3pak` to a GitHub release yourself; consumers can then install it with `cp3 install https://github.com/<owner>/<repo>`. Registry push, version resolution, and authentication are post-beta work.

## Native/legacy packages - the offline bindgen tool

For an existing C/C++ library not written for C+++, rather than the compiler blindly trusting a raw `#include` and letting mismatches surface as opaque C++ errors, `cp3 native-install` does the type-extraction work **once, offline**, so the compiler never needs to guess at build time:

```bash
# From a system-installed SDK (headers found on standard search paths)
cp3 native-install opencv

# From a local SDK folder - must point at the folder containing include/, not a .dll/.so directly
cp3 native-install ./path/to/opencv-sdk
```

**Why it needs the headers, not the compiled library.** A `.dll`/`.so`/`.dylib` is compiled machine code with no embedded type information in C++ - unlike a .NET assembly, which carries full metadata. Every type, function signature, and struct layout a C++ library exposes lives in its **header files** (`.h`/`.hpp`), not the binary. So `cp3 native-install` needs to be pointed at the SDK's `include/` folder; it records the path to the actual `.lib`/`.so`/`.dll` separately, for the linker to resolve later at `cp3 build` time.

**How it works:** the tool bundles **libclang** (the parsing library underlying Clang, not the full compiler - the same approach Rust's own `bindgen` takes) to parse the target headers exactly once, and generates a pure `.cp3` declaration file - `.cp3/native/<lib>/index.cp3` - containing just the extracted function signatures and struct shapes, in ordinary cp3 syntax:

```cp3
// .cp3/native/opencv/index.cp3 - auto-generated by cp3 native-install, do not edit
namespace cv {
  class Mat { /* ... */ }
  function imshow(winname: string, mat: Mat): void;
}
```

Once this exists, the cp3 **frontend** - not the C++ compiler - type-checks native calls directly against it:

```cp3
import native "opencv" as cv;
cv.imshow("window", someString);  // ✗ cp3-native error, not a C++ template wall:
                                   //   Type 'string' is not assignable to parameter of type 'cv.Mat'
```

This is a genuine, direct cp3 type error - sourced entirely from the declaration file, with no C++ compilation step involved in producing it. It also means the same `index.cp3` file gives an editor real autocomplete for native calls for free, since it's just an ordinary `.cp3` file like any other - no C++-aware LSP required.

**This narrows, but doesn't remove, the need for the error-diagnostics fallback ([§2](02-error-diagnostics-source-mapping.md)).** Bindgen only covers the native-interop boundary specifically. It does nothing for a cp3-frontend bug that generates invalid C++ from otherwise-valid pure cp3 code, or an opaque error surfacing from cp3's own generated code for a language feature (a `std::variant` access mismatch, a coroutine state-machine error) - those categories still need the translation-and-fallback subsystem exactly as described in [§2](02-error-diagnostics-source-mapping.md). What changes is that native-interop mismatches move from "an opaque C++ error that needs translating" to "a cp3 type error that was never opaque in the first place" - a meaningfully smaller and different problem, not a reason to remove the safety net for everything else.

**One real limitation, stated plainly rather than glossed over:** a C++ header's type signature alone doesn't reliably say whether a returned pointer is owned or borrowed - that's usually a documentation-only convention, not something extractable from the header text itself. So `cp3 native-install` can fully automate simple value types (numbers, strings, plain structs) without any human involvement, but a function returning a raw pointer may need the binding author to manually annotate which row of the native pointer safety table (above) it falls into. This is a real, acknowledged gap for beta one, not a claim that native-install makes every API automatically and perfectly safe - it's consistent with beta one's native-interop scope already excluding the more complex signatures where this would matter most.

## Toolchain handling

Clang is the **target compiler toolchain** for beta one, not an ordinary dependency:

```
C+++ compiler → generated C++ → Clang (MinGW-w64 GNU on Windows) → native executable
```

Longer-term goal: bundle/manage an appropriate toolchain so users don't configure one manually. Beta one detects the newest usable Clang and offers to install it when missing; GCC and MSVC are not supported target compilers.

