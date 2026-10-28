// object-churn (cpp entry) -- create 5,000,000 small heap objects in a hot
// loop: each iteration's temporary dies in scope, and every second object
// escapes into a vector. One checksum at the end.
//
// Build:  g++ -std=c++20 -O2 -o object-churn.exe main.cpp
// Run:    object-churn.exe
//
// Same workload and same output in every language entry (expected value in the
// task README). unique_ptr is the idiomatic owning handle; the compiler is
// free to scalarize what it can prove does not escape, which is exactly the
// honest thing to measure.

#include <iostream>
#include <memory>
#include <vector>

static const long long kObjects = 5000000;
static const long long kMod = 1000000007;

struct Cell {
    long long value;
    explicit Cell(long long v) : value(v) {}
};

int main() {
    std::vector<std::unique_ptr<Cell>> kept;
    kept.reserve((size_t)(kObjects / 2));

    long long acc = 0;
    for (long long i = 0; i < kObjects; ++i) {
        auto temp = std::make_unique<Cell>(i);
        acc = (acc + temp->value) % kMod;
        if (i % 2 == 0) {
            kept.push_back(std::make_unique<Cell>(i));
        }
    }

    long long checksum = acc;
    for (const auto& cell : kept) {
        checksum = (checksum * 31 + cell->value) % kMod;
    }
    std::cout << checksum << "\n";
    return 0;
}
