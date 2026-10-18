// array-growth (cpp entry) -- push 20,000,000 numbers into a growing array
// while summing them, pop half of them back off, then print the sum.
//
// Build:  g++ -std=c++20 -O2 -o array-growth.exe main.cpp
// Run:    array-growth.exe
//
// Same workload and same output in every language entry (expected value in the
// task README). The vector is deliberately NOT reserved: doubling growth,
// element moves and the final shrink are the workload.

#include <iostream>
#include <vector>

int main() {
    const long long kPushes = 20000000;
    std::vector<long long> values;
    long long total = 0;
    for (long long i = 1; i <= kPushes; ++i) {
        values.push_back(i);
        total += i;
    }
    for (long long k = 0; k < kPushes / 2; ++k) {
        values.pop_back();
    }
    std::cout << total << "\n";
    return 0;
}
