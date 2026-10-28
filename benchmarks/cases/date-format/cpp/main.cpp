// date-format (cpp entry) -- construct UTC dates from one fixed epoch
// millisecond value, format the components manually, and fold a fixed diff.
//
// Build:  g++ -std=c++20 -O2 -o date-format.exe main.cpp
// Run:    date-format.exe
//
// Same workload, same fold and same output in every runnable language entry.
// The formatted spelling is built by hand (snprintf with numeric fields only),
// never through a locale-aware date-time format, so every language emits the
// same 19 ASCII characters: YYYY-MM-DDTHH:MM:SS.

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>

namespace {

constexpr long long kBaseMs = 1700000000000LL;   // 2023-11-14T22:13:20Z
constexpr long long kLaterMs = 1700000123456LL;  // +123,456 ms
constexpr long long kIterations = 50000;
constexpr long long kMod = 1000003;
constexpr long long kHourSeconds = 3600;

// '0'..'9' -> 0..9, '-' -> 10, 'T' -> 11, ':' -> 12. The same weights are
// used by every entry, so the character fold cannot drift between languages.
int charValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == '-') return 10;
    if (ch == 'T') return 11;
    return 12;
}

}  // namespace

int main() {
    const long long diffMs = kLaterMs - kBaseMs;
    long long checksum = 0;

    for (long long k = 0; k < kIterations; ++k) {
        const auto instant = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(kBaseMs + (k % kHourSeconds) * 1000));
        const std::time_t seconds = std::chrono::system_clock::to_time_t(instant);

        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &seconds);
#else
        gmtime_r(&seconds, &utc);
#endif

        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02dT%02d:%02d:%02d",
                      utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                      utc.tm_hour, utc.tm_min, utc.tm_sec);
        const std::string text(buffer);

        int charSum = 0;
        for (char ch : text) charSum += charValue(ch);

        checksum = (checksum * 31 + charSum + (long long)text.size() + diffMs) %
                   kMod;
    }

    std::cout << checksum << "\n";
    return 0;
}
