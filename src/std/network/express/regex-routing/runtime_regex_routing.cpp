// =============================================================================
// src/std/network/express/regex-routing/runtime_regex_routing.cpp
//
// Implementation of regex_routing.hpp. Depends only on the core PCRE2
// runtime (cp3/core/regex.hpp) and the existing network header; it never
// touches ServerImpl / HttpServer internals, so it links as its own TU
// (see README WIRING for the native-package row).
//
// Matching strategy: string patterns compile to an anchored PCRE2 source
// (`^...$`) that is executed through the core `Regex` engine, so custom
// `:name(re)` fills, compound segments and case folding reuse the same
// engine the language itself uses. RegExp paths also go through it (after
// stripping the stateful `g`/`y` flags). Only the metacharacters of the
// Express grammar are special: everything else in a string pattern is
// escaped literally.
// =============================================================================
#include "regex_routing.hpp"

#include <cctype>
#include <mutex>
#include <unordered_map>

namespace cp3::runtime::network::regex_routing {
namespace {

using ::cp3::runtime::regex::Regex;

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

/// Escapes the PCRE2 metacharacters so a literal string-pattern byte
/// compares byte-equal (and caselessly when the `i` flag is on).
void appendEscaped(std::string& out, char c) {
    static const std::string kMeta = ".+*?^${}()|[]\\";
    if (kMeta.find(c) != std::string::npos) out += '\\';
    out += c;
}

/// `[...]` scan helper shared by the named-group parser: is `i` inside a
/// character class (unescaped `[` starts one, `]` closes it)?
bool inCharClass(const std::string& s, std::size_t i) {
    bool inClass = false;
    for (std::size_t j = 0; j < i; ++j) {
        const char c = s[j];
        if (c == '\\') {
            ++j;
            continue;
        }
        if (c == '[') inClass = true;
        else if (c == ']') inClass = false;
    }
    return inClass;
}

/// One parsed segment of a string pattern.
struct Segment {
    bool optional = false;  // `:name?` / `*name?` (the `/` goes too)
    bool wildcard = false;  // `*` / `*name`
    std::string name;       // param name ("" for a pure literal segment)
    std::string customRe;   // `:name(re)` fill; "" = the default class
    std::string literal;    // fill when `wildcard` is false and `name` empty
    bool literalOnly = true;
};

/// Parses one `/`-separated segment (Express/path-to-regexp grammar):
///   `:name`, `:name?`, `:name(re)`, `:name(re)?`, `*`, `*name`, `*name?`
/// plus compounds such as `file-:name.:ext` (mixed literal + params).
/// A whole-segment `*` keeps the existing dispatcher meaning (one segment
/// mid-pattern, the remainder last); inside a compound it means `.*`
/// (path-to-regexp), because a single `[^/]*` cannot carry a dot.
Segment parseSegment(const std::string& segText) {
    Segment seg;
    if (segText == "*") {
        seg.wildcard = true;
        seg.name = "*";
        seg.literalOnly = false;
        return seg;
    }
    if (!segText.empty() && segText.front() == '*' && segText.size() > 1) {
        seg.wildcard = true;
        std::string name = segText.substr(1);
        if (!name.empty() && name.back() == '?') {
            seg.optional = true;
            name.pop_back();
        }
        seg.name = name.empty() ? "*" : name;
        seg.literalOnly = false;
        return seg;
    }
    // General scan: literals and `:name` / `:name(re)` params.
    std::string re;  // regex fragment (without capturing parens)
    std::size_t i = 0;
    bool hasParam = false;
    bool optional = false;
    std::string name;
    std::string customRe;
    while (i < segText.size()) {
        const char c = segText[i];
        if (c == ':' && i + 1 < segText.size() && isNameChar(segText[i + 1])) {
            hasParam = true;
            std::size_t j = i + 1;
            while (j < segText.size() && isNameChar(segText[j])) ++j;
            name = segText.substr(i + 1, j - (i + 1));
            if (j < segText.size() && segText[j] == '(') {
                // Balanced-paren scan (the fill may nest, e.g. `(\d{2,3})`).
                int depth = 0;
                std::size_t k = j;
                for (; k < segText.size(); ++k) {
                    if (segText[k] == '\\') {
                        ++k;
                        continue;
                    }
                    if (segText[k] == '(') ++depth;
                    else if (segText[k] == ')') {
                        if (--depth == 0) break;
                    }
                }
                customRe = segText.substr(j + 1, k - j - 1);
                j = (k < segText.size() ? k + 1 : segText.size());
            }
            if (j < segText.size() && segText[j] == '?') {
                optional = true;
                ++j;
            }
            // A sole `:name` segment keeps its shape for the caller; a
            // compound segment needs the fragment inlined here.
            if (segText.size() > 0 && (i != 0 || j != segText.size())) {
                re += "(" + (customRe.empty() ? std::string("[^/]+") : customRe) + ")";
                // Record compound param markers inline in `literal` with
                // \x01 separators so the caller can split names back out.
                re += "\x01" + name + "\x01";
                customRe.clear();
            } else if (segText == ":" + name ||
                       segText == ":" + name + "?" ||
                       (!customRe.empty() &&
                        segText == ":" + name + "(" + customRe + ")" +
                                   (optional ? "?" : ""))) {
                // Whole-segment param: handled by the caller below.
            }
            i = j;
            continue;
        }
        if (c == '*') {
            // `*` inside a compound segment: `.*` (matches slashes too,
            // like path-to-regexp); recorded as an anonymous wildcard.
            hasParam = true;
            re += "(.*)";
            re += "\x01*\x01";
            ++i;
            continue;
        }
        appendEscaped(re, c);
        ++i;
    }
    if (!hasParam) {
        seg.literal = segText;
        seg.literalOnly = true;
        return seg;
    }
    // Whole-segment `:name` / `*name` shapes are handled by name; compounds
    // carry their fragment in `literal` with \x01 name markers.
    const bool wholeParam =
        segText.front() == ':' || segText.front() == '*';
    if (wholeParam && re.find("\x01") == std::string::npos) {
        seg.name = name;
        seg.customRe = customRe;
        seg.optional = optional;
        seg.wildcard = false;
        seg.literalOnly = false;
        return seg;
    }
    seg.name = "";  // compound: names live in the \x01 markers
    seg.literal = re;
    seg.literalOnly = false;
    seg.optional = optional;
    return seg;
}

std::vector<std::string> splitSegments(const std::string& s) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = s.find('/', start);
        if (slash == std::string::npos) {
            parts.push_back(s.substr(start));
            return parts;
        }
        parts.push_back(s.substr(start, slash - start));
        start = slash + 1;
    }
}

/// Executes an anchored pattern through the core engine and maps group i
/// (1-based) onto `names[i-1]`, decoding each participating value once.
MatchResult runAnchored(const std::string& source, const std::string& flags,
                        const std::string& path,
                        const std::vector<std::string>& names,
                        bool requireFullMatch) {
    MatchResult out;
    const Regex regex = Regex::compile(source, flags);
    const auto values = regex.exec(path);
    if (!values || values->empty()) return out;  // no match (empty == null, the M1 shape)
    // The exec shape is [full, g1, ...]. An unanchored search still yields
    // the first match; full-match callers verify coverage below.
    if (requireFullMatch && (*values)[0] != path) return out;
    out.matched = true;
    for (std::size_t g = 1; g < values->size() && g - 1 < names.size(); ++g) {
        const std::string& name = names[g - 1];
        out.params.emplace_back(name, decodeParam((*values)[g]));
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// decodeParam
// ---------------------------------------------------------------------------
std::string decodeParam(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size() && isHexDigit(value[i + 1]) &&
            isHexDigit(value[i + 2])) {
            out += static_cast<char>(hexValue(value[i + 1]) * 16 + hexValue(value[i + 2]));
            i += 2;
            continue;
        }
        out += value[i];  // malformed escape and '+' stay literal
    }
    return out;
}

// ---------------------------------------------------------------------------
// regexGroupNames
// ---------------------------------------------------------------------------
std::vector<std::string> regexGroupNames(const std::string& source) {
    std::vector<std::string> names;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '\\') {
            ++i;
            continue;
        }
        if (c == '[') {
            // Member of a character class: skip to the closing ']'.
            std::size_t j = i + 1;
            if (j < source.size() && source[j] == '^') ++j;
            if (j < source.size() && source[j] == ']') ++j;
            for (; j < source.size(); ++j) {
                if (source[j] == '\\') {
                    ++j;
                    continue;
                }
                if (source[j] == ']') break;
            }
            i = j;
            continue;
        }
        if (c != '(' || inCharClass(source, i)) continue;
        if (i + 2 < source.size() && source[i + 1] == '?' && source[i + 2] == '<') {
            // `(?<name>...)` capture — but not `(?<=` / `(?<!` lookbehinds.
            const char after = i + 3 < source.size() ? source[i + 3] : '\0';
            if (after == '=' || after == '!') {
                names.emplace_back("");
                continue;
            }
            std::size_t j = i + 3;
            while (j < source.size() && isNameChar(source[j])) ++j;
            names.push_back(source.substr(i + 3, j - (i + 3)));
            i = j;
            continue;
        }
        if (i + 2 < source.size() && source[i + 1] == '?' && source[i + 2] == '\'') {
            std::size_t j = i + 3;
            while (j < source.size() && isNameChar(source[j])) ++j;
            names.push_back(source.substr(i + 3, j - (i + 3)));
            i = j;
            continue;
        }
        if (i + 3 < source.size() && source[i + 1] == '?' && source[i + 2] == 'P' &&
            source[i + 3] == '<') {
            std::size_t j = i + 4;
            while (j < source.size() && isNameChar(source[j])) ++j;
            names.push_back(source.substr(i + 4, j - (i + 4)));
            i = j;
            continue;
        }
        if (i + 1 < source.size() && source[i + 1] == '?') {
            continue;  // (?:...) (?=...) (?flags:...) — non-capturing
        }
        names.emplace_back("");  // plain capturing group
    }
    return names;
}

// ---------------------------------------------------------------------------
// compileStringPattern
// ---------------------------------------------------------------------------
CompiledStringPattern compileStringPattern(const std::string& pattern,
                                           const EffectiveOptions& eff) {
    CompiledStringPattern out;
    out.caseInsensitive = !eff.caseSensitive;
    if (pattern == "*" || pattern == "/*") {
        // Matches any path (no capture), exactly like the existing matcher.
        out.source = ".*";
        return out;
    }
    const std::vector<std::string> segs = splitSegments(pattern);
    std::string re;
    for (std::size_t s = 0; s < segs.size(); ++s) {
        const bool last = s + 1 == segs.size();
        const Segment seg = parseSegment(segs[s]);
        if (s > 0) re += "/";
        if (seg.literalOnly) {
            for (char c : seg.literal) appendEscaped(re, c);
            continue;
        }
        if (seg.wildcard) {
            // Last `*` swallows the remainder (slashes included); a
            // mid-pattern `*` swallows one segment (existing rules).
            re += last ? "(.*)" : "([^/]*)";
            out.paramNames.push_back(seg.name.empty() ? "*" : seg.name);
            continue;
        }
        if (seg.name.empty() && !seg.literal.empty() &&
            seg.literal.find("\x01") != std::string::npos) {
            // Compound segment: inline fragment + its \x01 name markers.
            std::string frag = seg.literal;
            std::size_t pos = 0;
            while (true) {
                const std::size_t m = frag.find("\x01", pos);
                if (m == std::string::npos) break;
                std::string name = frag.substr(m + 1, frag.find("\x01", m + 1) - (m + 1));
                re += frag.substr(pos, m - pos);
                out.paramNames.push_back(name);
                pos = frag.find("\x01", m + 1) + 1;
            }
            re += frag.substr(pos);
            continue;
        }
        // Whole-segment `:name` / `:name(re)`.
        const std::string fill = seg.customRe.empty() ? "[^/]+" : seg.customRe;
        if (seg.optional) {
            // The `/` before it is optional too, so fold the slash in:
            // the slash already added above must be conditional. Rebuild
            // this segment as `(?:/fill)?` (root-optional parameters).
            if (!re.empty() && re.back() == '/') re.pop_back();
            re += "(?:/(" + fill + "))?";
            out.paramNames.push_back(seg.name);
        } else {
            re += "(" + fill + ")";
            out.paramNames.push_back(seg.name);
        }
    }
    if (!eff.strict && re.size() > 1) {
        // Non-strict: one trailing slash is optional (Express `strict:
        // false`). The `$` anchor is added by the caller, so `/?` here
        // makes `/a/b` match both `/a/b` and `/a/b/`.
        re += "/?";
    }
    out.source = re;
    return out;
}

// ---------------------------------------------------------------------------
// Matchers
// ---------------------------------------------------------------------------
MatchResult matchStringPattern(const std::string& pattern, const std::string& path,
                               const EffectiveOptions& eff) {
    const CompiledStringPattern compiled = compileStringPattern(pattern, eff);
    const std::string flags = compiled.caseInsensitive ? "i" : "";
    // Anchors live in the source; `runAnchored` is only for the integer
    // capture mapping, so hand it the anchored source.
    const std::string anchored = "^(?:" + compiled.source + ")$";
    return runAnchored(anchored, flags, path, compiled.paramNames, true);
}

MatchResult matchRegexPath(const Regex& regex, const std::string& path, bool anchored) {
    // A fresh compile without `g`/`y` (stateless across requests), keeping
    // the value's own flags otherwise (`i` is the caseSensitive spelling).
    std::string flags;
    for (char f : regex.flags()) {
        if (f == 'g' || f == 'y') continue;
        flags += f;
    }
    const std::string source = anchored ? "^(?:" + regex.source() + ")$" : regex.source();
    const std::vector<std::string> groupNames = regexGroupNames(regex.source());
    MatchResult out;
    const Regex fresh = Regex::compile(source, flags);
    const auto values = fresh.exec(path);
    if (!values || values->empty()) return out;
    if (anchored && (*values)[0] != path) return out;
    out.matched = true;
    for (std::size_t g = 1; g < values->size(); ++g) {
        std::string name = (g - 1 < groupNames.size() && !groupNames[g - 1].empty())
                               ? groupNames[g - 1]
                               : std::to_string(g - 1);
        out.params.emplace_back(name, decodeParam((*values)[g]));
    }
    return out;
}

MatchResult matchArrayPath(const std::vector<PathSpec>& elements,
                           const std::string& path, const EffectiveOptions& eff) {
    const std::vector<PathSpec> flat = flattenPathArray(elements);
    MatchResult out;
    for (std::size_t i = 0; i < flat.size(); ++i) {
        const PathSpec& el = flat[i];
        MatchResult hit;
        if (el.kind == PathSpec::Kind::String) {
            hit = matchStringPattern(el.pattern, path, eff);
        } else if (el.kind == PathSpec::Kind::RegExp) {
            hit = matchRegexPath(el.regex, path, false);
        } else {
            hit = matchArrayPath(el.elements, path, eff);  // defensive
        }
        if (hit.matched) {
            hit.index = i;
            return hit;
        }
    }
    return out;
}

MatchResult matchPath(const PathSpec& spec, const std::string& path,
                      const EffectiveOptions& eff, bool anchoredRegex) {
    switch (spec.kind) {
        case PathSpec::Kind::String:
            return matchStringPattern(spec.pattern, path, eff);
        case PathSpec::Kind::RegExp:
            return matchRegexPath(spec.regex, path, anchoredRegex);
        case PathSpec::Kind::Array:
            return matchArrayPath(spec.elements, path, eff);
    }
    return {};
}

std::vector<PathSpec> flattenPathArray(const std::vector<PathSpec>& input) {
    std::vector<PathSpec> out;
    for (const PathSpec& el : input) {
        if (el.kind == PathSpec::Kind::Array) {
            const std::vector<PathSpec> inner = flattenPathArray(el.elements);
            out.insert(out.end(), inner.begin(), inner.end());
        } else {
            out.push_back(el);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// App settings registry
// ---------------------------------------------------------------------------
namespace {

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<const HttpServer*, RoutingSettings>& registry() {
    static std::unordered_map<const HttpServer*, RoutingSettings> map;
    return map;
}

}  // namespace

bool isRoutingSettingKey(const std::string& key) {
    return key == "case sensitive routing" || key == "strict routing";
}

bool setAppSetting(const HttpServer* app, const std::string& key, bool value) {
    if (app == nullptr) return false;
    std::lock_guard<std::mutex> lock(registryMutex());
    RoutingSettings& current = registry()[app];
    if (key == "case sensitive routing") {
        current.caseSensitive = value;
        return true;
    }
    if (key == "strict routing") {
        current.strict = value;
        return true;
    }
    return false;
}

void enableAppSetting(const HttpServer* app, const std::string& key) {
    setAppSetting(app, key, true);
}

void disableAppSetting(const HttpServer* app, const std::string& key) {
    setAppSetting(app, key, false);
}

bool getAppSetting(const HttpServer* app, const std::string& key) {
    const RoutingSettings current = appSettingsFor(app);
    if (key == "case sensitive routing") return current.caseSensitive;
    if (key == "strict routing") return current.strict;
    return false;
}

RoutingSettings appSettingsFor(const HttpServer* app) {
    if (app == nullptr) return {};
    std::lock_guard<std::mutex> lock(registryMutex());
    const auto it = registry().find(app);
    return it == registry().end() ? RoutingSettings{} : it->second;
}

}  // namespace cp3::runtime::network::regex_routing
