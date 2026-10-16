// =============================================================================
// src/runtime/tests/worker_test.cpp — `worker(...)`: structured-clone in/out, an
// isolated OS thread, an async body, and rejection propagation.
//
// The `Point` type below demonstrates the codegen contract: a `Cloner<T>`
// specialisation is what makes a user object deep-copy across the boundary.
// =============================================================================
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

#include "cp3/error.hpp"
#include "cp3/eventloop.hpp"
#include "cp3/promise.hpp"
#include "cp3/value.hpp"
#include "cp3/worker.hpp"
#include "harness.hpp"

using cp3::runtime::Promise;
using cp3::runtime::runEventLoop;
using cp3::runtime::Thrown;
using cp3::runtime::worker;

namespace {

struct Point {
    int x = 0;
    int y = 0;
};

}  // namespace

// The structured-clone customisation codegen emits for every user object type.
namespace cp3::runtime {

template <>
struct Cloner<Point> {
    static Point clone(const Point& value) { return Point{value.x, value.y}; }
};

}  // namespace cp3::runtime

namespace {

void testSyncRoundTrip() {
    int observed = 0;
    worker(21, [](int v) { return v * 2; }).then([&observed](int v) { observed = v; });
    runEventLoop();
    CP3_CHECK_EQ(observed, 42);
}

void testRunsOnAnotherThread() {
    const std::thread::id mainId = std::this_thread::get_id();
    std::thread::id workerId = mainId;
    worker(0, [](int) { return std::this_thread::get_id(); })
        .then([&workerId](std::thread::id id) { workerId = id; });
    runEventLoop();
    CP3_CHECK(workerId != mainId);
}

void testArgumentIsDeepCopiedIn() {
    Point original{3, 4};
    Point observed;
    worker(original, [](Point p) {
        p.x += 100;
        return p;
    }).then([&observed](Point result) { observed = result; });
    runEventLoop();
    CP3_CHECK_EQ(original.x, 3);    // the caller's graph is untouched
    CP3_CHECK_EQ(observed.x, 103);
    CP3_CHECK_EQ(observed.y, 4);
}

void testAsyncBody() {
    int observed = 0;
    worker(5, [](int v) -> Promise<int> { co_return v * 3; })
        .then([&observed](int v) { observed = v; });
    runEventLoop();
    CP3_CHECK_EQ(observed, 15);
}

void testRejectionCrossesBoundary() {
    std::string message = "unset";
    worker(1, [](int) -> int { throw std::runtime_error("worker-fail"); })
        .catchError([&message](const Thrown& reason) {
            message = reason.getOrThrow<std::string>();
            return 0;
        })
        .then([](int) {});
    runEventLoop();
    CP3_CHECK_EQ(message, std::string("worker-fail"));
}

}  // namespace

int main() {
    testSyncRoundTrip();
    testRunsOnAnotherThread();
    testArgumentIsDeepCopiedIn();
    testAsyncBody();
    testRejectionCrossesBoundary();
    return cp3test::summary("worker_test");
}
