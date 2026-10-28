// compute-multi (cpp entry) -- the compute-single workload split across
// std::thread::hardware_concurrency() workers, each summing a contiguous
// chunk of 1..1,000,000; the partials are combined on the main thread.
//
// Build:  g++ -std=c++20 -O2 -o compute-multi.exe main.cpp
// Run:    compute-multi.exe
//
// Prints the same single integer as every other entry (single or multi), so
// the outputs cross-check each other.

#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>

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

static long long sumRange(long long first, long long last) {
    long long total = 0;
    for (long long start = first; start <= last; ++start) {
        total += collatzSteps(start);
    }
    return total;
}

int main() {
    const long long N = 1000000;
    unsigned workers = std::thread::hardware_concurrency();
    if (workers == 0) workers = 1;
    const long long chunk = (N + workers - 1) / workers;

    std::vector<long long> partial(workers, 0);
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; ++w) {
        long long first = (long long)w * chunk + 1;
        long long last = std::min(N, (long long)(w + 1) * chunk);
        pool.emplace_back([&partial, w, first, last]() {
            partial[w] = sumRange(first, last);
        });
    }
    for (auto& worker : pool) worker.join();

    long long total = 0;
    for (long long value : partial) total += value;
    std::cout << total << "\n";
    return 0;
}
