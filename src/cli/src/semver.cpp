// =============================================================================
// cli/semver.cpp - semver parsing + npm-style range matching DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/semver.hpp.
//
// Grammar covered: full/partial versions, comparators (=, >, >=, <, <=),
// caret and tilde ranges, wildcard components, whitespace intersections,
// `||` unions and hyphen ranges. Anything outside that grammar fails open
// (reports a match) so the engines check only ever warns when it is certain.
// =============================================================================
#include "private/semver.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace cp3::cli {

namespace {

bool isAllDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

bool parseLong(std::string_view s, long& out) {
    if (!isAllDigits(s)) return false;
    long value = 0;
    for (char c : s) {
        value = value * 10 + (c - '0');
        if (value > 1000000000L) return false;
    }
    out = value;
    return true;
}

// Splits on a single-character delimiter.
std::vector<std::string_view> splitView(std::string_view s, char delim) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t at = s.find(delim, start);
        if (at == std::string_view::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, at - start));
        start = at + 1;
    }
    return parts;
}

bool validPrerelease(std::string_view pre) {
    if (pre.empty()) return false;
    for (std::string_view id : splitView(pre, '.')) {
        if (id.empty()) return false;
        for (char c : id) {
            const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                            c == '-';
            if (!ok) return false;
        }
    }
    return true;
}

// Compares two dot-separated prerelease identifier lists.
int comparePrerelease(std::string_view a, std::string_view b) {
    const std::vector<std::string_view> pa = splitView(a, '.');
    const std::vector<std::string_view> pb = splitView(b, '.');
    const std::size_t count = pa.size() < pb.size() ? pa.size() : pb.size();
    for (std::size_t i = 0; i < count; ++i) {
        const bool numA = isAllDigits(pa[i]);
        const bool numB = isAllDigits(pb[i]);
        if (numA && numB) {
            const std::string_view sa = pa[i].substr(pa[i].find_first_not_of('0'));
            const std::string_view sb = pb[i].substr(pb[i].find_first_not_of('0'));
            if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
            if (sa != sb) return sa < sb ? -1 : 1;
            continue;
        }
        if (numA != numB) return numA ? -1 : 1;  // numeric < alphanumeric
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    if (pa.size() != pb.size()) return pa.size() < pb.size() ? -1 : 1;
    return 0;
}

// A version with wildcard-allowed components, e.g. "1", "1.2", "1.x".
struct Partial {
    bool majorSet = false;
    bool minorSet = false;
    bool patchSet = false;
    long major = 0;
    long minor = 0;
    long patch = 0;
    std::string prerelease;

    bool isAny() const { return !majorSet; }
    bool isFull() const { return majorSet && minorSet && patchSet; }
};

bool parsePartial(std::string_view token, Partial& out) {
    out = Partial{};
    if (!token.empty() && (token.front() == 'v' || token.front() == 'V')) {
        token.remove_prefix(1);
    }
    if (token.empty()) return false;
    if (token == "*" || token == "x" || token == "X") return true;

    std::string_view core = token;
    const std::size_t plus = core.find('+');
    if (plus != std::string_view::npos) core = core.substr(0, plus);
    const std::size_t dash = core.find('-');
    if (dash != std::string_view::npos) {
        const std::string_view pre = core.substr(dash + 1);
        if (!validPrerelease(pre)) return false;
        out.prerelease = std::string(pre);
        core = core.substr(0, dash);
    }
    const std::vector<std::string_view> comps = splitView(core, '.');
    if (comps.empty() || comps.size() > 3) return false;
    bool* setFlags[3] = {&out.majorSet, &out.minorSet, &out.patchSet};
    long* values[3] = {&out.major, &out.minor, &out.patch};
    for (std::size_t i = 0; i < comps.size(); ++i) {
        if (comps[i] == "*" || comps[i] == "x" || comps[i] == "X") break;
        if (!parseLong(comps[i], *values[i])) return false;
        *setFlags[i] = true;
    }
    return out.majorSet;  // a wildcard-only "x.y" never sets the major
}

struct Comparator {
    char op = '=';  // one of = > < ^ ~
    bool orEqual = false;
    Partial version;
};

bool parseComparator(std::string_view token, Comparator& out) {
    out = Comparator{};
    if (token.empty()) return false;
    if (token.front() == '^' || token.front() == '~') {
        out.op = token.front();
        token.remove_prefix(1);
    } else if (token.front() == '>') {
        out.op = '>';
        token.remove_prefix(1);
        if (!token.empty() && token.front() == '=') {
            out.orEqual = true;
            token.remove_prefix(1);
        }
    } else if (token.front() == '<') {
        out.op = '<';
        token.remove_prefix(1);
        if (!token.empty() && token.front() == '=') {
            out.orEqual = true;
            token.remove_prefix(1);
        }
    } else if (token.front() == '=') {
        token.remove_prefix(1);
    }
    return parsePartial(token, out.version);
}

SemVer makeVersion(long major, long minor, long patch,
                   std::string prerelease = {}) {
    SemVer v;
    v.major = major;
    v.minor = minor;
    v.patch = patch;
    v.prerelease = std::move(prerelease);
    v.valid = true;
    return v;
}

// The lowest version a partial covers (unset components are zero).
SemVer lowerBound(const Partial& p) {
    return makeVersion(p.major, p.minorSet ? p.minor : 0,
                       p.patchSet ? p.patch : 0, p.prerelease);
}

// The exclusive boundary right after a partial's covered range.
SemVer nextBoundary(const Partial& p) {
    if (!p.minorSet) return makeVersion(p.major + 1, 0, 0);
    if (!p.patchSet) return makeVersion(p.major, p.minor + 1, 0);
    return lowerBound(p);
}

// Exclusive upper boundary for `^`.
SemVer caretBoundary(const Partial& p) {
    if (p.major != 0) return makeVersion(p.major + 1, 0, 0);
    if (p.minorSet && p.minor != 0) return makeVersion(0, p.minor + 1, 0);
    if (p.minorSet && p.minor == 0 && p.patchSet) {
        return makeVersion(0, 0, p.patch + 1);
    }
    if (!p.minorSet) return makeVersion(1, 0, 0);
    return makeVersion(0, 1, 0);  // ^0.0
}

// Exclusive upper boundary for `~`.
SemVer tildeBoundary(const Partial& p) {
    if (!p.minorSet) return makeVersion(p.major + 1, 0, 0);
    return makeVersion(p.major, p.minor + 1, 0);
}

bool comparatorMatches(const SemVer& v, const Comparator& c) {
    const Partial& p = c.version;
    if (p.isAny()) return true;
    if (c.op == '^') {
        return compareSemVer(v, lowerBound(p)) >= 0 &&
               compareSemVer(v, caretBoundary(p)) < 0;
    }
    if (c.op == '~') {
        return compareSemVer(v, lowerBound(p)) >= 0 &&
               compareSemVer(v, tildeBoundary(p)) < 0;
    }
    if (c.op == '>') {
        if (c.orEqual) return compareSemVer(v, lowerBound(p)) >= 0;
        return p.isFull() ? compareSemVer(v, lowerBound(p)) > 0
                          : compareSemVer(v, nextBoundary(p)) >= 0;
    }
    if (c.op == '<') {
        if (!c.orEqual) return compareSemVer(v, lowerBound(p)) < 0;
        return p.isFull() ? compareSemVer(v, lowerBound(p)) <= 0
                          : compareSemVer(v, nextBoundary(p)) < 0;
    }
    // '=': a partial means the whole covered range, a full version equality.
    if (p.isFull()) return compareSemVer(v, lowerBound(p)) == 0;
    return compareSemVer(v, lowerBound(p)) >= 0 &&
           compareSemVer(v, nextBoundary(p)) < 0;
}

struct Alternative {
    std::vector<Comparator> comparators;
    std::vector<Partial> prereleaseRefs;
};

bool parseAlternativeTokens(const std::vector<std::string_view>& words,
                            Alternative& out) {
    out = Alternative{};
    // Hyphen range, e.g. "1.2.3 - 2.3.4".
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (words[i] != "-") continue;
        if (i == 0 || i + 1 >= words.size()) return false;
        Comparator lo;
        Comparator hi;
        if (!parseComparator(words[i - 1], lo) || lo.op != '=') return false;
        if (!parseComparator(words[i + 1], hi) || hi.op != '=') return false;
        Alternative range;
        range.comparators.push_back(lo);
        Comparator upper;
        upper.op = '<';
        upper.orEqual = true;
        upper.version = hi.version;
        range.comparators.push_back(upper);
        out = std::move(range);
        return true;
    }
    for (std::string_view word : words) {
        Comparator c;
        if (!parseComparator(word, c)) return false;
        out.comparators.push_back(std::move(c));
    }
    return true;
}

void collectPrereleaseRefs(Alternative& alt) {
    for (const Comparator& c : alt.comparators) {
        if (!c.version.prerelease.empty()) alt.prereleaseRefs.push_back(c.version);
    }
}

bool alternativeMatches(const SemVer& v, const Alternative& alt) {
    for (const Comparator& c : alt.comparators) {
        if (!comparatorMatches(v, c)) return false;
    }
    if (v.prerelease.empty()) return true;
    // npm prerelease rule: a prerelease only matches when a comparator in the
    // same alternative pins the same major.minor.patch with a prerelease.
    for (const Partial& p : alt.prereleaseRefs) {
        if (p.major == v.major && p.minor == v.minor && p.patch == v.patch) {
            return true;
        }
    }
    return false;
}

}  // namespace

SemVer parseSemVer(std::string_view text) {
    SemVer out;
    std::string_view core = trim(text);
    if (!core.empty() && (core.front() == 'v' || core.front() == 'V')) {
        core.remove_prefix(1);
    }
    const std::size_t plus = core.find('+');
    if (plus != std::string_view::npos) core = core.substr(0, plus);
    const std::size_t dash = core.find('-');
    std::string_view pre;
    if (dash != std::string_view::npos) {
        pre = core.substr(dash + 1);
        core = core.substr(0, dash);
        if (!validPrerelease(pre)) return out;
    }
    const std::vector<std::string_view> comps = splitView(core, '.');
    if (comps.size() != 3) return out;
    if (!parseLong(comps[0], out.major) || !parseLong(comps[1], out.minor) ||
        !parseLong(comps[2], out.patch)) {
        return out;
    }
    out.prerelease = std::string(pre);
    out.valid = true;
    return out;
}

int compareSemVer(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.prerelease.empty() != b.prerelease.empty()) {
        return a.prerelease.empty() ? 1 : -1;
    }
    if (a.prerelease.empty()) return 0;
    return comparePrerelease(a.prerelease, b.prerelease);
}

bool semverSatisfies(const SemVer& version, std::string_view range) {
    if (!version.valid) return true;
    const std::string_view cleaned = trim(range);
    if (cleaned.empty()) return true;
    std::size_t start = 0;
    bool anyParsed = false;
    while (start <= cleaned.size()) {
        const std::size_t end = cleaned.find("||", start);
        const std::string_view part = trim(
            cleaned.substr(start, end == std::string_view::npos
                                      ? std::string_view::npos
                                      : end - start));
        if (!part.empty()) {
            std::vector<std::string_view> words;
            for (std::string_view token : splitView(part, ' ')) {
                token = trim(token);
                if (!token.empty()) words.push_back(token);
            }
            Alternative alt;
            if (!parseAlternativeTokens(words, alt)) return true;  // fail open
            collectPrereleaseRefs(alt);
            anyParsed = true;
            if (alternativeMatches(version, alt)) return true;
        }
        if (end == std::string_view::npos) break;
        start = end + 2;
    }
    return !anyParsed;
}

}  // namespace cp3::cli
