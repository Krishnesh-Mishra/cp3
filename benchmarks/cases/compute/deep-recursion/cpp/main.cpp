// deep-recursion (cpp entry) -- recurse to DEPTH=5000 with a plain
// non-tail-recursive sumTo, 200 times, folding the triangular result.
//
// Build:  g++ -std=c++20 -O3 -o deep-recursion.exe main.cpp
// Run:    deep-recursion.exe
//
// Same workload and same output in every language entry: 292249866.

#include <iostream>

static const long long kDepth = 5000;
static const long long kRounds = 200;
static const long long kMod = 1000000007;

static long long sumTo(long long n) {
    if (n == 0) return 0;
    return n + sumTo(n - 1);
}

int main() {
    long long checksum = 0;
    for (long long round = 0; round < kRounds; ++round) {
        checksum = (checksum * 31 + sumTo(kDepth)) % kMod;
    }
    std::cout << checksum << "\n";
    return 0;
}
