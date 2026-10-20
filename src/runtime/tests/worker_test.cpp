// =============================================================================
// src/runtime/tests/worker_test.cpp - `new Worker(...)` handles: typed channel,
// deep copy both ways, FIFO delivery, error events, request/response and
// terminate.
//
// The `Point` type below demonstrates the codegen contract: a `Cloner<T>`
// specialisation is what makes a user object deep-copy across the boundary.
// =============================================================================
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "cp3/core/error.hpp"
#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"
#include "cp3/core/value.hpp"
#include "cp3/parallel/worker.hpp"
#include "harness.hpp"

using cp3::runtime::startWorker;
using cp3::runtime::Promise;
using cp3::runtime::runEventLoop;
using cp3::runtime::ThreadHandle;
using cp3::runtime::Thrown;
using cp3::runtime::WorkerHandle;

namespace {

struct Point {
    int x = 0;
    int y = 0;
};

// --- worker entries ---------------------------------------------------------

void echoEntry(ThreadHandle<int, int>& thread) {
    thread.onMessage([&thread](const int& v) { thread.postMessage(v * 2); });
}

void threadIdEntry(ThreadHandle<int, std::thread::id>& thread) {
    thread.onMessage([&thread](const int&) {
        thread.postMessage(std::this_thread::get_id());
    });
}

void pointEntry(ThreadHandle<Point, Point>& thread) {
    thread.onMessage([&thread](const Point& p) {
        Point out = p;
        out.x += 100;
        thread.postMessage(out);
    });
}

void tripleEntry(ThreadHandle<int, int>& thread) {
    thread.onMessage([&thread](const int& v) { thread.postMessage(v * 3); });
}

void throwingEntry(ThreadHandle<int, int>& thread) {
    thread.onMessage([](const int&) { throw std::runtime_error("boom"); });
}

void startupThrowEntry(ThreadHandle<int, int>&) {
    throw std::runtime_error("worker-startup");
}

}  // namespace

// The structured-clone customisation codegen emits for every user object type.
namespace cp3::runtime {

template <>
struct Cloner<Point> {
    static Point clone(const Point& value) { return Point{value.x, value.y}; }
};

}  // namespace cp3::runtime

namespace {

void testRoundTrip() {
    int observed = 0;
    WorkerHandle<int, int> w = startWorker<int, int>(&echoEntry);
    w.onMessage([&](const int& v) {
        observed = v;
        w.terminate();
    });
    w.postMessage(21);
    runEventLoop();
    CP3_CHECK_EQ(observed, 42);
}

void testRunsOnAnotherThread() {
    const std::thread::id mainId = std::this_thread::get_id();
    std::thread::id workerId = mainId;
    WorkerHandle<int, std::thread::id> w = startWorker<int, std::thread::id>(&threadIdEntry);
    w.onMessage([&](const std::thread::id& id) {
        workerId = id;
        w.terminate();
    });
    w.postMessage(0);
    runEventLoop();
    CP3_CHECK(workerId != mainId);
}

void testDeepCopyBothWays() {
    Point original{3, 4};
    Point observed;
    WorkerHandle<Point, Point> w = startWorker<Point, Point>(&pointEntry);
    w.onMessage([&](const Point& p) {
        observed = p;
        w.terminate();
    });
    w.postMessage(original);
    runEventLoop();
    CP3_CHECK_EQ(original.x, 3);  // the caller's graph is untouched
    CP3_CHECK_EQ(observed.x, 103);
    CP3_CHECK_EQ(observed.y, 4);
}

void testFifoDelivery() {
    std::vector<int> got;
    WorkerHandle<int, int> w = startWorker<int, int>(&tripleEntry);
    w.onMessage([&](const int& v) {
        got.push_back(v);
        if (got.size() == 4) {
            w.terminate();
        }
    });
    for (int i = 1; i <= 4; ++i) {
        w.postMessage(i);
    }
    runEventLoop();
    const std::vector<int> expected{3, 6, 9, 12};
    CP3_CHECK(got == expected);
}

void testRequestResolvesWithNextMessage() {
    int observed = 0;
    WorkerHandle<int, int> w = startWorker<int, int>(&echoEntry);
    w.request(7).then([&](const int& v) {
        observed = v;
        w.terminate();
    });
    runEventLoop();
    CP3_CHECK_EQ(observed, 14);
}

void testHandlerThrowBecomesErrorEvent() {
    std::string message = "unset";
    WorkerHandle<int, int> w = startWorker<int, int>(&throwingEntry);
    w.onError([&](const Thrown& reason) {
        if (const std::string* text = reason.get<std::string>()) {
            message = *text;
        }
        w.terminate();
    });
    w.postMessage(1);
    runEventLoop();
    CP3_CHECK_EQ(message, std::string("boom"));
}

void testStartupThrowBecomesErrorEvent() {
    std::string message = "unset";
    WorkerHandle<int, int> w = startWorker<int, int>(&startupThrowEntry);
    w.onError([&](const Thrown& reason) {
        if (const std::string* text = reason.get<std::string>()) {
            message = *text;
        }
    });
    runEventLoop();
    w.terminate();  // idempotent: the thread already ended
    CP3_CHECK_EQ(message, std::string("worker-startup"));
}

void testTerminateIsIdempotent() {
    int observed = 0;
    WorkerHandle<int, int> w = startWorker<int, int>(&echoEntry);
    w.onMessage([&](const int& v) {
        observed = v;
        w.terminate();
        w.terminate();
        w.terminate();
    });
    w.postMessage(5);
    runEventLoop();
    CP3_CHECK_EQ(observed, 10);
    w.terminate();
}

}  // namespace

int main() {
    testRoundTrip();
    testRunsOnAnotherThread();
    testDeepCopyBothWays();
    testFifoDelivery();
    testRequestResolvesWithNextMessage();
    testHandlerThrowBecomesErrorEvent();
    testStartupThrowBecomesErrorEvent();
    testTerminateIsIdempotent();
    return cp3test::summary("worker_test");
}
