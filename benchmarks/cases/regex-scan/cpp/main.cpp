// regex-scan (cpp entry) -- run the global pattern /[0-9]+/ with std::regex
// over a 240,000-byte ASCII text (a 48-byte unit repeated 5,000 times) for 8
// rounds, counting matches and summing their numeric values.
//
// Build:  g++ -std=c++20 -O3 -o regex-scan.exe main.cpp
// Run:    regex-scan.exe
//
// Same workload and same output in every language entry: 735703090.

#include <iostream>
#include <regex>
#include <string>

static const long long kRepeats = 5000;
static const long long kRounds = 8;
static const long long kMod = 1000000007;

int main() {
    const std::string unit =
        "alpha 123 beta 45 gamma 6789 delta 0 epsilon 42\n";

    std::string text;
    text.reserve(unit.size() * (size_t)kRepeats);
    for (long long r = 0; r < kRepeats; ++r) {
        text += unit;
    }

    const std::regex pattern("[0-9]+");

    long long checksum = 0;
    for (long long round = 0; round < kRounds; ++round) {
        long long count = 0;
        long long sum = 0;

        for (std::sregex_iterator it(text.cbegin(), text.cend(), pattern), end;
             it != end;
             ++it) {
            const std::smatch& match = *it;
            long long value = 0;
            for (char c : match.str()) {
                value = value * 10 + (c - '0');
            }
            sum += value;
            count += 1;
        }

        checksum = (checksum * 31 + count + sum) % kMod;
    }

    std::cout << checksum << "\n";
    return 0;
}
