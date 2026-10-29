// typed-array-sum (cpp entry) -- fill a fixed Float64Array and Uint8Array,
// sum the floats for 32 rounds, then fold the float and byte totals into one
// integer checksum.
//
// Build:  g++ -std=c++20 -O3 -o typed-array-sum.exe main.cpp
// Run:    typed-array-sum.exe
//
// Same workload and same output in every language entry. The float pattern is
// a multiple of 0.25 and every partial sum stays far below 2^53, so the
// addition is exact in IEEE-754 doubles.

#include <cstdint>
#include <iostream>
#include <vector>

static const int kSize = 1000000;
static const int kRounds = 32;
static const long long kMod = 1000000007;

int main() {
    std::vector<double> values(kSize);
    std::vector<std::uint8_t> bytes(kSize);
    for (int i = 0; i < kSize; ++i) {
        values[i] = (i % 97) * 0.5 + (i % 13) * 0.25;
        bytes[i] = static_cast<std::uint8_t>((i * 31 + 7) % 256);
    }

    double total = 0.0;
    for (int round = 0; round < kRounds; ++round) {
        double acc = 0.0;
        for (int i = 0; i < kSize; ++i) acc += values[i];
        total += acc;
    }

    long long byte_total = 0;
    for (int i = 0; i < kSize; ++i) byte_total += bytes[i];

    const long long scaled = static_cast<long long>(total * 4.0);
    long long checksum = (scaled + byte_total) % kMod;
    checksum = (checksum * 31 + kSize + kRounds) % kMod;
    std::cout << checksum << "\n";
    return 0;
}
