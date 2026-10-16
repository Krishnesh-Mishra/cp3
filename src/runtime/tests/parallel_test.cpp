// =============================================================================
// src/runtime/tests/parallel_test.cpp — `parallel.for` / `parallel.reduce` /
// `parallel.run` (cp3/parallel.hpp).
//
// Verifies deterministic results, the synchronous (blocking) contract, and that
// nested `parallel.*` runs inline instead of deadlocking on the busy pool.
// =============================================================================
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "cp3/parallel.hpp"
#include "harness.hpp"

using cp3::runtime::parallelFor;
using cp3::runtime::parallelReduce;
using cp3::runtime::parallelRun;

namespace {

constexpr std::int64_t kCount = 1000;
constexpr long long kExpectedSum = 499500;  // 0 + 1 + ... + 999

void testParallelFor() {
    std::vector<long long> slots(static_cast<std::size_t>(kCount), 0);
    parallelFor(0, kCount, [&slots](std::int64_t i) {
        slots[static_cast<std::size_t>(i)] = i;  // disjoint writes, one per index
    });
    long long total = 0;
    for (long long value : slots) {
        total += value;
    }
    CP3_CHECK_EQ(total, kExpectedSum);
}

void testParallelReduceSequentialFallback() {
    const long long total = parallelReduce<long long>(
        0, kCount, 0, [](long long acc, std::int64_t i) { return acc + i; });
    CP3_CHECK_EQ(total, kExpectedSum);
}

void testParallelReduceChunked() {
    const long long total = parallelReduce<long long>(
        0, kCount, 0, [](long long acc, std::int64_t i) { return acc + i; },
        [](long long a, long long b) { return a + b; });
    CP3_CHECK_EQ(total, kExpectedSum);
}

void testParallelReduceEmptyRange() {
    const long long total = parallelReduce<long long>(
        5, 5, 7, [](long long acc, std::int64_t i) { return acc + i; },
        [](long long a, long long b) { return a + b; });
    CP3_CHECK_EQ(total, 7);  // init, unchanged
}

void testParallelRun() {
    const std::array<int, 3> results =
        parallelRun([]() { return 1; }, []() { return 2; }, []() { return 3; });
    CP3_CHECK_EQ(results.size(), std::size_t(3));
    CP3_CHECK_EQ(results[0], 1);
    CP3_CHECK_EQ(results[1], 2);
    CP3_CHECK_EQ(results[2], 3);
}

void testNestedParallelDoesNotDeadlock() {
    std::atomic<int> counter{0};
    parallelFor(0, 4, [&counter](std::int64_t) {
        parallelFor(0, 4, [&counter](std::int64_t) { counter.fetch_add(1); });
    });
    CP3_CHECK_EQ(counter.load(), 16);
}

}  // namespace

int main() {
    testParallelFor();
    testParallelReduceSequentialFallback();
    testParallelReduceChunked();
    testParallelReduceEmptyRange();
    testParallelRun();
    testNestedParallelDoesNotDeadlock();
    return cp3test::summary("parallel_test");
}
