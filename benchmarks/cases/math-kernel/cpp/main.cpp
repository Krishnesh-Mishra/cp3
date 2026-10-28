// math-kernel (cpp entry) -- evaluate sqrt, floor, abs, pow and clamp over
// 5,000,000 points, quantize each result to an integer and fold one checksum.
//
// Build:  g++ -std=c++20 -O2 -o math-kernel.exe main.cpp
// Run:    math-kernel.exe
//
// Same workload and same output in every language entry. sqrt is correctly
// rounded, pow uses an integer exponent, and every term is quantized to an
// integer before folding, so the checksum is exact across libms.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

static const int kCount = 5000000;
static const long long kMod = 1000000007;

int main() {
    long long checksum = 0;
    double sqrt_total = 0.0;
    for (int i = 1; i <= kCount; ++i) {
        const double x = i * 0.000001;
        const double s = std::sqrt(static_cast<double>(i));
        sqrt_total += s;
        const double f = std::floor(x + 0.5);
        const double a = std::abs(x - 2.5);
        const long long p =
            static_cast<long long>(std::pow(static_cast<double>(i), 2.0));
        const double c = std::clamp(x * 1000.0, 0.0, 1000.0);
        const double mixed = s + f + a + c;
        const long long quantum = static_cast<long long>(mixed * 1000000.0);
        checksum = (checksum * 31 + quantum + p) % kMod;
    }
    const long long sqrt_scaled = static_cast<long long>(sqrt_total * 1000.0);
    checksum = (checksum * 31 + sqrt_scaled) % kMod;
    std::cout << checksum << "\n";
    return 0;
}
