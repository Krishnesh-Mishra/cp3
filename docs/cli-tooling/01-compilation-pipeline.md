# 1. Compilation pipeline

C+++ does not need its own machine-code backend to start. It transpiles to C++ and lets an existing toolchain do the rest:

```
src/ (.cp3 files)
   ↓
C+++ compiler/transpiler
   ↓
.cp3/build/ (generated C++, mirroring project structure)
   ↓
platform-appropriate C++ toolchain (Clang / GCC / MSVC)
   ↓
native executable (.exe / binary)
```

An LLVM backend (skipping the C++ intermediate) is a possible future step, not a v1 requirement.

## Generated code targets C++20, minimum - not negotiable, not C++23

Async/await ([README.md §14](../language-spec/14-async.md)) compiles to real C++20 coroutines (`co_await`/`co_return`), so every toolchain in the fallback order below must support that, and the generated code itself never uses anything newer. C++20 is picked deliberately over C++23 even though a couple of newer conveniences exist - the goal is the smallest standard that makes coroutines work, not the newest available one; moving the floor forward later (if a real feature needs it) is cheap, walking it back after shipping isn't. Toolchain detection (below) needs to check for a compiler version with real C++20 coroutine support specifically, not just "any C++ compiler" - a compiler that's merely C++17-current isn't sufficient even if it otherwise runs. Exact minimum compiler versions (Clang/GCC/MSVC) should be pinned during implementation against whichever versions actually pass a coroutine smoke test, rather than assumed from memory here.

## One `.cpp`/`.hpp` pair per source file - never one giant `main.cpp`

Generating all output into a single flat C++ file risks real name collisions (two cp3 files both declaring a local with the same name, colliding statics, etc.) - exactly the kind of bug real C++ projects avoid by using one compilation unit per source file. cp3 does the same: each `.cp3` file transpiles to its own `.cpp`/`.hpp` pair, and the generated `.cp3/build/` tree mirrors the project's own folder structure.

```
src/
  index.cp3
  helpers/
    math.cp3

.cp3/build/
  index.cpp / index.hpp
  helpers/
    math.cpp / math.hpp
```

## `.cp3/build/` - incremental, not wiped every time

`.cp3/build/` is a real, persistent intermediate directory, not a scratch temp folder - a build only regenerates the `.cpp`/`.hpp` pair for `.cp3` files that actually changed since the last build, rather than wiping and regenerating the whole tree every time.

## The compile/link model: one object file per source file, stitched at the link step

Because each `.cp3` file becomes its own `.cpp`/`.hpp` pair (above), it also compiles to its **own object file** (`.o`/`.obj`) in `.cp3/build/`. A build is therefore two distinct phases:

1. **Compile** - every *changed* `.cp3` file is regenerated to C++ and compiled to its object file. Unchanged files reuse the object file already sitting in `.cp3/build/`; they are not recompiled.
2. **Link** - the toolchain's linker stitches *all* the project's object files, plus every library cp3 statically bundles (libuv, fmt, Abseil, uWebSockets, PCRE2, …), into the single executable (see **Static linking by default**, below).

This split is what makes the incremental build real: a one-file edit recompiles **one** object file and relinks, rather than recompiling the world. `cp3 run src/index.cp3` is exactly `cp3 build` followed by executing the result - and if nothing changed since the last build, both the compile and the link are skipped and the existing binary runs immediately.

**Fast relinking.** Linking is the one step that has to touch every object file, so it is where a slow linker is felt on *every* incremental rebuild. Where a fast linker is available (`lld`, `mold`, Apple's `ld64`), cp3 prefers it for the link step; if none is present, the toolchain's own linker is used.

## Build profiles: incremental by default, `--release` for whole-program optimization

cp3 has two build strategies, trading build speed against how much the optimizer can see:

- **`cp3 build` / `cp3 run` - incremental (the default).** Reuses the object files already in `.cp3/build/`: only `.cp3` files that changed are regenerated and recompiled, then everything is linked. Each translation unit is compiled with the toolchain's standard optimized setting (`-O2` on Clang/GCC, `/O2` on MSVC), but because each file is compiled *separately*, the optimizer only ever sees that one file. Fast rebuilds; not the most optimized binary.
- **`cp3 build --release` - whole-program (from scratch).** Ignores the incremental cache and compiles **every** translation unit afresh, then links with **Link-Time Optimization** (`-flto` on Clang/GCC; `/GL` + `/LTCG` on MSVC). With the whole program in view the optimizer can inline across files and drop code nothing reaches. This is the ship-the-product build - slower to produce, fastest to run.
- **`cp3 build --debug` / `cp3 run --debug` - unoptimized plus debug symbols** (`-O0`/`/Od` and `-g`/`/Zi`), incremental like the default. For step-through debugging and the fastest possible rebuilds while iterating; not a shipping profile.

**Why the default is the fast one, not the whole-program one:** development is dominated by edit-rebuild-run cycles, where *rebuild time* is what you feel, so the default reuses cached object files and pays the compiler only for what changed. Shipping happens once, where *runtime* is what matters, so `--release` spends the extra build time compiling everything together for maximum optimization. The incremental build already optimizes each file; what `--release` adds is *cross-file* context (LTO), not merely a higher `-O` level.

> This supersedes an earlier "defer LTO to Phase 3" position: LTO is part of beta one, scoped to `--release`. The fast incremental default deliberately does **not** enable it, so the daily loop stays fast ([§6](06-open-tooling-questions.md)).

## No CMake - cp3 drives the C++ compiler directly

cp3 never generates, invokes, or requires **CMake** (or any other build-system config). It calls the platform's C++ **compiler driver** directly (`cl.exe`, `clang++`, `g++`) with the flags it needs, for the two phases above. This is what keeps the project's founding promise true - you never learn a build system, never write a `CMakeLists.txt`, and never open a header file to build a cp3 project. The only build configuration that exists at all is the small `"build"` block in `package.json` ([§5.1](05-package-format-authoring-cp3pak.md)).

## Toolchain selection: fixed per-platform order, with silent fallback

Rather than benchmarking at build time, toolchain preference is a **fixed order per platform**:

- **Windows** - MSVC → Clang
- **macOS** - Apple Clang → (plain) Clang
- **Linux** - Clang

If the first-choice toolchain fails to compile due to an error, `cp3 build` **silently retries with the next toolchain in the list** - the user doesn't have to manually re-invoke the build with a different compiler.

## Build progress: numbered steps, not a percentage bar

Progress is shown as real pipeline stages (`Step 1 of 7`, `Step 2 of 7`, etc.) - e.g. parsing, type-checking, cp3→C++ codegen, invoking the toolchain. If a toolchain step fails and the build falls through to the next one in the fallback order, the total step count **grows to reflect the retry** (e.g. `Step 6 of 8`, then `Step 7 of 9` if it falls through again) - so the user visibly sees "this pipeline attempt failed, now trying the next one," rather than the build silently restarting with no feedback.

If **every** available toolchain in the fallback order fails, the final report shows the failures from **all of them together** (see [§2](02-error-diagnostics-source-mapping.md)) - more diagnostic signal than picking just one arbitrarily, especially useful when different toolchains fail for different reasons.

**Spinner rule across the whole CLI, not just builds:** any command expected to take longer than ~0.5 seconds (`cp3 build` on a cold cache, `cp3 native-install` parsing headers, downloading a toolchain) shows a progress spinner or step indicator; anything faster (`cp3 check` on a small project, `cp3 fmt`) shows nothing and simply completes, since a spinner on a near-instant command just flashes for a single frame and reads as glitchy rather than informative.

## Toolchain detection, and offering to install one

`cp3 init`, `cp3 build`, and `cp3 run` all check for an available C++ toolchain before doing anything that would need one:

- **Found** - proceed normally, using the fixed per-platform order above.
- **Not found** - tell the user plainly (no C++ toolchain detected), and offer to install one directly:

```
$ cp3 build
No C++ compiler found on this system.
Would you like to install Clang/LLVM now? [Y/n]
```

Accepting downloads and installs Clang/LLVM for the current platform and proceeds with the build - the person never has to leave `cp3` or go hunt down a C++ toolchain themselves just to get started.

## The CLI command surface: `run`, `check`, `dev`, `fmt`

A JS/TS developer's daily loop is `node app.js`, `tsc --noEmit`, and a watch-mode restart on save - the cp3 CLI needs the equivalent shape, not a two-step "compile, then separately invoke the binary" ceremony:

- **`cp3 run src/index.cp3`** - chains the full pipeline (parse → type-check → generate C++ → invoke toolchain → execute) in one command. Under the hood it's exactly `cp3 build` followed by running the result; the point is the person never has to type both.
- **`cp3 check`** - parses and type-checks only, then stops. No C++ generation, no invoking Clang/GCC/MSVC, no linking - the same idea as `tsc --noEmit` or `cargo check`. This is fast specifically because **the cp3 frontend itself is not the slow part** - parsing and type-checking a project is comparable in cost to any other language's frontend; what actually takes real time in a full build is invoking the external C++ compiler and linking, and `check` skips both entirely. This makes it fast enough to run on every save or every keystroke for typical project sizes - not a hard, unconditional guarantee against arbitrarily large codebases, where frontend work scales with project size same as any compiler's frontend does.
- **`cp3 dev`** - a file watcher that triggers `check` (or a rebuild) automatically on save.
- **`cp3 fmt`** - a zero-config, opinionated formatter (Prettier/`gofmt`/`rustfmt` role), with an optional `"format"` block in `package.json` for teams that want to override specifics like indent width or quote style. Pure tooling, touches no language semantics.
- **`cp3 --version`** (or `cp3 version`) - trivial, but every CLI needs one; confirmed as beta-one baseline rather than an afterthought.
- **`cp3 clean`** - wipes `.cp3/build/` (the generated-C++/incremental-compilation cache), for when it gets corrupted or a truly clean rebuild is needed. `--all` additionally wipes `.cp3/packages/`, forcing every dependency to be re-fetched/re-verified too.

`cp3 init` generates a `package.json` with standard `dev`/`build`/`start`/`check` scripts out of the box, so `npm run dev` and `npm start` work immediately, matching existing JS muscle memory rather than requiring a whole new CLI vocabulary.

## `cp3 exec <package> [args...]` - running a package's bundled tool (confirmed exception to "no further CLI commands")

**The gap this closes:** `"type": "package"` compiles to a `.cp3pak` ([§5](05-package-format-authoring-cp3pak.md)) - a library, not a runnable thing. That's fine for a package that only exports functions, but it leaves no way for a package to ship a *tool* a consuming project needs to actually run at build time (a codegen step, a scaffolder, a JSX-style source transformer). `cp3ui` (`CP3UI-README.md`) is the motivating case: its `.cpx`-to-`.cp3` transform has to run before `cp3 build` even starts, and there was no mechanism for that.

**The fix, npm-`bin`-shaped:**

```bash
cp3 exec cp3ui transform
```

1. Resolves the named package against what's already installed in `.cp3/packages/` ([§4](04-package-manager.md)) - `cp3 exec` does **not** install anything itself; the package must already be a project dependency.
2. Reads that package's `"bin"` field ([§5.1](05-package-format-authoring-cp3pak.md)) to find the declared entry file (e.g. `src/cli.cp3`).
3. Builds *just that one file* into an executable - this reuses `cp3 run`'s existing build-and-execute machinery (above), not a new build path.
4. Caches the built executable in `.cp3/packages/<package>/.bin/` - the build cost is paid once; subsequent `cp3 exec` calls for the same package (until its source changes) reuse the cached binary.
5. Runs it, passing everything after the package name through as `process.argv`.

Never modifies the consuming project's own dependency list - `cp3 exec` runs something already installed, it doesn't install it, mirroring the same run-don't-install distinction `npx` draws against `npm install`.

**Triggering it automatically from a normal build:** `package.json`'s `"scripts"` (already generated by `cp3 init`, above) honors npm-style **lifecycle hooks** - a script literally named `pre<name>` or `post<name>` runs automatically immediately before/after the script (or built-in command) named `<name>`. This is confirmed for beta one specifically so packages like `cp3ui` can wire themselves in with nothing beyond ordinary `package.json`:

```json
{
  "scripts": {
    "prebuild": "cp3 exec cp3ui transform",
    "build": "cp3 build"
  }
}
```

Running `cp3 build` (or `npm run build`) runs `prebuild` first automatically - the consuming project's author writes one line and never has to remember to invoke the transform manually.

**Why this is a beta-one exception, not a beta-one-scope violation:** [§6](06-open-tooling-questions.md) below previously closed the CLI surface with "no further commands needed for beta one." `cp3 exec` reopens that - a real, deliberate decision, not a free one - because it isn't a new subsystem: package resolution already exists (`cp3 install`, [§4](04-package-manager.md)), build-one-file-and-run already exists (`cp3 run`, above), and the incremental cache pattern already exists (`.cp3/build/`, above). It's those three existing pieces pointed at a dependency's `"bin"` entry instead of the project's own entry file, plus one small cache directory. Logged explicitly in [§6](06-open-tooling-questions.md) so it doesn't read as scope creep.

**Left for later, not required to unblock `cp3ui` or any other beta-one package:** fetching and running a tool *without* installing it first (an `npx`-style `cp3 exec github:<user>/<repo> init`, skipping `cp3 install` entirely). Every known beta-one use case for `cp3 exec` - including `cp3ui`'s transform step - operates on a package that's already an ordinary project dependency, so the installed-package path above is sufficient; the fetch-and-run-without-installing variant stays on the post-beta-one roadmap ([§6](06-open-tooling-questions.md)) rather than being pulled in speculatively.

**A standalone `cp3 doctor` (environment preflight command) is deprioritized, not required.** Toolchain detection is already mandatory and automatic on `cp3 init`/`build`/`run` (above) - a separate command that does the same check on demand is a nice-to-have convenience, not something beta one needs to ship as a first-class feature, since the core problem it would solve (silent setup failures) is already covered by the inline checks.

## `--max-memory` - a safety cap on the codegen phase, not a performance feature

A large project generating many `.cpp`/`.hpp` files in parallel ([§1](01-compilation-pipeline.md)) can use real, unpredictable amounts of RAM during the cp3→C++ codegen phase. `cp3 build`/`cp3 run` accept an optional cap:

```bash
cp3 build --max-memory 1024      # cap at 1024 MB
cp3 build --max-memory 70%       # cap at 70% of system RAM
```

With no flag, the default is **70% of system RAM** - a program shouldn't be able to accidentally OOM the machine it's building on just because a project happened to be large. Mechanically, this controls how many files get processed concurrently during codegen: if the estimated memory for the currently-desired parallelism would exceed the cap, the compiler processes files in smaller batches sequentially instead of failing outright - a build that would exceed the cap gets slower, not aborted.

## `cp3 dev`'s watch mode: incremental recompilation, never a second runtime

The instinct behind "I want a `nodemon`-like instant feel" is right, but there's a real trap worth naming explicitly so it doesn't get proposed again without this context: **do not build a second execution engine (a C++ interpreter/JIT, e.g. Cling) for dev mode while keeping compiled C++ for production.** This looks appealing for iteration speed, but it creates what's worth calling the **dual-runtime problem** - two different engines that will inevitably diverge in edge cases the user can't predict:

- **`import native` breaks in dev mode.** An interpreter has no straightforward way to call into a compiled native shared library the way generated C++ does - building a full FFI bridge for an interpreter is a much harder, generally incomplete problem of its own.
- **The memory model has to be replicated perfectly, or it silently isn't.** cp3's specific ARC + RAII + explicit-`weak` memory model ([README.md §10](../language-spec/10-memory-management.md)/[README.md §11](../language-spec/11-reference-cycles-the-weak-keyword.md)) would need to behave identically under a JIT/interpreter, or a reference cycle that's silently fine under a dev-mode GC becomes a real leak in prod, undetected until it's already shipped.
- **Floating-point and optimization differences.** A strictly-interpreted calculation and an aggressively-optimized compiled one aren't always bit-identical - a "works in dev, wrong in prod" class of bug with no good way to catch it early.

The actual fix for the speed problem doesn't require any of that risk: because cp3 already generates **one `.cpp`/`.hpp` pair per `.cp3` file** ([§1](01-compilation-pipeline.md)) with a **persistent incremental build cache** (`.cp3/build/`, above), a single-file save only needs that one file's translation unit recompiled and the binary relinked - not the whole project rebuilt from scratch. Pairing this with a fast linker (e.g. `mold`/`lld`) keeps a single-file edit-rebuild-restart cycle fast without ever running two different semantics for the same program. Same backend in dev and prod, zero divergence risk, full native interop and memory-model fidelity preserved - the tradeoff this avoids (a from-scratch interpreter reimplementing escape analysis and native FFI) would cost vastly more engineering than a file watcher wired to incremental compilation.

## Static linking by default - cp3 ships one file, not a folder of DLLs

`cp3 build`'s default output is a **single, statically-linked executable**. Every library cp3 itself bundles - libuv, fmt, Abseil, uWebSockets, libsodium, TBB, PCRE2, XXHash, CRC32C, Boost.Lockfree - is baked directly into the final binary at link time, not shipped as a separate `.dll`/`.so`/`.dylib` sitting next to it. This is a deliberate choice, not an accident of "however the linker defaults happen to work":

- **No missing-DLL failures.** A user can copy the single output file to another machine of the same OS/architecture and run it - no separate runtime, no `node_modules`-equivalent, no "it works on my machine" dependency drift.
- **Marginally faster, too, not just simpler.** Static linking avoids the DLL loader's per-call indirection (the Import Address Table) and lets the C++ compiler perform Link-Time Optimization *across* what would otherwise be separate library boundaries - inlining and dead-code elimination the compiler can't do across a real DLL boundary.
- **This only applies to cp3's own bundled dependencies.** A third-party library brought in via `import native` ([§4](04-package-manager.md)) may itself only be distributable as a shared library - plenty of vendor SDKs (GPU/CUDA drivers, proprietary hardware APIs) never ship source or a static `.lib` at all. In that case cp3 links against it dynamically, same as any C++ program would, and the resulting executable depends on that shared library being present on the target machine at runtime - this is a property of the third-party library, not something cp3 can static-link its way around.
- **cp3 does not output a shared library in beta one.** There is no `--target=dll`/`.so`-producing build mode - `cp3 build` produces an executable, full stop. Beta one's target audience (servers, CLI tools, desktop apps) needs to *ship* an app, not author a library for other native programs to link against; if someone specifically wants to build a plugin/library for an existing C++ program, that's a job for hand-written C++, not cp3, at least for beta one.

## `bundleNativeDlls` - copying a third-party native dependency's shared library next to the output

Since a dynamically-linked `import native` dependency (above) has to actually be present on the machine running the final executable, `package.json`'s `"build"` section ([§5.1](05-package-format-authoring-cp3pak.md)) accepts an option to automatically copy it there at build time, rather than leaving the user to hunt down and manually place the right file:

```json
"build": {
  "bundleNativeDlls": true
}
```

- **Default: `true`.** Most people building a deployable app want a self-contained output folder they can zip and hand to someone else without a "you also need to install X" instruction sheet.
- **What it does:** after linking, `cp3 build` copies every non-system shared library that any `import native` dependency actually needs into the output folder alongside the executable - `.dll` on Windows, `.so` on Linux, `.dylib` on macOS, detected automatically per target platform; the user writes the same `true` once and it does the right thing on every OS.
- **When to set it `false`:** a Docker/Linux server deployment where the shared library gets installed into the image directly (`apt`/`yum`) rather than copied by cp3; a huge SDK (a CUDA runtime library can be tens of megabytes) where an installer that checks prerequisites is a better fit than bundling it into every build; a CI pipeline that wants a minimal build artifact and installs runtime dependencies separately.
- **What this can't do:** bundling a shared library only guarantees the *library* is present - it says nothing about hardware or driver requirements the library itself might have (a bundled CUDA runtime library still needs an actual Nvidia GPU with a compatible driver on the target machine; no amount of file-copying changes that).

