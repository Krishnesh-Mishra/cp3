# 2. Error diagnostics & source mapping

Raw C++ compiler errors (template instantiation walls, `std::variant`/`std::vector` type-mismatch errors, etc.) are notoriously unreadable, especially to a developer who's never learned C++ — which is exactly the audience cp3 targets. Surfacing those raw messages would break the whole "2-day onboarding" goal the first time someone hits a real type error.

**cp3 intercepts every C++ compiler error, maps it back to the originating `.cp3` source location, and rewrites it into a plain, cp3-native message** — not a raw C++ diagnostic. This requires:

- A **source map** from each generated line in `.cp3/build/*.cpp` back to its originating `.cp3` file/line/column
- A **translation layer** that recognizes common C++ error shapes (type mismatches, missing overloads, `std::variant` access errors, etc.) and rewrites them in terms of cp3's own types and syntax, not C++'s

**Scope, and why the size of this subsystem isn't what it used to be:** an earlier draft of this doc estimated this subsystem at "tens of thousands of lines," back when it had to cover every possible mismatch at the native-interop boundary too — the realistic size of translating every C++ library's own error shapes into cp3-native ones. That's no longer the job. Native interop now has its own type-checking ([README.md §19](../language-spec/19-c-cpp-interoperability.md), via [CLI-README.md §4](04-package-manager.md)'s `cp3 native-install`) — a native-boundary mismatch is caught as an ordinary cp3 type error against the generated declaration file, with no C++ compilation step involved, so there's nothing opaque left to translate there in the first place. What this subsystem still has to cover, and the reason it can't be removed entirely: (1) a cp3-frontend bug that generates invalid C++ from otherwise-valid pure cp3 code — a real compiler bug, expected to happen during beta one, and (2) opaque errors surfacing from cp3's own generated code for ordinary language features (a `std::variant` access mismatch from a union type, a C++20 coroutine state-machine error) — neither of which has anything to do with native interop. That's a genuinely smaller, more bounded problem than the original estimate — a fixed, fairly short list of language-feature-generated error shapes to translate, plus a labeled generic fallback for anything outside that list — not "thin wrapper" territory, but not tens of thousands of lines either. Treat the original estimate as historical, not current.

## Reporting across multiple toolchain fallback attempts

Since a build can fall through several toolchains before failing entirely ([§1](01-compilation-pipeline.md)), the final error report groups by whether the underlying error is the **same** or **different** across the toolchains that hit it — never a raw stack trace, always the cp3-native translated form:

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

## "Did you mean...?" suggestions

Where a diagnostic references a name that doesn't exist (a missing property, an unresolved identifier), the compiler checks it against a Levenshtein-distance-style similarity to what actually is in scope, and appends a suggestion when there's a close match:

```
✗ src/main.cp3:2:5 — Property 'puh' does not exist on type 'number[]'.
  Did you mean 'push'?
```

Cheap to implement, and standard in TypeScript/Rust for exactly this reason — it turns a dead-end error into an obvious one-character fix.

## Runtime crash reporting — a separate, much lighter mechanism from compile-time diagnostics

Everything above covers *build-time* errors. A **runtime** crash (an out-of-bounds array access, an unhandled native exception, a segfault) is a genuinely different problem, and needs its own answer — without one, a runtime crash would print a raw address and a `.cp3/build/*.cpp` line number, which breaks the "never show raw C++" promise just as thoroughly as an unmapped compile-time error would.

The fix here is much smaller than the compile-time translation layer: `cp3 build` emits an ordinary line-number **source map** (the same concept as a JS source map — mapping each generated `.cpp` line back to its `.cp3` file/line/column, already required for compile-time mapping above) embedded alongside the binary. A small runtime hook catches crashes and looks up the current location in that map before printing:

```
✗ Runtime panic: Array index out of bounds (index 5, length 3)
  at src/main.cp3:14:8
```

This is a straightforward lookup-table read, not a translation system — it requires no pattern-matching against C++ error shapes, since a source map entry either exists for a given line or it doesn't. Distinct subsystem from the compile-time error mapper above; don't conflate the two or assume runtime crash reporting requires anywhere near the same scope of work.

