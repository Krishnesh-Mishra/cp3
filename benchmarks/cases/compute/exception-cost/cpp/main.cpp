// exception-cost (cpp entry) -- run 1,000,000 predicate calls twice: once
// throwing std::runtime_error("boom") on every third input and catching it,
// once returning a -1 sentinel. Prints both checksums on one line.
//
// Build:  g++ -std=c++20 -O3 -o exception-cost.exe main.cpp
// Run:    exception-cost.exe
//
// Same workload and same output in every language entry: 446742259 446742259.

#include <iostream>
#include <stdexcept>

static const long long kIterations = 1000000;
static const long long kMod = 1000000007;

static long long mayThrow(long long i) {
    if (i % 3 == 0) throw std::runtime_error("boom");
    return i;
}

static long long mayReturn(long long i) {
    if (i % 3 == 0) return -1;
    return i;
}

int main() {
    long long thrown = 0;
    for (long long i = 0; i < kIterations; ++i) {
        try {
            const long long v = mayThrow(i);
            thrown = (thrown * 31 + v) % kMod;
        } catch (const std::exception&) {
            thrown = (thrown * 31 + 1) % kMod;
        }
    }

    long long returned = 0;
    for (long long i = 0; i < kIterations; ++i) {
        const long long v = mayReturn(i);
        if (v < 0) {
            returned = (returned * 31 + 1) % kMod;
        } else {
            returned = (returned * 31 + v) % kMod;
        }
    }

    std::cout << thrown << " " << returned << "\n";
    return 0;
}
