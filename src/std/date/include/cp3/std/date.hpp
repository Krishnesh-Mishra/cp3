// =============================================================================
// cp3/std/date.hpp - `Date` (std/date) backing: immutable UTC millisecond Date.
//
// OWNERSHIP: the std/date family (src/std/date/**). Additive to the
// generated-code ABI (D14): header-only, everything `inline`, mirroring the
// STDLIB docs section 04 Date signature table one-to-one.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `Date.now()` / `Date.fromISOString(s)` -> `::cp3::runtime::date::now()` /
//   `fromIsoString(s)`. Instance members lower to the free functions below with
//   the receiver first: `d.addDays(n)` -> `addDays(d, n)`. Every "change"
//   operation returns a new Date and never mutates the receiver (docs 04:
//   immutability is deliberate).
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * The value is an integral count of milliseconds since 1970-01-01T00:00Z
//     (the JS time value), so `diffMs` is exact.
//   * All components and spellings are UTC. Docs 04 leaves the getter timezone
//     open; UTC keeps the toolchain deterministic across machines (recorded
//     decision, WAVE-STD-R2-DATE-REPORT.md).
//   * `toISOString()` is the JS spelling `YYYY-MM-DDTHH:MM:SS.sssZ`;
//     `toString()` is the same instant without the millisecond tail
//     (`YYYY-MM-DDTHH:MM:SSZ`) - documented as a report decision because docs
//     04 does not pin any output.
//   * `fromISOString` accepts `YYYY-MM-DD`, optionally followed by
//     `THH:MM[:SS[.fff]]`, optionally followed by `Z` or `+HH:MM`/`-HH:MM`;
//     a missing offset means UTC. Anything else parses as the epoch: no case
//     pins bad input, and a throwing form would abort uncaught programs.
//   * `addDays`/`addHours`/`addMinutes`/`addSeconds` round to the nearest
//     millisecond (`std::llround`), so fractional steps stay deterministic;
//     the cases only pin whole units.
// =============================================================================
#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace cp3::runtime::date {

// A UTC instant: milliseconds since the Unix epoch. Immutable in spirit: the
// free functions below return new values and take the receiver by const ref.
struct Date {
    std::int64_t ms = 0;

    friend bool operator==(const Date&, const Date&) = default;
};

// `Date.now()`: the current wall-clock instant.
inline Date now() {
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return Date{std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch)
                    .count()};
}

namespace detail {

constexpr std::int64_t kDayMs = 86400000;
constexpr std::int64_t kHourMs = 3600000;
constexpr std::int64_t kMinuteMs = 60000;
constexpr std::int64_t kSecondMs = 1000;

// A proleptic Gregorian calendar date (month 1..12, day 1..31).
struct Civil {
    int year = 1970;
    int month = 1;
    int day = 1;
};

// Howard Hinnant's `civil_from_days` (public-domain algorithm): day zero is
// 1970-01-01; the era arithmetic works for negative days too.
inline Civil civilFromDays(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;                          // 0..146096
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);    // 0..365
    const std::int64_t mp = (5 * doy + 2) / 153;                        // 0..11
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;                // 1..31
    const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;                   // 1..12
    Civil c;
    c.year = static_cast<int>(y + (m <= 2));
    c.month = static_cast<int>(m);
    c.day = static_cast<int>(d);
    return c;
}

// Howard Hinnant's `days_from_civil`: the inverse of civilFromDays; an
// out-of-range day (say February 31) normalizes forward exactly like JS.
inline std::int64_t daysFromCivil(int y, int m, int d) {
    const std::int64_t yy = y - (m <= 2 ? 1 : 0);
    const std::int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    const std::int64_t yoe = yy - era * 400;                            // 0..399
    const std::int64_t doy =
        (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;                // 0..365
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;     // 0..146096
    return era * 146097 + doe - 719468;
}

// Floor division; needed so pre-epoch instants split into day + remainder.
inline std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

// `n` zero-padded decimal digits of a non-negative value.
inline std::string padded(std::int64_t v, std::size_t width) {
    std::string s = std::to_string(v);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
}

// The UTC spelling shared by toIsoString/toString; `millis` adds `.sss`.
inline std::string utcSpelling(const Date& d, bool millis) {
    const std::int64_t days = floorDiv(d.ms, kDayMs);
    std::int64_t rem = d.ms - days * kDayMs;                            // 0..86399999
    const Civil c = civilFromDays(days);
    const std::int64_t hour = rem / kHourMs;
    rem %= kHourMs;
    const std::int64_t minute = rem / kMinuteMs;
    rem %= kMinuteMs;
    const std::int64_t second = rem / kSecondMs;
    std::string out = padded(c.year, 4) + "-" + padded(c.month, 2) + "-" +
                      padded(c.day, 2) + "T" + padded(hour, 2) + ":" +
                      padded(minute, 2) + ":" + padded(second, 2);
    if (millis) out += "." + padded(rem % kSecondMs, 3);
    return out + "Z";
}

// Parses exactly `count` decimal digits at `s[i]`, advancing `i` on success.
inline bool fixedDigits(std::string_view s, std::size_t& i, int count,
                        int& out) {
    if (i + static_cast<std::size_t>(count) > s.size()) return false;
    int value = 0;
    for (int k = 0; k < count; ++k) {
        const char c = s[i + static_cast<std::size_t>(k)];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    i += static_cast<std::size_t>(count);
    out = value;
    return true;
}

inline bool literalAt(std::string_view s, std::size_t& i, char expected) {
    if (i >= s.size() || s[i] != expected) return false;
    ++i;
    return true;
}

// `.12345...` -> the first three digits as milliseconds (sub-ms truncated).
inline bool fractionMillis(std::string_view s, std::size_t& i, int& out) {
    if (i >= s.size() || s[i] != '.') return false;
    ++i;
    int value = 0;
    int taken = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        if (taken < 3) {
            value = value * 10 + (s[i] - '0');
            ++taken;
        }
        ++i;
    }
    if (taken == 0) return false;
    while (taken < 3) {
        value *= 10;
        ++taken;
    }
    out = value;
    return true;
}

// `Z` or `+HH:MM`/`-HH:MM`; returns the minutes to SUBTRACT from wall time.
inline bool offsetMinutes(std::string_view s, std::size_t& i, int& out) {
    out = 0;
    if (i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
        ++i;
        return true;
    }
    if (i >= s.size() || (s[i] != '+' && s[i] != '-')) return true;
    const int sign = s[i] == '-' ? -1 : 1;
    ++i;
    int hour = 0;
    int minute = 0;
    if (!fixedDigits(s, i, 2, hour) || !literalAt(s, i, ':') ||
        !fixedDigits(s, i, 2, minute)) {
        return false;
    }
    out = sign * (hour * 60 + minute);
    return true;
}

// One ISO-8601 timestamp in the documented subset.
struct Stamp {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millis = 0;
    int offset = 0;
};

// Parses a full timestamp; false on any deviation from the subset.
inline bool parseStamp(std::string_view s, Stamp& out) {
    std::size_t i = 0;
    if (!fixedDigits(s, i, 4, out.year) || !literalAt(s, i, '-') ||
        !fixedDigits(s, i, 2, out.month) || !literalAt(s, i, '-') ||
        !fixedDigits(s, i, 2, out.day)) {
        return false;
    }
    if (i < s.size() && (s[i] == 'T' || s[i] == 't')) {
        ++i;
        if (!fixedDigits(s, i, 2, out.hour) || !literalAt(s, i, ':') ||
            !fixedDigits(s, i, 2, out.minute)) {
            return false;
        }
        if (i < s.size() && s[i] == ':') {
            ++i;
            if (!fixedDigits(s, i, 2, out.second)) return false;
            if (i < s.size() && s[i] == '.') {
                if (!fractionMillis(s, i, out.millis)) return false;
            }
        }
    }
    if (!offsetMinutes(s, i, out.offset)) return false;
    if (i != s.size()) return false;
    return out.month >= 1 && out.month <= 12 && out.day >= 1 && out.day <= 31 &&
           out.hour <= 23 && out.minute <= 59 && out.second <= 59;
}

}  // namespace detail

// `Date.fromISOString(s)`: strict ISO-8601 subset, bad input -> the epoch.
inline Date fromIsoString(const std::string& text) {
    detail::Stamp stamp;
    if (!detail::parseStamp(std::string_view(text), stamp)) return Date{};
    const std::int64_t days = detail::daysFromCivil(stamp.year, stamp.month,
                                                    stamp.day);
    const std::int64_t wall =
        days * detail::kDayMs + stamp.hour * detail::kHourMs +
        stamp.minute * detail::kMinuteMs + stamp.second * detail::kSecondMs +
        stamp.millis;
    return Date{wall - static_cast<std::int64_t>(stamp.offset) * detail::kMinuteMs};
}

// `toISOString()`: `YYYY-MM-DDTHH:MM:SS.sssZ` (JS-compatible, UTC).
inline std::string toIsoString(const Date& d) {
    return detail::utcSpelling(d, true);
}

// `toString()`: stable UTC ISO-style spelling without the millisecond tail.
inline std::string toString(const Date& d) {
    return detail::utcSpelling(d, false);
}

// Printing: `console.log(d)` / `${d}` reach the generated helper's `cp3_ts`
// overload set, which has no Date arm; this ADL-visible overload makes a bare
// Date print exactly `toString(d)`. Deliberate report decision (docs 04 does
// not pin direct printing; `String(d)` still goes through the core
// `cp3::runtime::toString` overload set and stays a reported handoff).
inline std::string cp3_ts(const Date& d) {
    return toString(d);
}

// The UTC getters; `getMonth` is 0-indexed, matching JS (docs 04).
inline double getFullYear(const Date& d) {
    return detail::civilFromDays(detail::floorDiv(d.ms, detail::kDayMs)).year;
}

inline double getMonth(const Date& d) {
    return detail::civilFromDays(detail::floorDiv(d.ms, detail::kDayMs)).month - 1;
}

inline double getDate(const Date& d) {
    return detail::civilFromDays(detail::floorDiv(d.ms, detail::kDayMs)).day;
}

inline double getHours(const Date& d) {
    const std::int64_t rem = d.ms - detail::floorDiv(d.ms, detail::kDayMs) *
                                       detail::kDayMs;
    return static_cast<double>(rem / detail::kHourMs);
}

inline double getMinutes(const Date& d) {
    const std::int64_t rem = d.ms - detail::floorDiv(d.ms, detail::kDayMs) *
                                       detail::kDayMs;
    return static_cast<double>((rem % detail::kHourMs) / detail::kMinuteMs);
}

inline double getSeconds(const Date& d) {
    const std::int64_t rem = d.ms - detail::floorDiv(d.ms, detail::kDayMs) *
                                       detail::kDayMs;
    return static_cast<double>((rem % detail::kMinuteMs) / detail::kSecondMs);
}

namespace detail {

// A `number` step of `unit` milliseconds, rounded to the nearest millisecond.
inline std::int64_t scaledMilliseconds(double n, std::int64_t unit) {
    return static_cast<std::int64_t>(
        std::llround(n * static_cast<double>(unit)));
}

}  // namespace detail

// The `add*` family: a NEW Date, never a mutation (docs 04).
inline Date addDays(const Date& d, double n) {
    return Date{d.ms + detail::scaledMilliseconds(n, detail::kDayMs)};
}

inline Date addHours(const Date& d, double n) {
    return Date{d.ms + detail::scaledMilliseconds(n, detail::kHourMs)};
}

inline Date addMinutes(const Date& d, double n) {
    return Date{d.ms + detail::scaledMilliseconds(n, detail::kMinuteMs)};
}

inline Date addSeconds(const Date& d, double n) {
    return Date{d.ms + detail::scaledMilliseconds(n, detail::kSecondMs)};
}

// Comparisons + `diffMs` (this minus other, in milliseconds).
inline bool isBefore(const Date& d, const Date& other) {
    return d.ms < other.ms;
}

inline bool isAfter(const Date& d, const Date& other) {
    return d.ms > other.ms;
}

inline double diffMs(const Date& d, const Date& other) {
    return static_cast<double>(d.ms - other.ms);
}

}  // namespace cp3::runtime::date
