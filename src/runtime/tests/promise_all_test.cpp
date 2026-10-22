// =============================================================================
// src/runtime/tests/promise_all_test.cpp - `Promise.all` / `.race` /
// `.allSettled` (cp3/async/promise_combinators.hpp).
//
// Verifies input-order results, first-rejection wins, and that combining always
// observes every input (so a combinator never itself leaks an unhandled
// rejection).
// =============================================================================
#include <cstddef>
#include <string>
#include <vector>

#include "cp3/core/error.hpp"
#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"
#include "harness.hpp"

using cp3::runtime::all;
using cp3::runtime::Array;
using cp3::runtime::makeArray;
using cp3::runtime::allSettled;
using cp3::runtime::Promise;
using cp3::runtime::race;
using cp3::runtime::runEventLoop;
using cp3::runtime::Settled;
using cp3::runtime::sleep;
using cp3::runtime::Thrown;

namespace {

void testAllFulfilledInOrder() {
    std::vector<Promise<int>> promises{
        Promise<int>::resolve(1),
        Promise<int>::resolve(2),
        Promise<int>::resolve(3),
    };
    std::vector<int> observed;
    all(promises).then([&observed](const Array<int>& values) { observed = *values; });
    runEventLoop();
    CP3_CHECK_EQ(observed.size(), std::size_t(3));
    CP3_CHECK_EQ(observed[0], 1);
    CP3_CHECK_EQ(observed[1], 2);
    CP3_CHECK_EQ(observed[2], 3);
}

void testAllPreservesOrderWhenResolvedOutOfOrder() {
    std::vector<Promise<int>> promises;
    promises.push_back(sleep(10).then([]() { return 10; }));
    promises.push_back(sleep(1).then([]() { return 1; }));
    std::vector<int> observed;
    all(promises).then([&observed](const Array<int>& values) { observed = *values; });
    runEventLoop();
    CP3_CHECK_EQ(observed.size(), std::size_t(2));
    CP3_CHECK_EQ(observed[0], 10);  // input order, not completion order
    CP3_CHECK_EQ(observed[1], 1);
}

void testAllEmpty() {
    std::vector<Promise<int>> promises;
    std::vector<int> observed{99};
    all(promises).then([&observed](const Array<int>& values) { observed = *values; });
    runEventLoop();
    CP3_CHECK_EQ(observed.size(), std::size_t(0));
}

void testAllRejectsOnFirstFailure() {
    std::vector<Promise<int>> promises{
        Promise<int>::resolve(1),
        Promise<int>::reject(Thrown::make(std::string("bad"))),
    };
    std::string message = "unset";
    all(promises)
        .catchError([&message](const Thrown& reason) {
            message = reason.getOrThrow<std::string>();
            return makeArray<int>(std::vector<int>{});
        })
        .then([](const Array<int>&) {});
    runEventLoop();
    CP3_CHECK_EQ(message, std::string("bad"));
}

void testAllVoid() {
    std::vector<Promise<void>> promises{Promise<void>::resolve(), Promise<void>::resolve()};
    bool done = false;
    all(promises).then([&done]() { done = true; });
    runEventLoop();
    CP3_CHECK(done);
}

void testRace() {
    std::vector<Promise<int>> promises{Promise<int>::resolve(7), Promise<int>::resolve(9)};
    int observed = 0;
    race(promises).then([&observed](int v) { observed = v; });
    runEventLoop();
    CP3_CHECK_EQ(observed, 7);  // first settled wins
}

void testAllSettled() {
    std::vector<Promise<int>> promises{
        Promise<int>::resolve(1),
        Promise<int>::reject(Thrown::make(std::string("no"))),
        Promise<int>::resolve(3),
    };
    std::vector<Settled<int>> observed;
    allSettled(promises).then(
        [&observed](const Array<Settled<int>>& results) { observed = *results; });
    runEventLoop();
    CP3_CHECK_EQ(observed.size(), std::size_t(3));
    CP3_CHECK(observed[0].ok);
    CP3_CHECK_EQ(observed[0].value, 1);
    CP3_CHECK(!observed[1].ok);
    CP3_CHECK_EQ(observed[1].reason.getOrThrow<std::string>(), std::string("no"));
    CP3_CHECK(observed[2].ok);
    CP3_CHECK_EQ(observed[2].value, 3);
}

}  // namespace

int main() {
    testAllFulfilledInOrder();
    testAllPreservesOrderWhenResolvedOutOfOrder();
    testAllEmpty();
    testAllRejectsOnFirstFailure();
    testAllVoid();
    testRace();
    testAllSettled();
    return cp3test::summary("promise_all_test");
}
