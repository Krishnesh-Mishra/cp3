// =============================================================================
// src/common/src/suggest.cpp - "did you mean" candidate ranking DEFINITIONS.
//
// Implements cp3/suggest.hpp. See the header for the ranking contract.
//
// Why a hand-rolled UTF-8 decoder: the compiler reports column numbers in
// bytes, but typo distance must be measured per user-visible character ("emoi"
// vs "emoji" is one edit, not four). Invalid bytes decode to U+FFFD and count
// as one character, which keeps the metric sane on any input.
// =============================================================================
#include "cp3/suggest.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// Names are identifiers; anything longer is not a typo we want to rank.
constexpr std::size_t kMaxCodePoints = 128;

// ASCII case folding only: no locale, no ICU. cp3 identifiers are ASCII in
// practice, and non-ASCII case pairs folding "wrong" only changes the rank of
// an already-accepted candidate, never the accept/reject decision.
char foldAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::u32string decodeUtf8(const std::string& text) {
    std::u32string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = lead;
        std::size_t length = 1;
        if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1Fu;
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0Fu;
            length = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07u;
            length = 4;
        } else if (lead >= 0x80) {
            cp = 0xFFFD;  // stray continuation/overlong lead byte
        }
        bool valid = i + length <= text.size();
        for (std::size_t k = 1; valid && k < length; ++k) {
            const auto cont = static_cast<unsigned char>(text[i + k]);
            if ((cont & 0xC0) != 0x80) {
                valid = false;
            } else {
                cp = (cp << 6) | (cont & 0x3Fu);
            }
        }
        if (!valid) {
            cp = 0xFFFD;
            length = 1;
        }
        out.push_back(cp);
        i += length;
    }
    return out;
}

bool equalCaseInsensitive(const std::string& a, const std::string& b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (foldAscii(a[i]) != foldAscii(b[i])) return false;
    }
    return true;
}

bool startsWith(const std::u32string& text, const std::u32string& prefix) noexcept {
    return prefix.size() <= text.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin());
}

bool endsWith(const std::u32string& text, const std::u32string& suffix) noexcept {
    return suffix.size() <= text.size() &&
           std::equal(suffix.rbegin(), suffix.rend(), text.rbegin());
}

// Classic two-row Levenshtein over code points; bounded by kMaxCodePoints.
std::size_t editDistance(const std::u32string& a, const std::u32string& b) {
    const std::size_t n = b.size();
    std::vector<std::size_t> prev(n + 1);
    std::vector<std::size_t> cur(n + 1);
    for (std::size_t j = 0; j <= n; ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= n; ++j) {
            const std::uint32_t ca =
                (a[i - 1] >= U'A' && a[i - 1] <= U'Z') ? a[i - 1] + 32 : a[i - 1];
            const std::uint32_t cb =
                (b[j - 1] >= U'A' && b[j - 1] <= U'Z') ? b[j - 1] + 32 : b[j - 1];
            const std::size_t cost = ca == cb ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        prev.swap(cur);
    }
    return prev[n];
}

// A distance is acceptable when it stays within 1 + len/3 edits; short names
// tolerate one typo, longer ones two or three. The prefix/suffix bonus ranks
// truncations ("logh" -> "log") above unrelated names at the same distance.
std::size_t thresholdFor(std::size_t codePoints) noexcept {
    return 1 + codePoints / 3;
}

struct Scored {
    std::string name;
    bool caseOnly = false;  // differs from the miss only in case
    std::size_t score = 0;  // distance minus affix bonus
    std::size_t distance = 0;
    std::size_t length = 0;
};

}  // namespace

std::vector<std::string> suggestNames(const std::string& name,
                                      const std::vector<std::string>& candidates,
                                      std::size_t maxCount) {
    std::vector<std::string> out;
    if (name.empty() || maxCount == 0) return out;

    const std::u32string wanted = decodeUtf8(name);
    if (wanted.size() > kMaxCodePoints) return out;
    const std::size_t threshold = thresholdFor(wanted.size());

    std::vector<Scored> scored;
    for (const std::string& candidate : candidates) {
        if (candidate.empty() || candidate == name) continue;
        if (equalCaseInsensitive(name, candidate)) {
            scored.push_back({candidate, true, 0, 0, candidate.size()});
            continue;
        }
        const std::u32string text = decodeUtf8(candidate);
        if (text.size() > kMaxCodePoints) continue;
        // Length gap alone already exceeds the threshold: no point in the DP.
        const std::size_t gap =
            wanted.size() > text.size() ? wanted.size() - text.size() : text.size() - wanted.size();
        if (gap > threshold + 1) continue;

        const std::size_t distance = editDistance(wanted, text);
        if (distance > threshold) continue;
        std::size_t score = distance;
        if (distance <= 1 && wanted.size() >= 2 && text.size() >= 2 &&
            (startsWith(wanted, text) || startsWith(text, wanted) || endsWith(wanted, text) ||
             endsWith(text, wanted))) {
            score -= 1;
        }
        scored.push_back({candidate, false, score, distance, text.size()});
    }

    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.caseOnly != b.caseOnly) return a.caseOnly;
        if (a.score != b.score) return a.score < b.score;
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.length != b.length) return a.length < b.length;
        return a.name < b.name;
    });

    const std::size_t count = std::min(maxCount, scored.size());
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.push_back(scored[i].name);
    return out;
}

std::string suggestName(const std::string& name, const std::vector<std::string>& candidates) {
    const std::vector<std::string> ranked = suggestNames(name, candidates, 1);
    return ranked.empty() ? std::string() : ranked.front();
}

std::string didYouMeanPhrase(const std::vector<std::string>& names) {
    if (names.empty()) return {};
    std::string out = "Did you mean ";
    const std::size_t count = std::min<std::size_t>(names.size(), 3);
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out += (i + 1 == count) ? ((count == 2) ? " or " : ", or ") : ", ";
        }
        out += '\'';
        out += names[i];
        out += '\'';
    }
    out += '?';
    return out;
}

}  // namespace cp3
