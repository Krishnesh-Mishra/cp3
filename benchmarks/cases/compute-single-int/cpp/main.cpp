// compute-single-int (cpp entry) -- total Collatz steps for every start value
// in 1..1,000,000, printed as one integer. Same workload and output as every
// other entry; this is the native-integer tier (D65): long long throughout.
//
// Build:  g++ -std=c++20 -O2 -o compute-single-int.exe main.cpp
// Run:    compute-single-int.exe

#include <iostream>

static long long collatzSteps(long long start) {
    long long n = start;
    long long steps = 0;
    while (n != 1) {
        if (n % 2 == 0) {
            n = n / 2;
        } else {
            n = 3 * n + 1;
        }
        steps += 1;
    }
    return steps;
}

int main() {
    const long long N = 1000000;
    long long total = 0;
    for (long long start = 1; start <= N; ++start) {
        total += collatzSteps(start);
    }
    std::cout << total << "\n";
    return 0;
}
