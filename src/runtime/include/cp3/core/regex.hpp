// =============================================================================
// cp3/core/regex.hpp - native regex literals (core language, no import).
//
// OWNERSHIP: runtime module. This header is part of the generated-code ABI.
//
// DECISIONS D48: `/pattern/flags` is core language; the documented surface is
// `.test(s)`, `.match(s)`, `.exec(s)` plus the `.source` / `.flags` reads, and
// the string-side `s.split(r)` lowering (`Regex::split`).
// The engine is PCRE2 (docs/cli-tooling/03 - chosen for JS feature parity:
// lookarounds, backreferences; not std::regex, not RE2), vendored as a pinned
// static archive (third_party/pcre2, MANIFEST "Verified consumer flags"). The
// engine itself stays behind a pimpl so generated code (and this header) never
// includes pcre2.h; only src/runtime/src/core/regex.cpp does.
//
// -----------------------------------------------------------------------------
// JS-PARITY CONTRACT (the shape the corpus pins; deviations are deliberate and
// recorded in the wave report)
// -----------------------------------------------------------------------------
//   * `.test(s)` -> boolean; `.match(s)` / `.exec(s)` -> `string[]`.
//   * A no-match result is the EMPTY array, not JS `null`: M1 cannot spell
//     `string[] | null` (classifyNullishUnion covers primitives only), and the
//     brief prefers a pinned honest shape over an unpinnable union.
//   * Single-match results are [full, group1, ...]; a group that did not
//     participate is `""` (JS uses `undefined`; `undefined` is not an element
//     of `string[]`).
//   * With `g`, `.match(s)` returns all whole matches (no groups), like JS
//     String.prototype.match; without `g` it returns one [full, ...groups].
//   * `g` / `y` regexes carry `lastIndex` state exactly like JS RegExp objects:
//     `.test`/`.exec` advance it on a match and reset it on a failure; a fresh
//     literal evaluates to a fresh object (shared on assignment, like JS).
//     The value is NOT exposed as a `.lastIndex` property in M1 (reading and
//     writing it are not part of the documented D48 surface).
//   * Offsets are UTF-8 BYTE offsets, not UTF-16 code units (cp3 strings are
//     byte strings, D63). ASCII behavior is identical to JS; non-ASCII `.` /
//     offset behavior is a documented deviation, unpinned by the corpus.
//   * Flags: `i` -> caseless, `m` -> multiline, `s` -> dotall, `u`/`v` -> UTF
//     (v treated as u), `g` -> stateful iteration, `y` -> anchored at
//     lastIndex, `d` -> accepted and ignored (no indices are exposed here).
//     `.flags` renders the canonical JS order `dgimsuvy`.
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include "cp3/core/value.hpp"  // Array<E> (the string[] result spelling)

namespace cp3::runtime::regex {

// The compiled-pattern handle. Defined in regex.cpp; PCRE2 types never cross
// this header.
struct Engine;

// A cp3 regex value. Copyable and const-functor-like: copies share one engine
// (JS reference semantics), and the `g`/`y` match state lives in the engine,
// so a `const` binding can still advance its lastIndex through the handle
// (exactly like a `const Ref<T>` in the rest of the runtime ABI).
class Regex {
public:
    Regex() = default;
    Regex(const Regex&) = default;
    Regex& operator=(const Regex&) = default;
    Regex(Regex&&) noexcept = default;
    Regex& operator=(Regex&&) noexcept = default;

    // The lowering of a `/pattern/flags` literal. A pattern PCRE2 rejects
    // terminates the program with a clear message (JS raises SyntaxError when
    // the literal is evaluated; M1 has no CompileError value to throw).
    static Regex compile(const std::string& pattern, const std::string& flags);

    // The documented D48 instance surface.
    bool test(const std::string& subject) const;
    Array<std::string> match(const std::string& subject) const;
    Array<std::string> exec(const std::string& subject) const;

    // `s.split(r)`, the string-side lowering of a regex separator (the
    // day-2 docs example and the stdlib/string_split_regex corpus case): the
    // substrings between matches, with every participating capture group
    // inserted (a group that did not participate is "", the M1 spelling of
    // undefined). A subject with no match yields [subject]; an empty match
    // advances one byte; the g/y state is neither read nor written.
    Array<std::string> split(const std::string& subject) const;

    // `.source` / `.flags`: the literal text as written and its canonical
    // flag order. Both are `""` on a default-constructed value.
    std::string source() const;
    std::string flags() const;

private:
    std::shared_ptr<Engine> engine_;
};

// Printing: `console.log(r)` / `${r}` reach the generated helper's `cp3_ts`
// overload set through ADL; JS renders a regex value as `/pattern/flags`.
inline std::string cp3_ts(const Regex& r) {
    return "/" + r.source() + "/" + r.flags();
}

// Truthiness: every regex value is an object, and objects are truthy in JS.
inline bool cp3_truthy(const Regex&) { return true; }

}  // namespace cp3::runtime::regex
