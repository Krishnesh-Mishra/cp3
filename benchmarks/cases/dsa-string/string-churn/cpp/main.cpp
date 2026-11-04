// string-churn (cpp entry) -- build a 4 KiB string, then repeatedly rebuild
// strings from it: uppercase, slice, lowercase + re-concatenate, re-slice.
//
// Build:  g++ -std=c++20 -O3 -o string-churn.exe main.cpp
// Run:    string-churn.exe [iterations]        (default 50000)
//
// Same workload and same output in every language entry. Every round allocates
// new strings, which is what makes this task exercise allocators and GCs. The
// per-iteration fold (checksum * 31 + first 'c' position + length) depends on
// the exact string contents, so all six languages must print the same number.
//
// Fairness note: the alphabet is pinned ASCII, so the case helpers use direct
// ASCII conversion instead of locale-aware std::toupper/std::tolower, which
// re-checks the C locale on every char and is ~25x slower on this toolchain.
// That would slow the C++ entry for a reason that has nothing to do with the
// task; same output, same workload, no sandbagging.

#include <cstdlib>
#include <iostream>
#include <string>

static const long long kDefaultIterations = 50000;

static std::string toUpper(const std::string& text) {
    std::string out = text;
    for (char& c : out) if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    return out;
}

static std::string toLower(const std::string& text) {
    std::string out = text;
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
    return out;
}

int main(int argc, char** argv) {
    long long iterations = argc > 1 ? std::atoll(argv[1]) : kDefaultIterations;

    const std::string seed = "abcXYZ0123456789";
    std::string data;
    while (data.size() < 4096) data += seed;

    long long checksum = 0;
    for (long long k = 0; k < iterations; ++k) {
        std::string s = data;
        for (int round = 0; round < 8; ++round) {
            const std::string upper = toUpper(s);
            const std::string sliced = upper.substr(3, upper.size() - 4);
            std::string rebuilt = toLower(sliced);
            rebuilt += sliced.substr(0, 8);
            s = rebuilt.substr(2);
        }
        size_t found = s.find('c');
        long long position = found == std::string::npos ? -1 : (long long)found;
        checksum = (checksum * 31 + position + (long long)s.size()) % 1000003;
    }
    std::cout << checksum << "\n";
    return 0;
}
