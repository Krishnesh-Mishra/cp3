// closure-capture (cpp entry) -- two closure shapes back to back over a
// 10,000,000-element source:
//   A: map / filter / reduce with lambdas that capture offset and cap
//   B: 1000 closures, each capturing its own key, stored in an array and
//      invoked after the creating scope has ended (escaping closures)
// Prints both sums on one line.
//
// Build:  g++ -std=c++20 -O2 -o closure-capture.exe main.cpp
// Run:    closure-capture.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). std::function is the owning type for escaping callables; the
// map/filter/reduce phase goes through the standard algorithm library.

#include <algorithm>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    const long long kSource = 10000000;
    const long long kOffset = 7;
    const long long kCap = 3;

    std::vector<long long> source((size_t)kSource);
    std::iota(source.begin(), source.end(), 0);

    std::vector<long long> mapped((size_t)kSource);
    std::transform(source.begin(), source.end(), mapped.begin(),
                   [kOffset](long long x) { return x + kOffset; });

    std::vector<long long> filtered;
    filtered.reserve((size_t)(kSource / kCap + 1));
    std::copy_if(mapped.begin(), mapped.end(), std::back_inserter(filtered),
                 [kCap](long long x) { return x % kCap == 0; });

    long long totalMapReduce =
        std::accumulate(filtered.begin(), filtered.end(), 0LL,
                        [](long long acc, long long x) { return acc + x; });

    std::vector<std::function<long long(long long)>> closures;
    closures.reserve(1000);
    for (long long key = 0; key < 1000; ++key) {
        closures.push_back([key, kOffset](long long x) { return x * key + kOffset; });
    }

    long long totalEscaping = 0;
    for (size_t index = 0; index < closures.size(); ++index) {
        for (long long call = 0; call < 3000; ++call) {
            totalEscaping += closures[index]((long long)index);
        }
    }

    std::cout << totalMapReduce << " " << totalEscaping << "\n";
    return 0;
}
