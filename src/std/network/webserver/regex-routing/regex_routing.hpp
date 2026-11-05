// =============================================================================
// src/std/network/webserver/regex-routing/regex_routing.hpp
//
// webserver-style extended routing for `std/network`: RegExp paths, arrays of
// paths, `*` wildcard segments, the `caseSensitive` / `strict` route
// options, the matching app settings (`case sensitive routing` /
// `strict routing`), and percent-decoded `req.params`.
//
// OWNERSHIP: src/std/network/webserver/regex-routing/** ONLY. This header is
// self-contained (it only needs cp3/std/network.hpp + cp3/core/regex.hpp)
// and it does NOT modify the existing HttpServer surface: the integrator
// wires it in with the exact lines in README.md (WIRING). No shared file is
// touched by this module.
//
// SUPPORTED SOURCE FORMS (all lower onto the three PathSpec kinds below):
//   app.get("/users/:id", handler)                     // existing string path
//   app.get(/\/commits\/(\d+)/, handler)               // RegExp path
//   app.get(["/a", "/b"], handler)                     // array of paths
//   app.get(["/a", /\/b\/(\d+)/], handler)             // mixed array
//   app.get("/files/*", handler)                       // wildcard segment
//   app.get(path, { caseSensitive: true, strict: true }, handler)
//   app.set("case sensitive routing", true); app.enable("strict routing");
//   app.get("case sensitive routing")                   // settings read
//
// SEMANTICS (webserver 4 parity unless noted):
//   * String patterns keep the existing pathMatches rules (literal /
//     `:name` one segment / whole-segment `*`), extended with:
//       - `:name?`      optional segment (the preceding `/` goes too);
//       - `:name(re)`   custom regex for the captured segment fill;
//       - `*name`       named wildcard (whole segment, `:name` shape);
//       - compound segments (`/file-:name.:ext`) are matched char by char.
//   * Non-strict (the default): one trailing `/` is optional on the request
//     path, except for the root `/`. Strict: the trailing slash must match
//     the pattern exactly.
//   * Case-insensitive (the default): literal bytes compare caselessly
//     (compiled with the `i` flag); captured values keep their original
//     case. Case-sensitive compares literally.
//   * RegExp paths use `test` (search) semantics, like webserver: the regex
//     matches if it matches anywhere in the path. `matchRegexPath` with
//     `anchored=true` wraps it in `^(?:...)$` for full-path matching.
//     webserver reads no options for RegExp paths; case comes from the `i`
//     flag and `strict` is ignored (see LIMITATIONS in README.md).
//   * Captures: `:name` / `*` / `*name` names as before ("*" for anonymous
//     wildcards); RegExp unnamed groups become "0", "1", ... (group 1 is
//     "0", exactly like webserver `req.params[0]`); named groups `(?<name>)`
//     land under their name. Every captured value is percent-decoded once.
//   * App settings are snapshotted when the route is registered (webserver
//     compiles the path at registration): set them before registering
//     routes. A per-route option overrides the app setting for that route.
//   * Array paths try each element in order; the first match wins and its
//     zero-based position is reported as `MatchResult::index`.
// =============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/core/regex.hpp"
#include "cp3/std/network.hpp"

namespace cp3::runtime::network::regex_routing {

// Captured values in order: (name, decoded value). A `Map<string, string>`
// is built from this list by the dispatcher (one fresh Map per match, the
// existing rule), so repeated names keep the last write, like today.
using Params = std::vector<std::pair<std::string, std::string>>;

struct MatchResult {
    bool matched = false;
    Params params;
    std::size_t index = 0;  // array element that matched (0 for non-arrays)
};

// ---------------------------------------------------------------------------
// Options / app settings
// ---------------------------------------------------------------------------

/// Per-route options (`{ caseSensitive?: boolean, strict?: boolean }`).
/// Empty means "inherit the app setting"; an explicit `false` overrides a
/// `true` app setting (webserver `options` win over app settings).
struct RoutingOptions {
    std::optional<bool> caseSensitive;
    std::optional<bool> strict;
    bool hasAny() const { return caseSensitive.has_value() || strict.has_value(); }
};

/// The two app settings this module implements, with webserver defaults.
struct RoutingSettings {
    bool caseSensitive = false;  // "case sensitive routing" (default off)
    bool strict = false;         // "strict routing" (default off)
};

/// Effective options after merging a route option over the app settings.
struct EffectiveOptions {
    bool caseSensitive = false;
    bool strict = false;
};

inline EffectiveOptions resolveEffective(
    const RoutingSettings& app, const RoutingOptions& route = {}) {
    EffectiveOptions eff;
    eff.caseSensitive = route.caseSensitive.value_or(app.caseSensitive);
    eff.strict = route.strict.value_or(app.strict);
    return eff;
}

// ---------------------------------------------------------------------------
// Percent decoding
// ---------------------------------------------------------------------------

/// Decodes one captured path value, webserver `decodeURIComponent` style:
///   * `%XX` hex byte pairs decode (both `%2f` and `%2F`);
///   * `+` stays `+` (that is query-string, not path, decoding);
///   * a malformed `%` sequence (`%`, `%zz`, `%1`) stays literal, like
///     webserver's fallback to the raw value when decoding throws.
/// Applied once to every captured param value by the matchers below.
std::string decodeParam(const std::string& value);
inline std::string decodeURIComponent(const std::string& value) {
    return decodeParam(value);
}

// ---------------------------------------------------------------------------
// Path specs
// ---------------------------------------------------------------------------

/// One route path argument: a string pattern, a RegExp, or an array of
/// those. `PathSpec::makeRegex` takes the compiled core `Regex` value, so
/// the codegen just forwards the lowered `/pattern/flags` literal.
struct PathSpec {
    enum class Kind { String, RegExp, Array };

    Kind kind = Kind::String;
    std::string pattern;                       // String
    ::cp3::runtime::regex::Regex regex;        // RegExp
    std::vector<PathSpec> elements;            // Array (leaves: String|RegExp)

    static PathSpec makeString(std::string pattern) {
        PathSpec s;
        s.kind = Kind::String;
        s.pattern = std::move(pattern);
        return s;
    }
    static PathSpec makeRegex(::cp3::runtime::regex::Regex regex) {
        PathSpec s;
        s.kind = Kind::RegExp;
        s.regex = std::move(regex);
        return s;
    }
    static PathSpec makeArray(std::vector<PathSpec> elements) {
        PathSpec s;
        s.kind = Kind::Array;
        s.elements = std::move(elements);
        return s;
    }
};

/// Flattens nested arrays by one level (webserver accepts nested arrays at
/// runtime when built dynamically); leaf order and indices are preserved.
std::vector<PathSpec> flattenPathArray(const std::vector<PathSpec>& input);

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

/// Matches one string pattern against a request path, honouring `eff`.
/// This is the strict/case-aware replacement for the segment matcher: see
/// the file header for the pattern grammar.
MatchResult matchStringPattern(const std::string& pattern, const std::string& path,
                               const EffectiveOptions& eff);

/// Matches a RegExp path. `anchored=false` is webserver `test` (search)
/// semantics; `anchored=true` requires the whole path to match. `g`/`y`
/// flags are stripped before compiling (a shared stateful regex would walk
/// `lastIndex` across requests); `i`/`m`/`s`/`u`/`v`/`d` pass through.
MatchResult matchRegexPath(const ::cp3::runtime::regex::Regex& regex,
                           const std::string& path, bool anchored = false);

/// Tries each array element (strings and regexes) in order, first hit wins.
MatchResult matchArrayPath(const std::vector<PathSpec>& elements,
                           const std::string& path, const EffectiveOptions& eff);

/// Unified entry: dispatches on `spec.kind`. For RegExp specs `eff` is
/// ignored (webserver parity); callers keep the app-settings snapshot only
/// for string/array-of-strings specs.
MatchResult matchPath(const PathSpec& spec, const std::string& path,
                      const EffectiveOptions& eff, bool anchoredRegex = false);

// ---------------------------------------------------------------------------
// Group-name extraction + string-pattern compilation (exposed for tests
// and for the dispatcher replacement; the matchers above use them too)
// ---------------------------------------------------------------------------

/// Names of the named capture groups of a regex source, in group order
/// ("" for unnamed groups). Recognises `(?<name>`, `(?'name'` and
/// `(?P<name>`; respects `\` escapes and `[...]` character classes.
std::vector<std::string> regexGroupNames(const std::string& source);

/// The anchored PCRE2 source a string pattern compiles to, and the names
/// of its capturing groups in group order ("*" for anonymous wildcards).
struct CompiledStringPattern {
    std::string source;  // without the ^...$ anchors
    std::vector<std::string> paramNames;
    bool caseInsensitive = false;
};
CompiledStringPattern compileStringPattern(const std::string& pattern,
                                           const EffectiveOptions& eff);

// ---------------------------------------------------------------------------
// App settings registry (per HttpServer, read at route registration time)
// ---------------------------------------------------------------------------

/// True for the two keys this module owns. Unknown keys return false so
/// the integrator can keep mirroring everything else.
bool isRoutingSettingKey(const std::string& key);

/// `app.set(key, value)`. Unknown keys are ignored (returns false).
bool setAppSetting(const HttpServer* app, const std::string& key, bool value);
/// `app.enable(key)` == set true / `app.disable(key)` == set false.
void enableAppSetting(const HttpServer* app, const std::string& key);
void disableAppSetting(const HttpServer* app, const std::string& key);
/// `app.get(key)` / `app.enabled(key)` / `app.disabled(key)`. Unknown keys
/// throw the usual "unknown setting" fatal? No: they return false and the
/// codegen only routes this module's keys here (see README WIRING).
bool getAppSetting(const HttpServer* app, const std::string& key);
/// The snapshot consulted at `addRoute` time for one app (defaults when
/// the app never set a key).
RoutingSettings appSettingsFor(const HttpServer* app);

}  // namespace cp3::runtime::network::regex_routing
