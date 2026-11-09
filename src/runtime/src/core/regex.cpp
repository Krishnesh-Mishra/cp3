// =============================================================================
// src/runtime/src/core/regex.cpp - native regex values (PCRE2-backed).
//
// OWNERSHIP: runtime module. Implements cp3/core/regex.hpp. This is the only
// file in the tree that includes pcre2.h; the vendored build it is compiled
// against is pinned in vendor/third_party/MANIFEST.md ("Verified consumer flags"):
// PCRE2 10.49, static 8-bit library. The PCRE2_STATIC and
// PCRE2_CODE_UNIT_WIDTH=8 the header needs come from src/runtime/CMakeLists.txt.
//
// JS parity: see the contract block in cp3/core/regex.hpp. The g/y match state
// (lastIndex) lives in the shared engine, so copies of one Regex value observe
// each other's state exactly like copies of a JS RegExp object.
// =============================================================================
#include "cp3/core/regex.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Do not include pcre2.h from any other TU: these definitions live in CMake.
#include <pcre2.h>

#include "cp3/core/error.hpp"  // kFailureExitCode

namespace cp3::runtime::regex {

namespace {

// The canonical `.flags` rendering: JS order, one letter per flag the literal
// carried (duplicates are impossible: the lexer already rejected them).
std::string canonicalFlags(const std::string& flags) {
    std::string out;
    for (const char letter : {'d', 'g', 'i', 'm', 's', 'u', 'v', 'y'}) {
        if (flags.find(letter) != std::string::npos) out.push_back(letter);
    }
    return out;
}

}  // namespace

// The compiled pattern plus the g/y state. Defined here so PCRE2 types never
// reach cp3/core/regex.hpp (the engine stays behind the pimpl).
struct Engine {
    pcre2_code* code = nullptr;
    std::uint32_t captures = 0;  // number of capture groups (excluding $0)
    bool global = false;         // `g`: stateful left-to-right iteration
    bool sticky = false;         // `y`: every attempt anchored at lastIndex
    std::size_t lastIndex = 0;   // byte offset, like the JS property
    std::string pattern;
    std::string flags;

    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    ~Engine() {
        if (code != nullptr) pcre2_code_free(code);
    }
};

namespace {

// One match attempt's outcome: the groups ([$0, $1, ...]) and the byte offsets
// of the whole match.
struct MatchResult {
    bool matched = false;
    std::vector<std::string> values;
    std::size_t begin = 0;
    std::size_t end = 0;
};

// One match attempt at an explicit byte offset, with no g/y state involved
// (`Regex::split` scans through this).
MatchResult attemptFrom(Engine& engine, const std::string& subject,
                        std::size_t start) {
    MatchResult result;
    if (start > subject.size()) return result;
    pcre2_match_data* data =
        pcre2_match_data_create_from_pattern(engine.code, nullptr);
    const int rc = pcre2_match(
        engine.code, reinterpret_cast<PCRE2_SPTR>(subject.data()),
        static_cast<PCRE2_SIZE>(subject.size()), static_cast<PCRE2_SIZE>(start),
        0, data, nullptr);
    if (rc < 0) {
        // PCRE2_ERROR_NOMATCH (or a bad subject offset): JS "no match".
        pcre2_match_data_free(data);
        return result;
    }
    // match_data_create_from_pattern sizes the ovector for every capture group,
    // so ovector[2*g .. 2*g+1] is PCRE2_UNSET for a group that did not
    // participate (JS `undefined`; the empty string is the cp3 spelling).
    const PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(data);
    result.matched = true;
    result.values.reserve(engine.captures + 1);
    for (std::uint32_t group = 0; group <= engine.captures; ++group) {
        const PCRE2_SIZE begin = ovector[2 * group];
        const PCRE2_SIZE finish = ovector[2 * group + 1];
        if (begin == PCRE2_UNSET || finish == PCRE2_UNSET) {
            result.values.push_back(std::string());
        } else {
            result.values.push_back(subject.substr(begin, finish - begin));
        }
    }
    result.begin = static_cast<std::size_t>(ovector[0]);
    result.end = static_cast<std::size_t>(ovector[1]);
    pcre2_match_data_free(data);
    return result;
}

// One match attempt through the public g/y state. Honors it exactly like
// `RegExp.prototype.exec`: a stateful regex starts at `lastIndex`, advances it
// to the end of the match on success, and resets it to 0 on failure. A plain
// regex always starts at 0 and leaves the state untouched.
//
// The engine is mutable state shared through a `shared_ptr`: a `const Regex`
// still advances it, exactly like `const Ref<T>` elsewhere in the runtime ABI.
MatchResult attempt(Engine& engine, const std::string& subject) {
    const bool stateful = engine.global || engine.sticky;
    const std::size_t start = stateful ? engine.lastIndex : 0;
    const MatchResult result = attemptFrom(engine, subject, start);
    if (stateful) engine.lastIndex = result.matched ? result.end : 0;
    return result;
}

// The group count PCRE2 compiled for `code`.
std::uint32_t captureCountOf(const pcre2_code* code) {
    std::uint32_t count = 0;
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &count);
    return count;
}

}  // namespace

Regex Regex::compile(const std::string& pattern, const std::string& flags) {
    auto engine = std::make_shared<Engine>();
    engine->pattern = pattern;
    engine->global = flags.find('g') != std::string::npos;
    engine->sticky = flags.find('y') != std::string::npos;

    std::uint32_t options = 0;
    if (flags.find('i') != std::string::npos) options |= PCRE2_CASELESS;
    if (flags.find('m') != std::string::npos) options |= PCRE2_MULTILINE;
    if (flags.find('s') != std::string::npos) options |= PCRE2_DOTALL;
    // `u`/`v`: UTF-8 mode only, WITHOUT PCRE2_UCP, so `\w`/`\d` stay ASCII
    // like JS (PCRE2's UCP classes would match more than the spec does).
    if (flags.find('u') != std::string::npos ||
        flags.find('v') != std::string::npos) {
        options |= PCRE2_UTF;
    }
    // `y`: every attempt is anchored at its start offset (JS sticky).
    if (engine->sticky) options |= PCRE2_ANCHORED;

    int errorCode = 0;
    PCRE2_SIZE errorOffset = 0;
    engine->code = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern.data()),
        static_cast<PCRE2_SIZE>(pattern.size()), options, &errorCode,
        &errorOffset, nullptr);
    if (engine->code == nullptr) {
        // JS throws a SyntaxError when the literal is evaluated; M1 has no
        // Error value to throw, so this is a fatal runtime error (the
        // documented shape of a runtime failure, cp3/core/error.hpp).
        PCRE2_UCHAR message[256];
        pcre2_get_error_message(errorCode, message, sizeof(message));
        std::fprintf(stderr,
                     "cp3: error: invalid regular expression /%s/%s: %s\n",
                     pattern.c_str(), flags.c_str(),
                     reinterpret_cast<const char*>(message));
        std::fflush(stderr);
        std::exit(kFailureExitCode);
    }
    engine->captures = captureCountOf(engine->code);
    engine->flags = canonicalFlags(flags);

    Regex value;
    value.engine_ = std::move(engine);
    return value;
}

bool Regex::test(const std::string& subject) const {
    if (engine_ == nullptr) return false;
    return attempt(*engine_, subject).matched;
}

Array<std::string> Regex::exec(const std::string& subject) const {
    if (engine_ == nullptr) return makeArray<std::string>();
    return makeArray<std::string>(attempt(*engine_, subject).values);
}

Array<std::string> Regex::match(const std::string& subject) const {
    std::vector<std::string> out;
    if (engine_ == nullptr) return makeArray<std::string>(std::move(out));
    if (!engine_->global) {
        // One match, exactly the exec shape: groups included, and a sticky
        // regex still reads/updates lastIndex (String.prototype.match on a
        // non-global regex is one RegExpExec).
        return makeArray<std::string>(attempt(*engine_, subject).values);
    }
    // Global: every whole match (groups excluded), iterated from 0 like
    // String.prototype.match, and lastIndex is left at 0 afterwards. An empty
    // match advances one byte (JS's AdvanceStringIndex), so always progress.
    engine_->lastIndex = 0;
    while (engine_->lastIndex <= subject.size()) {
        const std::size_t start = engine_->lastIndex;
        MatchResult found = attempt(*engine_, subject);
        if (!found.matched) break;
        out.push_back(found.values.front());
        if (found.end > start) continue;
        if (start >= subject.size()) break;
        engine_->lastIndex = start + 1;
    }
    engine_->lastIndex = 0;
    return makeArray<std::string>(std::move(out));
}

// `s.split(r)`: the spec's separator algorithm, run as a fresh left-to-right
// scan (g/y state untouched). `piece` is the start of the substring not yet
// emitted; an empty match at that exact byte advances one byte with nothing
// emitted (the spec's AdvanceStringIndex), which is what makes "ab".split(/(?:)/)
// ["a", "b"] and not ["", "a", "b"].
Array<std::string> Regex::split(const std::string& subject) const {
    std::vector<std::string> out;
    if (engine_ == nullptr) {
        out.push_back(subject);
        return makeArray<std::string>(std::move(out));
    }
    // The spec's empty-subject early return: "" splits to [] when the pattern
    // matches at 0, and to [""] when it does not.
    if (subject.empty()) {
        if (!attemptFrom(*engine_, subject, 0).matched) {
            out.push_back(std::string());
        }
        return makeArray<std::string>(std::move(out));
    }
    std::size_t piece = 0;
    std::size_t pos = 0;
    while (pos < subject.size()) {
        const MatchResult found = attemptFrom(*engine_, subject, pos);
        if (!found.matched) {
            // An unanchored search exhausts every later position, so a
            // non-sticky pattern is done; a sticky one (PCRE2_ANCHORED) still
            // has to try the next byte, exactly like the spec's per-position
            // sticky clone.
            if (engine_->sticky) {
                ++pos;
                continue;
            }
            break;
        }
        if (found.end == piece) {
            // The spec's AdvanceStringIndex: a match that would emit an empty
            // piece at the pending offset advances instead (this is what keeps
            // "ab".split(/(?:)/) ["a", "b"]).
            ++pos;
            continue;
        }
        out.push_back(subject.substr(piece, found.begin - piece));
        // JS inserts every capture group into the result; a group that did not
        // participate is "" here (the M1 spelling of undefined).
        for (std::size_t group = 1; group < found.values.size(); ++group) {
            out.push_back(found.values[group]);
        }
        piece = found.end;
        pos = found.end;
    }
    out.push_back(subject.substr(piece));
    return makeArray<std::string>(std::move(out));
}

// The `replace`/`replaceAll(r)` scan, mirroring `split`'s left-to-right
// walk: an empty match advances one byte (never re-replaces), the g/y state
// is untouched, and the replacement text is spliced in literally. A fresh
// scan from 0 matches the `split()` precedent; `stopAfterFirst` implements
// `replace`'s one-shot behavior for non-global regexes; `requireGlobal` is
// `replaceAll`'s JS `TypeError` for non-global regexes (cp3 fatally reports
// it, like matchAll).
std::string replaceScan(const std::string& subject,
                        Engine& engine,
                        const std::string& replacement,
                        bool stopAfterFirst,
                        bool requireGlobal) {
    if (engine.code == nullptr) return subject;
    if (requireGlobal && !engine.global) {
        std::fputs("cp3c: String.prototype.replaceAll requires a global "
                   "regular expression\n", stderr);
        std::fflush(stderr);
        std::exit(1);
    }
    const bool single = stopAfterFirst && !engine.global;
    std::string out;
    std::size_t piece = 0;
    for (std::size_t pos = 0; pos <= subject.size();) {
        const MatchResult found = attemptFrom(engine, subject, pos);
        if (!found.matched) {
            if (engine.sticky) {
                ++pos;
                continue;
            }
            break;
        }
        out.append(subject, piece, found.begin - piece);
        out.append(replacement);
        piece = found.end;
        if (single) break;
        // An empty match must advance one byte so the same zero-width match
        // cannot be replaced forever (JS's AdvanceStringIndex).
        pos = found.end == pos ? pos + 1 : found.end;
    }
    out.append(subject, piece, std::string::npos);
    return out;
}

std::string Regex::replace(const std::string& subject,
                           const std::string& replacement) const {
    if (engine_ == nullptr) return subject;
    return replaceScan(subject, *engine_, replacement, true, false);
}

std::string Regex::replaceAll(const std::string& subject,
                              const std::string& replacement) const {
    if (engine_ == nullptr) return subject;
    return replaceScan(subject, *engine_, replacement, false, true);
}

std::string Regex::source() const {
    return engine_ == nullptr ? std::string() : engine_->pattern;
}

std::string Regex::flags() const {
    return engine_ == nullptr ? std::string() : engine_->flags;
}

}  // namespace cp3::runtime::regex
