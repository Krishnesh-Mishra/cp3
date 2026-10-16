# C+++ (`.cp3`) — CLI, Compiler & Tooling Reference

> Companion doc to `README.md` (the language spec). This document covers everything about the `cp3` command-line tool itself: the build/run pipeline, the diagnostics and crash-reporting subsystems, the package manager, `.cp3pak` format, and native-library bindgen. It has no language syntax in it — for `let`/classes/types/memory model/concurrency/etc., see `README.md`.

## Status

**Beta One.** Locked for implementation alongside `README.md` and `STDLIB-README.md`, under the same terms: build against this document directly, and stop to ask rather than silently assuming anything not already decided here.

---

## 1. Compilation pipeline

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

### Generated code targets C++20, minimum — not negotiable, not C++23

Async/await (README.md §14) compiles to real C++20 coroutines (`co_await`/`co_return`), so every toolchain in the fallback order below must support that, and the generated code itself never uses anything newer. C++20 is picked deliberately over C++23 even though a couple of newer conveniences exist — the goal is the smallest standard that makes coroutines work, not the newest available one; moving the floor forward later (if a real feature needs it) is cheap, walking it back after shipping isn't. Toolchain detection (below) needs to check for a compiler version with real C++20 coroutine support specifically, not just "any C++ compiler" — a compiler that's merely C++17-current isn't sufficient even if it otherwise runs. Exact minimum compiler versions (Clang/GCC/MSVC) should be pinned during implementation against whichever versions actually pass a coroutine smoke test, rather than assumed from memory here.

### One `.cpp`/`.hpp` pair per source file — never one giant `main.cpp`

Generating all output into a single flat C++ file risks real name collisions (two cp3 files both declaring a local with the same name, colliding statics, etc.) — exactly the kind of bug real C++ projects avoid by using one compilation unit per source file. cp3 does the same: each `.cp3` file transpiles to its own `.cpp`/`.hpp` pair, and the generated `.cp3/build/` tree mirrors the project's own folder structure.

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

### `.cp3/build/` — incremental, not wiped every time

`.cp3/build/` is a real, persistent intermediate directory, not a scratch temp folder — a build only regenerates the `.cpp`/`.hpp` pair for `.cp3` files that actually changed since the last build, rather than wiping and regenerating the whole tree every time.

### The compile/link model: one object file per source file, stitched at the link step

Because each `.cp3` file becomes its own `.cpp`/`.hpp` pair (above), it also compiles to its **own object file** (`.o`/`.obj`) in `.cp3/build/`. A build is therefore two distinct phases:

1. **Compile** — every *changed* `.cp3` file is regenerated to C++ and compiled to its object file. Unchanged files reuse the object file already sitting in `.cp3/build/`; they are not recompiled.
2. **Link** — the toolchain's linker stitches *all* the project's object files, plus every library cp3 statically bundles (libuv, fmt, Abseil, uWebSockets, PCRE2, …), into the single executable (see **Static linking by default**, below).

This split is what makes the incremental build real: a one-file edit recompiles **one** object file and relinks, rather than recompiling the world. `cp3 run src/index.cp3` is exactly `cp3 build` followed by executing the result — and if nothing changed since the last build, both the compile and the link are skipped and the existing binary runs immediately.

**Fast relinking.** Linking is the one step that has to touch every object file, so it is where a slow linker is felt on *every* incremental rebuild. Where a fast linker is available (`lld`, `mold`, Apple's `ld64`), cp3 prefers it for the link step; if none is present, the toolchain's own linker is used.

### Build profiles: incremental by default, `--release` for whole-program optimization

cp3 has two build strategies, trading build speed against how much the optimizer can see:

- **`cp3 build` / `cp3 run` — incremental (the default).** Reuses the object files already in `.cp3/build/`: only `.cp3` files that changed are regenerated and recompiled, then everything is linked. Each translation unit is compiled with the toolchain's standard optimized setting (`-O2` on Clang/GCC, `/O2` on MSVC), but because each file is compiled *separately*, the optimizer only ever sees that one file. Fast rebuilds; not the most optimized binary.
- **`cp3 build --release` — whole-program (from scratch).** Ignores the incremental cache and compiles **every** translation unit afresh, then links with **Link-Time Optimization** (`-flto` on Clang/GCC; `/GL` + `/LTCG` on MSVC). With the whole program in view the optimizer can inline across files and drop code nothing reaches. This is the ship-the-product build — slower to produce, fastest to run.
- **`cp3 build --debug` / `cp3 run --debug` — unoptimized plus debug symbols** (`-O0`/`/Od` and `-g`/`/Zi`), incremental like the default. For step-through debugging and the fastest possible rebuilds while iterating; not a shipping profile.

**Why the default is the fast one, not the whole-program one:** development is dominated by edit-rebuild-run cycles, where *rebuild time* is what you feel, so the default reuses cached object files and pays the compiler only for what changed. Shipping happens once, where *runtime* is what matters, so `--release` spends the extra build time compiling everything together for maximum optimization. The incremental build already optimizes each file; what `--release` adds is *cross-file* context (LTO), not merely a higher `-O` level.

> This supersedes an earlier "defer LTO to Phase 3" position: LTO is part of beta one, scoped to `--release`. The fast incremental default deliberately does **not** enable it, so the daily loop stays fast (§6).

### No CMake — cp3 drives the C++ compiler directly

cp3 never generates, invokes, or requires **CMake** (or any other build-system config). It calls the platform's C++ **compiler driver** directly (`cl.exe`, `clang++`, `g++`) with the flags it needs, for the two phases above. This is what keeps the project's founding promise true — you never learn a build system, never write a `CMakeLists.txt`, and never open a header file to build a cp3 project. The only build configuration that exists at all is the small `"build"` block in `package.json` (§5.1).

### Toolchain selection: fixed per-platform order, with silent fallback

Rather than benchmarking at build time, toolchain preference is a **fixed order per platform**:

- **Windows** — MSVC → Clang
- **macOS** — Apple Clang → (plain) Clang
- **Linux** — Clang

If the first-choice toolchain fails to compile due to an error, `cp3 build` **silently retries with the next toolchain in the list** — the user doesn't have to manually re-invoke the build with a different compiler.

### Build progress: numbered steps, not a percentage bar

Progress is shown as real pipeline stages (`Step 1 of 7`, `Step 2 of 7`, etc.) — e.g. parsing, type-checking, cp3→C++ codegen, invoking the toolchain. If a toolchain step fails and the build falls through to the next one in the fallback order, the total step count **grows to reflect the retry** (e.g. `Step 6 of 8`, then `Step 7 of 9` if it falls through again) — so the user visibly sees "this pipeline attempt failed, now trying the next one," rather than the build silently restarting with no feedback.

If **every** available toolchain in the fallback order fails, the final report shows the failures from **all of them together** (see §2) — more diagnostic signal than picking just one arbitrarily, especially useful when different toolchains fail for different reasons.

**Spinner rule across the whole CLI, not just builds:** any command expected to take longer than ~0.5 seconds (`cp3 build` on a cold cache, `cp3 native-install` parsing headers, downloading a toolchain) shows a progress spinner or step indicator; anything faster (`cp3 check` on a small project, `cp3 fmt`) shows nothing and simply completes, since a spinner on a near-instant command just flashes for a single frame and reads as glitchy rather than informative.

### Toolchain detection, and offering to install one

`cp3 init`, `cp3 build`, and `cp3 run` all check for an available C++ toolchain before doing anything that would need one:

- **Found** — proceed normally, using the fixed per-platform order above.
- **Not found** — tell the user plainly (no C++ toolchain detected), and offer to install one directly:

```
$ cp3 build
No C++ compiler found on this system.
Would you like to install Clang/LLVM now? [Y/n]
```

Accepting downloads and installs Clang/LLVM for the current platform and proceeds with the build — the person never has to leave `cp3` or go hunt down a C++ toolchain themselves just to get started.

### The CLI command surface: `run`, `check`, `dev`, `fmt`

A JS/TS developer's daily loop is `node app.js`, `tsc --noEmit`, and a watch-mode restart on save — the cp3 CLI needs the equivalent shape, not a two-step "compile, then separately invoke the binary" ceremony:

- **`cp3 run src/index.cp3`** — chains the full pipeline (parse → type-check → generate C++ → invoke toolchain → execute) in one command. Under the hood it's exactly `cp3 build` followed by running the result; the point is the person never has to type both.
- **`cp3 check`** — parses and type-checks only, then stops. No C++ generation, no invoking Clang/GCC/MSVC, no linking — the same idea as `tsc --noEmit` or `cargo check`. This is fast specifically because **the cp3 frontend itself is not the slow part** — parsing and type-checking a project is comparable in cost to any other language's frontend; what actually takes real time in a full build is invoking the external C++ compiler and linking, and `check` skips both entirely. This makes it fast enough to run on every save or every keystroke for typical project sizes — not a hard, unconditional guarantee against arbitrarily large codebases, where frontend work scales with project size same as any compiler's frontend does.
- **`cp3 dev`** — a file watcher that triggers `check` (or a rebuild) automatically on save.
- **`cp3 fmt`** — a zero-config, opinionated formatter (Prettier/`gofmt`/`rustfmt` role), with an optional `"format"` block in `package.json` for teams that want to override specifics like indent width or quote style. Pure tooling, touches no language semantics.
- **`cp3 --version`** (or `cp3 version`) — trivial, but every CLI needs one; confirmed as beta-one baseline rather than an afterthought.
- **`cp3 clean`** — wipes `.cp3/build/` (the generated-C++/incremental-compilation cache), for when it gets corrupted or a truly clean rebuild is needed. `--all` additionally wipes `.cp3/packages/`, forcing every dependency to be re-fetched/re-verified too.

`cp3 init` generates a `package.json` with standard `dev`/`build`/`start`/`check` scripts out of the box, so `npm run dev` and `npm start` work immediately, matching existing JS muscle memory rather than requiring a whole new CLI vocabulary.

### `cp3 exec <package> [args...]` — running a package's bundled tool (confirmed exception to "no further CLI commands")

**The gap this closes:** `"type": "package"` compiles to a `.cp3pak` (§5) — a library, not a runnable thing. That's fine for a package that only exports functions, but it leaves no way for a package to ship a *tool* a consuming project needs to actually run at build time (a codegen step, a scaffolder, a JSX-style source transformer). `cp3ui` (`CP3UI-README.md`) is the motivating case: its `.cpx`-to-`.cp3` transform has to run before `cp3 build` even starts, and there was no mechanism for that.

**The fix, npm-`bin`-shaped:**

```bash
cp3 exec cp3ui transform
```

1. Resolves the named package against what's already installed in `.cp3/packages/` (§4) — `cp3 exec` does **not** install anything itself; the package must already be a project dependency.
2. Reads that package's `"bin"` field (§5.1) to find the declared entry file (e.g. `src/cli.cp3`).
3. Builds *just that one file* into an executable — this reuses `cp3 run`'s existing build-and-execute machinery (above), not a new build path.
4. Caches the built executable in `.cp3/packages/<package>/.bin/` — the build cost is paid once; subsequent `cp3 exec` calls for the same package (until its source changes) reuse the cached binary.
5. Runs it, passing everything after the package name through as `process.argv`.

Never modifies the consuming project's own dependency list — `cp3 exec` runs something already installed, it doesn't install it, mirroring the same run-don't-install distinction `npx` draws against `npm install`.

**Triggering it automatically from a normal build:** `package.json`'s `"scripts"` (already generated by `cp3 init`, above) honors npm-style **lifecycle hooks** — a script literally named `pre<name>` or `post<name>` runs automatically immediately before/after the script (or built-in command) named `<name>`. This is confirmed for beta one specifically so packages like `cp3ui` can wire themselves in with nothing beyond ordinary `package.json`:

```json
{
  "scripts": {
    "prebuild": "cp3 exec cp3ui transform",
    "build": "cp3 build"
  }
}
```

Running `cp3 build` (or `npm run build`) runs `prebuild` first automatically — the consuming project's author writes one line and never has to remember to invoke the transform manually.

**Why this is a beta-one exception, not a beta-one-scope violation:** §6 below previously closed the CLI surface with "no further commands needed for beta one." `cp3 exec` reopens that — a real, deliberate decision, not a free one — because it isn't a new subsystem: package resolution already exists (`cp3 install`, §4), build-one-file-and-run already exists (`cp3 run`, above), and the incremental cache pattern already exists (`.cp3/build/`, above). It's those three existing pieces pointed at a dependency's `"bin"` entry instead of the project's own entry file, plus one small cache directory. Logged explicitly in §6 so it doesn't read as scope creep.

**Left for later, not required to unblock `cp3ui` or any other beta-one package:** fetching and running a tool *without* installing it first (an `npx`-style `cp3 exec github:<user>/<repo> init`, skipping `cp3 install` entirely). Every known beta-one use case for `cp3 exec` — including `cp3ui`'s transform step — operates on a package that's already an ordinary project dependency, so the installed-package path above is sufficient; the fetch-and-run-without-installing variant stays on the post-beta-one roadmap (§6) rather than being pulled in speculatively.

**A standalone `cp3 doctor` (environment preflight command) is deprioritized, not required.** Toolchain detection is already mandatory and automatic on `cp3 init`/`build`/`run` (above) — a separate command that does the same check on demand is a nice-to-have convenience, not something beta one needs to ship as a first-class feature, since the core problem it would solve (silent setup failures) is already covered by the inline checks.

### `--max-memory` — a safety cap on the codegen phase, not a performance feature

A large project generating many `.cpp`/`.hpp` files in parallel (§1) can use real, unpredictable amounts of RAM during the cp3→C++ codegen phase. `cp3 build`/`cp3 run` accept an optional cap:

```bash
cp3 build --max-memory 1024      # cap at 1024 MB
cp3 build --max-memory 70%       # cap at 70% of system RAM
```

With no flag, the default is **70% of system RAM** — a program shouldn't be able to accidentally OOM the machine it's building on just because a project happened to be large. Mechanically, this controls how many files get processed concurrently during codegen: if the estimated memory for the currently-desired parallelism would exceed the cap, the compiler processes files in smaller batches sequentially instead of failing outright — a build that would exceed the cap gets slower, not aborted.

### `cp3 dev`'s watch mode: incremental recompilation, never a second runtime

The instinct behind "I want a `nodemon`-like instant feel" is right, but there's a real trap worth naming explicitly so it doesn't get proposed again without this context: **do not build a second execution engine (a C++ interpreter/JIT, e.g. Cling) for dev mode while keeping compiled C++ for production.** This looks appealing for iteration speed, but it creates what's worth calling the **dual-runtime problem** — two different engines that will inevitably diverge in edge cases the user can't predict:

- **`import native` breaks in dev mode.** An interpreter has no straightforward way to call into a compiled native shared library the way generated C++ does — building a full FFI bridge for an interpreter is a much harder, generally incomplete problem of its own.
- **The memory model has to be replicated perfectly, or it silently isn't.** cp3's specific ARC + RAII + explicit-`weak` memory model (README.md §10/README.md §11) would need to behave identically under a JIT/interpreter, or a reference cycle that's silently fine under a dev-mode GC becomes a real leak in prod, undetected until it's already shipped.
- **Floating-point and optimization differences.** A strictly-interpreted calculation and an aggressively-optimized compiled one aren't always bit-identical — a "works in dev, wrong in prod" class of bug with no good way to catch it early.

The actual fix for the speed problem doesn't require any of that risk: because cp3 already generates **one `.cpp`/`.hpp` pair per `.cp3` file** (§1) with a **persistent incremental build cache** (`.cp3/build/`, above), a single-file save only needs that one file's translation unit recompiled and the binary relinked — not the whole project rebuilt from scratch. Pairing this with a fast linker (e.g. `mold`/`lld`) keeps a single-file edit-rebuild-restart cycle fast without ever running two different semantics for the same program. Same backend in dev and prod, zero divergence risk, full native interop and memory-model fidelity preserved — the tradeoff this avoids (a from-scratch interpreter reimplementing escape analysis and native FFI) would cost vastly more engineering than a file watcher wired to incremental compilation.

### Static linking by default — cp3 ships one file, not a folder of DLLs

`cp3 build`'s default output is a **single, statically-linked executable**. Every library cp3 itself bundles — libuv, fmt, Abseil, uWebSockets, libsodium, TBB, PCRE2, XXHash, CRC32C, Boost.Lockfree — is baked directly into the final binary at link time, not shipped as a separate `.dll`/`.so`/`.dylib` sitting next to it. This is a deliberate choice, not an accident of "however the linker defaults happen to work":

- **No missing-DLL failures.** A user can copy the single output file to another machine of the same OS/architecture and run it — no separate runtime, no `node_modules`-equivalent, no "it works on my machine" dependency drift.
- **Marginally faster, too, not just simpler.** Static linking avoids the DLL loader's per-call indirection (the Import Address Table) and lets the C++ compiler perform Link-Time Optimization *across* what would otherwise be separate library boundaries — inlining and dead-code elimination the compiler can't do across a real DLL boundary.
- **This only applies to cp3's own bundled dependencies.** A third-party library brought in via `import native` (§4) may itself only be distributable as a shared library — plenty of vendor SDKs (GPU/CUDA drivers, proprietary hardware APIs) never ship source or a static `.lib` at all. In that case cp3 links against it dynamically, same as any C++ program would, and the resulting executable depends on that shared library being present on the target machine at runtime — this is a property of the third-party library, not something cp3 can static-link its way around.
- **cp3 does not output a shared library in beta one.** There is no `--target=dll`/`.so`-producing build mode — `cp3 build` produces an executable, full stop. Beta one's target audience (servers, CLI tools, desktop apps) needs to *ship* an app, not author a library for other native programs to link against; if someone specifically wants to build a plugin/library for an existing C++ program, that's a job for hand-written C++, not cp3, at least for beta one.

### `bundleNativeDlls` — copying a third-party native dependency's shared library next to the output

Since a dynamically-linked `import native` dependency (above) has to actually be present on the machine running the final executable, `package.json`'s `"build"` section (§5.1) accepts an option to automatically copy it there at build time, rather than leaving the user to hunt down and manually place the right file:

```json
"build": {
  "bundleNativeDlls": true
}
```

- **Default: `true`.** Most people building a deployable app want a self-contained output folder they can zip and hand to someone else without a "you also need to install X" instruction sheet.
- **What it does:** after linking, `cp3 build` copies every non-system shared library that any `import native` dependency actually needs into the output folder alongside the executable — `.dll` on Windows, `.so` on Linux, `.dylib` on macOS, detected automatically per target platform; the user writes the same `true` once and it does the right thing on every OS.
- **When to set it `false`:** a Docker/Linux server deployment where the shared library gets installed into the image directly (`apt`/`yum`) rather than copied by cp3; a huge SDK (a CUDA runtime library can be tens of megabytes) where an installer that checks prerequisites is a better fit than bundling it into every build; a CI pipeline that wants a minimal build artifact and installs runtime dependencies separately.
- **What this can't do:** bundling a shared library only guarantees the *library* is present — it says nothing about hardware or driver requirements the library itself might have (a bundled CUDA runtime library still needs an actual Nvidia GPU with a compatible driver on the target machine; no amount of file-copying changes that).

## 2. Error diagnostics & source mapping

Raw C++ compiler errors (template instantiation walls, `std::variant`/`std::vector` type-mismatch errors, etc.) are notoriously unreadable, especially to a developer who's never learned C++ — which is exactly the audience cp3 targets. Surfacing those raw messages would break the whole "2-day onboarding" goal the first time someone hits a real type error.

**cp3 intercepts every C++ compiler error, maps it back to the originating `.cp3` source location, and rewrites it into a plain, cp3-native message** — not a raw C++ diagnostic. This requires:

- A **source map** from each generated line in `.cp3/build/*.cpp` back to its originating `.cp3` file/line/column
- A **translation layer** that recognizes common C++ error shapes (type mismatches, missing overloads, `std::variant` access errors, etc.) and rewrites them in terms of cp3's own types and syntax, not C++'s

**Scope, and why the size of this subsystem isn't what it used to be:** an earlier draft of this doc estimated this subsystem at "tens of thousands of lines," back when it had to cover every possible mismatch at the native-interop boundary too — the realistic size of translating every C++ library's own error shapes into cp3-native ones. That's no longer the job. Native interop now has its own type-checking (`README.md` §19, via `CLI-README.md` §4's `cp3 native-install`) — a native-boundary mismatch is caught as an ordinary cp3 type error against the generated declaration file, with no C++ compilation step involved, so there's nothing opaque left to translate there in the first place. What this subsystem still has to cover, and the reason it can't be removed entirely: (1) a cp3-frontend bug that generates invalid C++ from otherwise-valid pure cp3 code — a real compiler bug, expected to happen during beta one, and (2) opaque errors surfacing from cp3's own generated code for ordinary language features (a `std::variant` access mismatch from a union type, a C++20 coroutine state-machine error) — neither of which has anything to do with native interop. That's a genuinely smaller, more bounded problem than the original estimate — a fixed, fairly short list of language-feature-generated error shapes to translate, plus a labeled generic fallback for anything outside that list — not "thin wrapper" territory, but not tens of thousands of lines either. Treat the original estimate as historical, not current.

### Reporting across multiple toolchain fallback attempts

Since a build can fall through several toolchains before failing entirely (§1), the final error report groups by whether the underlying error is the **same** or **different** across the toolchains that hit it — never a raw stack trace, always the cp3-native translated form:

**Different errors per toolchain** — shown separately, each attributed to its toolchain:

```
✗ Build failed — 2 toolchains attempted

  Clang
    src/utils.cp3:14:8 — type mismatch: expected number, got string

  GCC
    src/utils.cp3:14:8 — cannot convert 'std::string' to 'double'
```

**Same underlying error across all attempted toolchains** — collapsed into one message, naming every toolchain that hit it:

```
✗ Build failed — MSVC + 3 others

  src/utils.cp3:14:8 — type mismatch: expected number, got string
```

**Decided for beta one:** a **clean, static, well-formatted printed report** — colored, readable, source-mapped — not a live-updating TUI. A TUI (live step progress, collapsible sections, closer to `cargo`) is real additional rendering-layer engineering on top of the diagnostics translation work, and isn't required to hit the "no raw C++ stack traces" bar. It stays on the post-beta-one roadmap as a pure UX upgrade — swapping the rendering layer later doesn't touch the diagnostics translation subsystem itself, so nothing here needs to be built twice.

**Diagnostics coverage for beta one:** the translation layer must cover, at minimum: type mismatches, undefined identifier/import errors, syntax errors, arity mismatches, and missing-member errors against a declared interface/abstract class shape. Any C++ error shape outside that list falls back to a clearly-labeled generic message ("Unmapped compiler error — raw backend output below") followed by the raw compiler output, rather than blocking the release on a fully exhaustive translation table. Expanding coverage past this list is ongoing, not a beta-one blocker.

### "Did you mean...?" suggestions

Where a diagnostic references a name that doesn't exist (a missing property, an unresolved identifier), the compiler checks it against a Levenshtein-distance-style similarity to what actually is in scope, and appends a suggestion when there's a close match:

```
✗ src/main.cp3:2:5 — Property 'puh' does not exist on type 'number[]'.
  Did you mean 'push'?
```

Cheap to implement, and standard in TypeScript/Rust for exactly this reason — it turns a dead-end error into an obvious one-character fix.

### Runtime crash reporting — a separate, much lighter mechanism from compile-time diagnostics

Everything above covers *build-time* errors. A **runtime** crash (an out-of-bounds array access, an unhandled native exception, a segfault) is a genuinely different problem, and needs its own answer — without one, a runtime crash would print a raw address and a `.cp3/build/*.cpp` line number, which breaks the "never show raw C++" promise just as thoroughly as an unmapped compile-time error would.

The fix here is much smaller than the compile-time translation layer: `cp3 build` emits an ordinary line-number **source map** (the same concept as a JS source map — mapping each generated `.cpp` line back to its `.cp3` file/line/column, already required for compile-time mapping above) embedded alongside the binary. A small runtime hook catches crashes and looks up the current location in that map before printing:

```
✗ Runtime panic: Array index out of bounds (index 5, length 3)
  at src/main.cp3:14:8
```

This is a straightforward lookup-table read, not a translation system — it requires no pattern-matching against C++ error shapes, since a source map entry either exists for a given line or it doesn't. Distinct subsystem from the compile-time error mapper above; don't conflate the two or assume runtime crash reporting requires anywhere near the same scope of work.

## 3. Internal library wrapping

Several std modules and internal runtime pieces are backed by established, high-performance C++ libraries — never exposed directly; the user only ever sees cp3's own API surface. Same pattern already established for `crypto` (libsodium) and `parallel.*` (TBB, README.md §13), extended to a few more pieces:

| Library            | Used for                                                                                                                                        | Where it surfaces to the user                                                                                                               |
| ------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| **Abseil**         | `flat_hash_map`/`InlinedVector` as an optional faster backend for `Map`/`Set`/internal containers, plus string utilities, synchronization, time | `std/collections`' `Map`/`Set` API stays identical whether backed by `std::unordered_map` or Abseil's `flat_hash_map`; also backs `string.replaceAll`/`.trimStart`/`.trimEnd` (`README.md` §9)    |
| **fmt**            | Fast, safe string formatting                                                                                                                    | Backend for `console.log` and string interpolation — user sees `console.log(...)`, never `fmt::format`                                      |
| **libuv**          | Single-threaded event loop: timers, async file/process I/O, background thread pool for blocking work                                          | Backend for `async`/`await`, `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval`, and every Promise-returning std call (`fs`, `fetch`, `process`, `worker`) — user sees `await`/`Promise`/`setTimeout`, never a libuv handle or callback directly (README.md §14) |
| **uWebSockets** (uWS, built on uSockets) | HTTP parsing/routing and WebSocket framing/handshake, in one C++ library, sharing a single event loop | Backend for `std/network`'s HTTP server and `WebSocket`/`WebSocketServer` — user sees `app.get(...)`/`res.json(...)`/`ws.on("message", ...)`, never uWebSockets directly. Superseded llhttp: since uWebSockets already has its own HTTP parsing built in, a separate parser is redundant once the server backend switched |
| **PCRE2**           | Regex engine with full JS regex feature parity (lookaheads, lookbehinds, backreferences)                                                        | Backend for native `/pattern/flags` regex literals (mentioned as a core-language builtin needing no import, README.md §20 above) — chosen over `std::regex` (notoriously slow, and missing some JS regex features) and over RE2 (faster and ReDoS-resistant, but doesn't support backreferences, so it can't give full JS parity). Adds roughly 150–300KB to a final stripped binary — negligible next to bundling a JS engine, and the reason regex stays a zero-import core-language literal rather than needing its own std module. |
| **XXHash**         | Extremely fast non-cryptographic hashing                                                                                                        | Internal `Map`/`Set` hashing, package checksums, cache keys                                                                                 |
| **CRC32C**         | Hardware-accelerated checksums                                                                                                                  | Internal integrity checks for `.cp3pak` contents and `.cp3/build/` incremental-compilation cache validity                                       |
| **Boost.Lockfree** | Lock-free queues/stacks                                                                                                                         | Internal runtime queues only (e.g. scheduling for `parallel.*`/async) — lock-free programming is never exposed as a user-facing cp3 concept |
| **libcds**         | Concurrent data structures (if internal lock-free needs exceed what Boost.Lockfree covers)                                                      | Same as above — internal high-concurrency runtime queues only, not a user-facing API                                                        |

None of these change any user-facing syntax or type anywhere in this spec — they're purely implementation choices for making the *compiler's own generated runtime code* fast, in the same way the compiler already generates calls into `std::vector`/`std::variant`/`std::shared_ptr` without the user ever writing those names themselves.

## 4. Package manager

```bash
cp3 install some-utility-package   # a package someone already published — pure cp3, or with native bindings baked in
cp3 install                        # installs everything already listed in package.json
```

**`cp3 install` and `cp3 native-install` solve two different problems — they are not two names for the same thing.** This is worth stating plainly, since both commands touch "getting a C/C++ library working in cp3" and it's an easy pair to mix up otherwise:

- **`cp3 install <name-or-url>`** — installs a package **someone has already published** as a `.cp3pak`: either pure cp3 code, or a native library that someone already ran `cp3 native-install` on, packaged up, and shared (§5 covers exactly how an author does that, including baking in generated bindings). This is the everyday path — most of the time, using a native library means installing an already-published package for it, the same as `npm install` assumes someone already published the package.
- **`cp3 native-install <sdk-path-or-name>`** (below) — generates bindings **from scratch, from raw C++ headers**, for a library nobody has packaged for cp3 yet, or that you're the one packaging. This is a less common, more involved path, and it's specifically for library *authors* bridging a new C/C++ library into the cp3 ecosystem for the first time — not the default way an application developer pulls in a dependency.

Qt and Unreal Engine specifically are **not realistic beta-one examples for either command** — full binding surfaces for both are explicitly out of scope for beta one (operator overloading, templates, and framework-specific machinery like Qt's MOC all need post-beta-one bindgen work, §6). They're mentioned elsewhere in this spec as the long-term motivating use case for native interop, not as something `cp3 install qt` actually does today.

### Project layout: one hidden tooling directory, not several scattered ones

```
MyProject/
├── package.json
├── src/
│   └── index.cp3
└── .cp3/                  ← the only hidden/generated path — one line in .gitignore
    ├── packages/          ← downloaded .cp3pak packages (from `cp3 install`)
    ├── native/            ← generated native bindings (from `cp3 native-install`, below)
    │   └── opencv/
    │       ├── index.cp3  ← auto-generated declaration file (types/signatures only)
    │       └── lib/       ← recorded paths to the system's .lib/.so/.dll — not the binaries themselves
    ├── build/              ← generated C++ output (§1), one .cpp/.hpp pair per .cp3 file
    └── lock.json           ← pins exact resolved versions (replaces the earlier standalone `cp3.lock`)
```

An earlier draft of this doc put `.packages/`, `.build/`, and `cp3.lock` directly in the project root, alongside `package.json` and `src/`. Consolidating them under one `.cp3/` directory is a pure tooling/UX improvement, not a language-semantics change — core language rules are locked (see `README.md`'s status note), project tooling layout isn't — it keeps the root clean, reduces `.gitignore` to a single `.cp3/` line instead of three separate entries, and means an IDE only ever needs to index `src/` and the lightweight generated `index.cp3` declaration files, never the generated C++ or full native headers.

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

### Installing directly from a public GitHub repository

Alongside installing a named package, `cp3 install` also accepts a public GitHub repo URL directly:

```bash
cp3 install https://github.com/someuser/somelib
```

This checks that repo's **latest non-prerelease GitHub release** for `.cp3pak` assets attached to it:

- **Exactly one `.cp3pak` asset found** → downloaded and installed directly, no prompt.
- **More than one** (e.g. separate packages for different purposes in one release) → an interactive selector lists them by filename, and the user picks one — unless `--auto` is passed:

```bash
cp3 install https://github.com/someuser/somelib --auto
```

  `--auto` skips the prompt and picks automatically: it reads each candidate `.cp3pak`'s `package.json` `"engines"` field (below) and selects the one compatible with the currently-installed `cp3` compiler version, silently skipping any that require a version it doesn't have. If more than one candidate is still compatible after filtering, or if none declare an `"engines"` field at all, it falls back to the interactive selector rather than guessing — `--auto` resolves an unambiguous compatibility question, it doesn't invent an answer to an ambiguous one.
- **None found** → a clear error naming the repo and release tag, not a silent failure or a generic "not found."

This is scoped specifically to GitHub (using GitHub's own release API) for beta one, not a generic "any git URL" feature — keeping the implementation surface small and predictable. Whatever gets installed this way is recorded in `.cp3/lock.json` with the exact repo, release tag, asset filename, and a SHA256 hash of the downloaded file — since a GitHub release isn't a curated registry with its own integrity guarantees the way `.cp3pak`'s normal install path can rely on, pinning the hash at install time means a later `cp3 install` (or a CI run) can detect if the same tag's asset ever silently changes, rather than trusting it blindly every time.

### Native/legacy packages — the offline bindgen tool

For an existing C/C++ library not written for C+++, rather than the compiler blindly trusting a raw `#include` and letting mismatches surface as opaque C++ errors, `cp3 native-install` does the type-extraction work **once, offline**, so the compiler never needs to guess at build time:

```bash
# From a system-installed SDK (headers found on standard search paths)
cp3 native-install opencv

# From a local SDK folder — must point at the folder containing include/, not a .dll/.so directly
cp3 native-install ./path/to/opencv-sdk
```

**Why it needs the headers, not the compiled library.** A `.dll`/`.so`/`.dylib` is compiled machine code with no embedded type information in C++ — unlike a .NET assembly, which carries full metadata. Every type, function signature, and struct layout a C++ library exposes lives in its **header files** (`.h`/`.hpp`), not the binary. So `cp3 native-install` needs to be pointed at the SDK's `include/` folder; it records the path to the actual `.lib`/`.so`/`.dll` separately, for the linker to resolve later at `cp3 build` time.

**How it works:** the tool bundles **libclang** (the parsing library underlying Clang, not the full compiler — the same approach Rust's own `bindgen` takes) to parse the target headers exactly once, and generates a pure `.cp3` declaration file — `.cp3/native/<lib>/index.cp3` — containing just the extracted function signatures and struct shapes, in ordinary cp3 syntax:

```cp3
// .cp3/native/opencv/index.cp3 — auto-generated by cp3 native-install, do not edit
namespace cv {
  class Mat { /* ... */ }
  function imshow(winname: string, mat: Mat): void;
}
```

Once this exists, the cp3 **frontend** — not the C++ compiler — type-checks native calls directly against it:

```cp3
import native "opencv" as cv;
cv.imshow("window", someString);  // ✗ cp3-native error, not a C++ template wall:
                                   //   Type 'string' is not assignable to parameter of type 'cv.Mat'
```

This is a genuine, direct cp3 type error — sourced entirely from the declaration file, with no C++ compilation step involved in producing it. It also means the same `index.cp3` file gives an editor real autocomplete for native calls for free, since it's just an ordinary `.cp3` file like any other — no C++-aware LSP required.

**This narrows, but doesn't remove, the need for the error-diagnostics fallback (§2).** Bindgen only covers the native-interop boundary specifically. It does nothing for a cp3-frontend bug that generates invalid C++ from otherwise-valid pure cp3 code, or an opaque error surfacing from cp3's own generated code for a language feature (a `std::variant` access mismatch, a coroutine state-machine error) — those categories still need the translation-and-fallback subsystem exactly as described in §2. What changes is that native-interop mismatches move from "an opaque C++ error that needs translating" to "a cp3 type error that was never opaque in the first place" — a meaningfully smaller and different problem, not a reason to remove the safety net for everything else.

**One real limitation, stated plainly rather than glossed over:** a C++ header's type signature alone doesn't reliably say whether a returned pointer is owned or borrowed — that's usually a documentation-only convention, not something extractable from the header text itself. So `cp3 native-install` can fully automate simple value types (numbers, strings, plain structs) without any human involvement, but a function returning a raw pointer may need the binding author to manually annotate which row of the native pointer safety table (above) it falls into. This is a real, acknowledged gap for beta one, not a claim that native-install makes every API automatically and perfectly safe — it's consistent with beta one's native-interop scope already excluding the more complex signatures where this would matter most.

### Toolchain handling

GCC/Clang/MSVC are treated as **compiler toolchains**, not ordinary dependencies:

```
C+++ compiler → generated C++ → Clang/GCC/MSVC → native executable
```

Longer-term goal: bundle/manage an appropriate toolchain so users don't configure one manually.

## 5. Package format: authoring & `.cp3pak`

### 5.1 `cp3 init`

```bash
cp3 init
```

Prompts: project **type** (`application` or `package`) → **name** → **description** → **type-system pattern** (TypeScript / C++ / Rust — see README.md §3). Generates `src/`, `package.json`, and a hello-world entry file (`index.cp3` by default).

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
- `"main"` — the entry point; defaults to `index.cp3` (or `main.cp3`) if omitted. The compiler starts here and bundles only whatever is actually reached through `import`.
- `"typeSystem"` — which type-name vocabulary is the project default (README.md §3).
- `"build.files"` — optional extra files (README, license, etc.) to include in the package output alongside the code.
- `"build.cacheBinary"` — optional. See §5.4.
- `"build.bundleNativeDlls"` — optional, default `true`. Relevant for `"type": "application"` projects specifically (a `"package"` doesn't produce a final deployable executable itself, so there's nothing to bundle alongside). When `true`, `cp3 build` copies every non-system shared library an `import native` dependency needs into the output folder next to the executable (`.dll`/`.so`/`.dylib`, auto-detected per platform) — full detail and when to set it `false` in §1.
- `"engines"` — optional, e.g. `{ "cp3": "^0.9.0" }`, npm-style semver range. Declares which `cp3` compiler versions this package is known to work with. Consumed by `cp3 install`'s dependency-version warning (if a package declares a range the installed compiler doesn't satisfy, install still proceeds but warns) and by `cp3 install <github-url> --auto`'s compatibility selection (§4) when a release has multiple `.cp3pak` candidates.
- `"bin"` — optional, e.g. `{ "cp3ui": "src/cli.cp3" }`. Maps a command name to an ordinary `.cp3` file inside the package that is a runnable **program**, not a library export — it reads `process.argv`, does work, and exits. This is distinct from `"main"`: `"main"` is the library API a consumer `import`s; `"bin"` is a separate entry a consumer *runs*, via `cp3 exec` (§1). A `.cp3pak` is fundamentally a library (§5.3) with no runnable output of its own — `"bin"` is what lets a package ship a build-time tool (a JSX transformer, a scaffolder, a codegen step) without that tool needing to be a whole separate published application. Most packages have no runnable tool and omit `"bin"` entirely; a package may declare more than one entry if it exposes more than one tool.

### 5.2 Authoring — the export object

A package's entry file exports a single default object; every function or value it offers is a property on it:

```cp3
// src/index.cp3
function add(a: number, b: number): number { return a + b; }
function sub(a: number, b: number): number { return a - b; }

export default { add, sub, PI: 3.14159 }
```

- Every key on the object becomes a named export.
- One optional reserved key, `default`, is the function invoked if the package itself is imported and called directly.
- There's no separate hand-maintained type file — the compiler extracts types straight from this same source (see §5.3).

**The `export default { ... }` statement itself is mandatory for `"type": "package"`** — if the main/index file has no `export default` at all, `cp3 build` fails and no `.cp3pak` is produced. (This is distinct from the optional `default` *key* inside the exported object, mentioned above, which controls whether the package is directly callable — that key itself stays optional.)

### 5.3 What's actually inside a `.cp3pak`

Building a package does **not** produce a compiled binary by default — it produces **C++ source**, transpiled from the cp3 source. The single downloadable `.cp3pak` file itself is a compressed archive (so the network transfer is small), but that compression exists **only** for the transfer — `cp3 install` decompresses it exactly once, and everything written to disk afterward is plain, ordinary, uncompressed files:

```
.cp3/packages/
  mathkit/
    package.json
    index.cp3       ← auto-generated: signatures/types only, no function bodies
    pak/
      *.cpp / *.hpp  ← plain, uncompressed C++ — decompressed once at install, not touched again
```

- **`index.cp3`** is generated automatically by the compiler by stripping the real source down to just its exported signatures and types — the author never writes or maintains this by hand. This is what an editor/LSP reads for autocomplete and type-checking, instantly, without needing to compile anything.
- **`pak/`** holds the actual logic as ordinary C++ — the consumer's own build compiles this like any other C++ source when they build their project, reading it straight off disk exactly like any other source file. **`cp3 build` never decompresses anything, ever** — that work happened once, at install time, not on every build; this is worth stating explicitly since "the `.cp3pak` file is compressed" could otherwise be misread as "the compiler has to decompress before every build," which would be real, repeated, unnecessary overhead this design specifically avoids. Shipping source (not a binary) means a package naturally works on whatever OS/architecture the consumer is building on — no per-platform binary is required just to install and use a package.

### 5.4 Optional: precompiled binary cache

Since every consumer compiling every dependency's C++ from scratch is real overhead, an author can opt in to shipping precompiled binaries alongside the source:

```json
"build": {
  "cacheBinary": ["windows", "linux", "mac"]
}
```

- When set, `cp3 build` additionally compiles the generated C++ into a real binary for whichever platforms are listed **that the build machine can actually target**, and bundles it into `pak/` next to the source.
- On `cp3 install`, if a cached binary matching the consumer's platform is present, it's used directly (fast path, no compile). If not present, the compiler falls back to compiling the included C++ source itself.
- Cross-compiling for every OS from one machine isn't realistic (each OS's toolchain differs), so `cacheBinary` only produces binaries for platforms the current build machine can already target — it does not force cross-compilation.

**`cp3 recompile`** — adds more platform binaries to an already-built `.cp3pak`, incrementally, from a different machine:

```bash
cp3 recompile mathkit.cp3pak
```

Run on a machine of a platform not yet cached (e.g. a Mac, after the package was originally built on Windows) — it reads the C++ source already inside the file, compiles it for the current machine's platform, and appends that binary into the same `.cp3pak`. No source re-fetch needed; the file simply grows to cover more platforms over time, one real machine per platform, with no cross-compilation toolchain required anywhere.

**Optional: `excludeSource`, an explicit opt-out from the default guarantee above.** The default is that a `.cp3pak` always carries its C++ source as a universal fallback (§5.3) — that default doesn't change. But for an author who has already committed to `cacheBinary` covering every platform they intend to support, carrying the source too makes the downloadable archive dead weight nobody needs: a heavy native-wrapping package (OpenCV-style bindings, say) could have tens of megabytes of generated C++ that a consumer receiving a matching precompiled binary will never touch or compile.

```json
"build": {
  "cacheBinary": ["windows-x64", "linux-x64", "mac-arm64"],
  "excludeSource": true
}
```

- **`index.cp3` is never excluded, regardless of this setting** — the IDE/LSP needs it for autocomplete and type-checking, and it's tiny (signatures only, no bodies) compared to the C++ source this option is actually meant to drop.
- **`cp3 build` refuses to produce a package with `excludeSource: true` and an empty or missing `cacheBinary` list** — that combination would make the package permanently uninstallable for every platform, which is never the intent; it's a build-time validation error, not a runtime surprise for whoever installs it later.
- **The real, accepted cost:** a platform outside the author's `cacheBinary` list has no fallback at all for this package. `cp3 install` fails with a clear, specific message rather than a raw linker error:

```
✗ Installation failed for 'heavy-math-lib@1.0.0'
  No precompiled binary found for your platform (linux-arm32).
  The author excluded the C++ source from this package (excludeSource: true).
  Ask the author to add support for your platform, or to drop excludeSource.
```

This is a deliberate, visible trade an author opts into — smaller, faster installs for the platforms they've covered, at the cost of zero fallback for the ones they haven't — not a silent gap in the "install once, run anywhere" story most packages still get by default.

### 5.5 Importing and using a package

```cp3
import mathkit from "mathkit";

let s = mathkit.add(2, 3);   // 5
console.log(mathkit.PI);
```

```cp3
import { add, PI } from "mathkit";

let s = add(2, 3);
```

Both forms resolve against the same installed package — `import x from` binds the whole export object, `import { a, b }` destructures specific keys off it. Deliberately mirrors ES module ergonomics.

### 5.6 Example: a minimal user-authored package

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

The consumer's editor shows full type info for all four functions immediately after `cp3 install` — via the auto-generated `index.cp3` — before any build ever runs.

## Post-beta-one roadmap (not beta-one requirements — captured so good thinking isn't lost)

Extensive design conversations produced a large set of genuinely good ideas that were explicitly phased out of beta one rather than rejected — captured here as a roadmap, not a commitment to build them next, so they don't need re-deriving from scratch later and don't get silently conflated with what beta one actually requires. The phasing logic: **Phase 1 (beta one) is correctness — it works, it ships. Phase 2 is DX/QOL — it feels great to use. Phase 3 is optimization — faster than C++ sometimes.** Optimizing before the language is even correct and pleasant to use is the classic compiler-project trap; this phasing is a deliberate guard against it.

**One item confirmed to need *no* engineering at all, not even later:** liveness-based "early drop" (destroying a value at its last use rather than scope-end) doesn't need to be built manually — C++'s own RAII already destroys at scope-end correctly, and Clang/MSVC's own optimizer already narrows this to last-use at `-O2`/`-O3` in many cases. There's nothing here for the cp3 compiler to implement; scope-end destruction is already correct and sufficient.

**Phase 2 (DX/QOL):**
- `cp3 exec github:<user>/<repo> [cmd]` — an `npx`-style extension to `cp3 exec` (§1) that fetches and runs a package's `"bin"` entry (§5.1) **without** installing it into the project's dependencies first. Deferred because every known beta-one use of `cp3 exec` — including `cp3ui`'s `.cpx` transform — operates on a package that's already an ordinary installed dependency; this variant only matters for one-off tool invocations, not for wiring a build.
- LSP / IDE autocomplete + type info (deferred from beta one)
- Debugger integration (e.g. VS Code)
- Build-progress TUI, cargo-style (deferred from beta one — §2's static report ships first)
- Faster `cp3 dev` incremental loop (tighter watch cycle, beyond what beta one's base incremental compilation already gives)
- Expanded "Did you mean...?" / richer error-message coverage beyond §2's beta-one minimum category list
- More stdlib convenience methods, as real usage surfaces gaps

**Phase 3 (optimization):**
- Loop/chain fusion (`.map().filter().reduce()` compiling to a single pass instead of allocating an intermediate array per step) — a genuine case where cp3 can beat typical hand-written C++, since a C++ compiler can't fuse a chain it doesn't know came from one
- Refcount elision — skipping retain/release entirely for a synchronous, provably-non-escaping call, passing a raw reference instead (the same borrow-instead-of-retain idea as `README.md` §13's `parallel.*` captures, applied more generally; with every object refcounted atomically (`README.md` §10), this is the main optimization lever for hot paths, and is still purely an optimization, never needed for correctness)
- Bounds-check elimination in release builds (debug builds keep them; this is the release-mode relaxation)
- `typeof`-narrowed union access skipping the runtime `std::variant` tag check inside a narrowed branch, once the compiler has already proven the concrete type
- Safe auto-parallelization — automatically applying `parallel.for` semantics to a plain loop, but *only* for the one provably-safe shape (writes through the loop's own index, README.md §13) — never a general "guess and parallelize" pass, since that's a correctness landmine, not an optimization
- Whole-program tree shaking as a distinct cp3 pass (dropping unreachable modules/packages before codegen, beyond what LTO's link-time dead-code elimination already removes) — LTO itself is beta one's `--release` path (§1)
- Incremental frontend caching (cache parsed AST/type info per file, not just generated C++) so `cp3 check` stays fast even as a project grows large

**The honest framing for "faster than C++ sometimes," worth stating alongside the roadmap rather than just as a slogan:** cp3 won't beat hand-tuned, `-O3`, expert-written C++ across the board — that's not a realistic claim. It can beat *typical* C++ specifically in the abstraction-heavy cases above (fluent chains, forgotten bounds-check removal, forgotten parallelization opportunities) — because the compiler sees high-level intent a C++ compiler never gets to see once that intent has already been thrown away into raw loops and `std::vector` calls. That's a real, defensible edge, not hype — but it's a Phase 3 edge, not a beta-one claim.

**Qt integration — a project-owner-committed follow-up, not a generic Phase 2/3 idea:** unlike the prioritized-but-unowned items above, Qt has a confirmed post-beta-one plan with an owner. Worth separating clearly from "templates/operator overloading get added to bindgen later," because that alone would not be enough — Qt fails at a more fundamental layer:

- **Why lifting the templates/operator-overloading limitation wouldn't fix Qt on its own.** Qt's signal/slot/property system (`Q_OBJECT`, `signals:`, `slots:`, `Q_PROPERTY`) isn't real C++ — it's Qt's own syntax, processed by Qt's separate MOC (Meta-Object Compiler) tool *before* the real C++ compiler ever runs. `libclang` (what `cp3 native-install` is built on, above) can't parse these macros into anything meaningful, and the metadata they describe doesn't exist as a header could ever expose it — it only exists in MOC's generated output, which isn't produced until MOC runs. So bindgen fails on `Q_OBJECT` before templates or operators ever become the blocker.
- **Confirmed direction:** rather than waiting on or extending automatic bindgen to somehow cover this, the plan is to hand-author a dedicated `.cp3pak` Qt bridge post-beta-one — plain, manually-written C++ wrapper code exposing a simple C API, not `cp3 native-install` output. (This is the same "third-party `.cp3pak` package" path already left open for `std/webview` in `STDLIB-README.md`, applied to Qt specifically.)
- **What that hand-written bridge will need to solve, none of it bindgen's job:** running MOC as an explicit pre-build step and linking its generated files; bridging Qt's own event loop (`QCoreApplication::exec()`) with cp3's libuv-based one (§14 of `README.md`) — the two can't coexist naively in one process, most likely solved by running Qt on its own thread with careful GUI-call thread-safety, since Qt widget calls aren't thread-safe from any other thread; converting between cp3's UTF-8 strings (`README.md` §9) and Qt's UTF-16 `QString` at every boundary; and deliberately wrapping only a subset of Qt's ~1,500+ classes rather than the full API surface, at least to start.
- **Scope note:** this is a real roadmap item with an owner and a rough plan, logged here so it isn't re-derived from scratch later — not a beta-one deliverable, and not a claim that bindgen itself will ever auto-generate Qt bindings.

**Not to be confused with `cp3ui`.** The hand-authored Qt bridge above is for a project that specifically wants real Qt. It is a separate, unrelated effort from `cp3ui` — the project's own React-like native UI framework (`CP3UI-README.md`) — which deliberately does **not** use Qt at all. `cp3ui` renders through **Slint** instead, chosen specifically because it avoids everything that makes Qt hard here: no MOC preprocessor to fight `libclang`-based bindgen, no 25–45 MB baseline footprint, and a C++ API that links into cp3's generated C++ the ordinary way. Both efforts happen to solve "desktop UI for cp3," which is exactly why this note exists — so the Qt bridge's existence here doesn't get read as `cp3ui`'s rendering backend, or vice versa.

## 6. Open tooling questions

**Resolved since the initial draft** (kept here for history, tooling/CLI/package-manager decisions only — see the language spec's own history for everything else): `.cp3pak` internals (compressed C++ source + auto-generated `index.cp3`, not a compiled binary by default) · optional per-platform binary cache (`cacheBinary` + `cp3 recompile`) · `cp3 init` workflow (now 4 questions, including type-system pattern) · arrays explicitly confirmed homogeneous by default, mixed types require an explicit union — not "free" just because vectors are used underneath (README.md §9) · compiler build architecture (`.build/` persistent incremental output directory mirroring project structure, one `.cpp`/`.hpp` pair per `.cp3` file — never one flat `main.cpp` — fixed per-platform toolchain order: MSVC→Clang→GCC on Windows, Apple Clang→Clang→GCC on macOS, Clang→GCC on Linux, with silent fallback retry on failure) · error diagnostics as a first-class subsystem (source-mapped, cp3-native error messages instead of raw C++ compiler output; step-based build progress, not a percentage bar, with the step count growing on toolchain fallback; multi-toolchain failures grouped by same-vs-different underlying error, §2) · prelude file stacking order for multiple files in one folder (`_prelude.same.cp3` → `_prelude.cp3` → `_prelude.weak.cp3`, README.md §18; strong + weak coexisting in one folder confirmed as allowed, not a conflict — each follows its own independent rule) · **compiler implementation language decided: C++**, not TypeScript or another host language — dogfooding, and consistent with the project's own no-GC/no-bundled-runtime performance stance · **internal library wrapping specified**: Abseil (optional faster `Map`/`Set` backend), fmt (`console.log`/string formatting backend), XXHash (internal hashing), CRC32C (`.cp3pak`/`.build` cache integrity checks), Boost.Lockfree/libcds (internal lock-free runtime queues only, never a user-facing concept) — same "wrap proven C++ libraries invisibly" pattern as libsodium/TBB, README.md §20.

**Resolved for beta one** (closing out items that were previously open — tooling/CLI/package-manager decisions; see `README.md`'s own history for language decisions):

- **CLI surface confirmed sufficient for beta one, plus three additions**: `cp3 --version`/`cp3 version`, `cp3 clean` (wipes `.cp3/build/`; `--all` additionally wipes `.cp3/packages/`), and `cp3 exec <package> [args]` (§1) — a **confirmed exception**, not a reopening of the general question. `cp3 exec` is required because `"type": "package"` (§5) has no runnable output at all, which blocks any package (`cp3ui` being the first real one, `CP3UI-README.md`) that needs to ship a build-time tool rather than only a library. It's implemented as existing pieces (`cp3 install`'s resolution, `cp3 run`'s build-and-execute, `.cp3/build/`'s cache pattern) pointed at a dependency's new `"bin"` field (§5.1), not a new subsystem. `cp3 upgrade`/`cp3 config`/`cp3 profile` and similar stay explicitly post-beta-one, as does the `npx`-style fetch-and-run-without-installing form of `cp3 exec` (roadmap, above) — no known beta-one package needs it.
- **`package.json` lifecycle script hooks confirmed for beta one**: a script named `pre<name>`/`post<name>` runs automatically immediately before/after the script or built-in command named `<name>` (npm's own convention, adopted as-is) — e.g. a `"prebuild"` script runs before `cp3 build`. This is what lets `cp3 exec cp3ui transform` (above) wire itself into an ordinary build with one `package.json` line, with no new compiler or CLI mechanism beyond the hook-naming convention itself.
- **`--max-memory` flag added for `cp3 build`/`cp3 run`** — caps codegen-phase RAM usage (absolute MB or a percentage of system RAM), defaulting to 70% of system RAM so a large project can't accidentally OOM the machine building it. A build that would exceed the cap processes files in smaller sequential batches rather than failing outright.
- **Static linking confirmed as the default build output, with a documented exception for third-party native dependencies, and `bundleNativeDlls` added to automate the exception.** cp3's own bundled libraries are always statically linked. A dynamically-linked `import native` dependency needs its shared library present on the machine running the final executable — `"build.bundleNativeDlls"` (default `true`) automates copying it into the output folder at build time, cross-platform (`.dll`/`.so`/`.dylib`, auto-detected). Full detail in §1 and §5.1.
- **Build strategies and toolchain acquisition confirmed for beta one** (§1). `cp3 build`/`cp3 run` are **incremental** by default: they reuse the cached object files in `.cp3/build/`, recompile only the `.cp3` files that changed, and link — each translation unit compiled with the toolchain's optimized setting (`-O2`/`/O2`), though without cross-file context. **`cp3 build --release`** compiles everything from scratch and links with **LTO** for whole-program optimization (the ship build); **`cp3 build --debug`** gives `-O0`/`/Od` plus debug symbols. LTO is therefore **in beta one**, scoped to `--release` — the fast incremental default deliberately does not enable it. The C++ toolchain used is the **system** one (fixed per-platform order, silent fallback, **Toolchain detection** above), never a bundled or pinned compiler; when none is found, `cp3` offers to download Clang rather than failing. cp3 **never uses CMake** — it drives the compiler driver directly.
- **`std/network`'s server/WebSocket backend switched from llhttp to uWebSockets**, superseding the earlier llhttp-based `std/server` decision — uWebSockets already includes its own HTTP parsing and handles WebSocket framing/handshake in the same event loop, making a separate HTTP parser redundant once the server backend itself changed. Internal library table (§3) updated accordingly.
- **A large set of Phase 2/3 compiler-engineering and DX ideas (LSP, debugger, loop fusion, refcount elision, bounds-check elimination, safe auto-parallelization, whole-program tree shaking, incremental frontend caching, and more) explicitly deferred past beta one, at the project's own direction** — captured as a documented roadmap (above) rather than either silently dropped or accidentally treated as beta-one requirements. One item confirmed to need no engineering at all, ever: liveness-based "early drop" — C++'s own RAII plus the C++ compiler's own optimizer already handle this correctly without any manual cp3-compiler pass.

- **`.cp3pak`'s compression scope clarified — a real ambiguity, not just a wording nitpick.** The layout-once-installed diagram previously showed the on-disk `pak/*.cpp`/`*.hpp` files as "highly compressed," which could be misread as "the compiler decompresses on every build." Fixed: compression exists only in the single downloadable `.cp3pak` archive, for the network transfer; `cp3 install` decompresses it exactly once, and `cp3 build` reads plain, ordinary files off disk like any other C++ source, no decompression step involved, ever (§5).

- **Error report format** (§2): static, well-formatted printed report — not a live TUI — for beta one; see §2 for the decision and reasoning.
- **Error diagnostics coverage** (§2): minimum required category list specified directly in §2, with a labeled generic fallback for everything else.
- **`.cp3pak` compression & versioning**: gzip-compressed archive; a manifest header inside the pak carries an integer `formatVersion` field, incremented only when the `.cp3pak` container format itself changes — independent of, and unrelated to, the package's own semver `version`.
- **Debugger / LSP / IDE tooling**: explicitly **out of scope for beta one**. Beta one is compiler + std lib + CLI only; tooling is a deliberate follow-on, not a hidden dependency of beta one shipping.
- **C++ standard floor raised to C++20**, specifically for coroutine support — was previously unspecified anywhere in this doc. Not C++23; the smallest standard that makes coroutines work, on purpose (§1).
- **`libuv` added to the internal-library wrapping table** (§3), alongside libsodium/TBB/fmt/etc. — same "wrap it invisibly, never expose it" pattern.
- **Offline bindgen tool added for native interop (`cp3 native-install`)**, superseding the earlier "cp3 blindly transpiles native calls, C++ compiler catches mismatches" model as the primary path. Bundles **libclang** (parsing library only, not the full compiler — same approach as Rust's own `bindgen`) to parse a native library's headers once, offline, generating a `.cp3/native/<lib>/index.cp3` declaration file the cp3 frontend type-checks native calls against directly — turning native-interop mismatches into ordinary cp3 type errors instead of opaque C++ template walls, and giving native calls real editor autocomplete for free, since the generated file is ordinary `.cp3` syntax. Full detail in §4. **This narrows, but does not remove, the error-diagnostics subsystem's job (§2)** — that subsystem still exists for cp3-frontend compiler bugs and opaque errors from cp3's own generated code (variant/coroutine internals), neither of which bindgen touches; only the native-interop slice of its original scope goes away. A real, documented limitation: a header's type signature alone can't say whether a returned pointer is owned or borrowed, so bindgen fully automates simple value types but may need a binding author to manually classify pointer-returning functions into the native pointer safety table (README.md §19).
- **Project tooling layout consolidated into one hidden `.cp3/` directory** (`.cp3/packages/`, `.cp3/native/`, `.cp3/build/`, `.cp3/lock.json`), replacing the earlier scattered `.packages/`/`.build/`/`cp3.lock` at the project root. Pure tooling/UX change, not a language-semantics one — cleaner root, a single `.gitignore` entry, faster IDE indexing since generated C++ and full native headers never need to be indexed at all.
- **`cp3 install` accepts a public GitHub repo URL directly**, checking the repo's latest non-prerelease release for `.cp3pak` assets: one match installs directly, multiple prompt an interactive selector, none produces a clear named error. Recorded in `.cp3/lock.json` with repo, tag, filename, and a SHA256 hash pinned at install time — since a GitHub release isn't a curated, integrity-guaranteed registry the way the normal `.cp3pak` install path is, pinning the hash lets a later install/CI run detect if a tag's asset silently changed. Scoped to GitHub specifically for beta one, not a generic git-URL feature.
- **`cp3 install <github-url>` refined with `--auto` compatibility selection**: when a release has multiple `.cp3pak` assets, `--auto` picks the one whose `package.json` `"engines.cp3"` range matches the installed compiler, falling back to the interactive selector rather than guessing if the result is still ambiguous or no candidate declares `"engines"` at all. `"engines"` added as a new optional `package.json` field (§5.1).
- **`excludeSource` added as an explicit, opt-in exception to the "a `.cp3pak` always carries its C++ source" default** (§5.4) — for authors who've already committed to `cacheBinary` covering their target platforms and don't want to ship dead-weight source nobody will compile. Requires at least one `cacheBinary` platform (a `cp3 build`-time validation error otherwise, since an empty list would make the package permanently uninstallable for everyone) and never excludes `index.cp3` (still needed for IDE autocomplete/type-checking regardless). The real, accepted cost — a platform outside the author's `cacheBinary` list has zero fallback for that package — is surfaced as a clear, specific install-time error, not a raw linker failure.
- **CLI command surface formalized**: `cp3 run` (build+execute in one step), `cp3 check` (frontend-only type-check, no C++ generation/invocation/linking — fast because the C++ toolchain step is what's actually slow, not cp3's own frontend), `cp3 dev` (incremental watch mode), `cp3 fmt` (zero-config formatter, `package.json`-overridable). `cp3 init` now generates standard `dev`/`build`/`start`/`check` `package.json` scripts by default. A standalone `cp3 doctor` is deprioritized — toolchain detection is already mandatory and automatic on `init`/`build`/`run` (§1), so a separate on-demand version of the same check isn't a beta-one requirement.
- **"Dual-runtime" dev-mode approach (a JIT/interpreter like Cling for fast iteration, compiled C++ for production) considered and explicitly rejected**, logged here so it isn't silently re-proposed later without this context: two different execution engines for the same program guarantees eventual semantic divergence — `import native` has no straightforward path through an interpreter, cp3's specific ARC + RAII + explicit-`weak` memory model (README.md §10/README.md §11) would need to be perfectly replicated by a second engine or bugs only manifest in production, and interpreted vs. optimized-compiled floating-point/UB behavior isn't guaranteed identical. The actual fix for `cp3 dev`'s speed goal needs none of that risk: surgical incremental recompilation (one `.cpp`/`.hpp` pair per `.cp3` file, §1, plus the persistent `.cp3/build/` cache) paired with a fast linker — same backend in dev and prod, zero divergence risk (§1).
- **Runtime crash reporting added as a distinct, much lighter subsystem from compile-time diagnostics** (§2): an embedded `.cp3`-to-`.cpp` line-number source map (the same source-map concept already required for compile-time mapping, §2) lets a runtime crash print a `.cp3` file/line instead of a raw address or generated-C++ location. A lookup-table read, not a translation system — doesn't require anywhere near the scope of the compile-time error mapper.
- **"Did you mean...?" typo suggestions added to diagnostics** (§2): Levenshtein-distance-style matching against in-scope names, appended to "does not exist" errors. Cheap, standard practice (TypeScript, Rust), logged as confirmed for beta one.
- **CLI spinner rule confirmed**: any command over ~0.5s gets a spinner/progress indicator; anything faster shows nothing, since a spinner on a near-instant command reads as a glitch rather than useful feedback (§1).
- **Pre-existing section-numbering bug fixed, unrelated to any new decision**: §5's subsections were mislabeled `21.1`–`21.6` (matching an old section-21 numbering that predates this doc's current section order), and two internal cross-references pointed at nonexistent `§25.3`/`§25.4`. After this document's own subsequent split into `README.md` and `CLI-README.md`, that section is now `§5` here, so those subsections are `5.1`–`5.6` and the fixed cross-references are `§5.3`/`§5.4` — worth a note here since a stale cross-reference is exactly the kind of ambiguity that costs an implementer real time chasing a section that doesn't say what it's pointed at.
- **Regex engine decided: PCRE2**, not `std::regex` (slow, missing some JS regex features) and not RE2 (faster and ReDoS-resistant, but no backreferences, so it can't give full JS parity). Adds roughly 150–300KB to a final binary. Added to the internal library wrapping table (§3) — same "wrap it invisibly" pattern as every other bundled library.
- **Qt clarified as failing on MOC specifically, not merely on the templates/operator-overloading gap** — even a bindgen that fully supported both would still fail on `Q_OBJECT`/`signals:`/`slots:`, since those aren't parseable C++ at all until Qt's own MOC tool generates code from them first, which happens outside anything `libclang`-based bindgen can see. **Confirmed direction: a hand-authored `.cp3pak` Qt bridge, post-beta-one, owned by the project, not a future bindgen capability.** Full reasoning and scope in the roadmap section above.

**Still genuinely open / not required for beta one** (implementation-detail engineering, not spec gaps):

- [ ] Compiler implementation architecture itself (transpiler internals, IR if any, pass structure) — this is the actual engineering work of beta one, not a design decision blocking it.

**Guiding principle:** define C+++ semantics independently first, and treat C++ as *only* the initial implementation backend — this keeps the language decoupled from C++'s complexity while still reusing its mature, battle-tested compiler infrastructure. For the full set of core language decisions (types, memory model, concurrency, classes, etc.), see `README.md`.
