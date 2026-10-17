// =============================================================================
// src/runtime/tests/promise_test.cpp — Promise chaining, the coroutine `await`
// bridge, and event-loop ordering (cp3/promise.hpp + cp3/eventloop.hpp).
//
// Covers DECISIONS D40 (resolve/reject), D41 (awaitValue passthrough),
// D42 (chaining/flattening) and the microtask-vs-timer ordering contract.
// =============================================================================
#include <stdexcept>
#include <string>

#include "cp3/error.hpp"
#include "cp3/eventloop.hpp"
#include "cp3/promise.hpp"
#include "harness.hpp"

using cp3::runtime::awaitValue;
using cp3::runtime::Promise;
using cp3::runtime::runEventLoop;
using cp3::runtime::sleep;
using cp3::runtime::Thrown;

namespace {

// A coroutine whose awaited value is already settled (resumes synchronously).
Promise<int> asyncSum(int a, int b) {
    const int x = co_await awaitValue(Promise<int>::resolve(a));
    co_return x + b;
}

// A coroutine that really suspends: it awaits a timer.
Promise<int> asyncSleepy(int milliseconds) {
    co_await awaitValue(sleep(milliseconds));
    co_return 123;
}

// `await` on a non-Promise yields the value itself (DECISIONS D41).
Promise<int> asyncPlain(int seed) {
    const int v = co_await awaitValue(seed + 1);
    co_return v;
}

void testResolveThen() {
    int observed = 0;
    Promise<int> promise = Promise<int>::resolve(21);
    promise.then([&observed](int v) { observed = v * 2; });
    runEventLoop();
    CP3_CHECK_EQ(observed, 42);
}

void testThenFlattening() {
    int observed = 0;
    Promise<int> promise = Promise<int>::resolve(5);
    promise.then([&observed](int v) { return Promise<int>::resolve(v + 1); })
        .then([&observed](int v) { observed = v; });
    runEventLoop();
    CP3_CHECK_EQ(observed, 6);
}

void testCatchError() {
    int observed = 0;
    Promise<int> promise = Promise<int>::reject(Thrown::make(std::string("boom")));
    promise
        .catchError([&observed](const Thrown& reason) {
            observed = static_cast<int>(reason.getOrThrow<std::string>().size());
            return 7;
        })
        .then([&observed](int v) { observed += v; });
    runEventLoop();
    CP3_CHECK_EQ(observed, 11);  // 4 ("boom".size()) + 7
}

void testErrorPropagation() {
    std::string message;
    Promise<int> promise = Promise<int>::resolve(1);
    promise
        .then([](int) -> int { throw std::runtime_error("kaboom"); })
        .catchError([&message](const Thrown& reason) {
            message = reason.getOrThrow<std::string>();
            return 0;
        });
    runEventLoop();
    CP3_CHECK_EQ(message, std::string("kaboom"));
}

void testFinally() {
    bool ran = false;
    int observed = 0;
    Promise<int> promise = Promise<int>::resolve(9);
    promise.finally([&ran]() { ran = true; }).then([&observed](int v) { observed = v; });
    runEventLoop();
    CP3_CHECK(ran);
    CP3_CHECK_EQ(observed, 9);
}

// JS hands the value to a handler that declares no parameters and the handler
// simply ignores it (`g().then(() => …)`, M2-PLAN bug 7); the ABI adapts the
// arity in `detail::invokeHandler` instead of failing in `HandlerResult`.
void testNullaryHandlers() {
    int thenRan = 0;
    Promise<int>::resolve(3).then([&thenRan]() { ++thenRan; });
    runEventLoop();
    CP3_CHECK_EQ(thenRan, 1);

    int catchRan = 0;
    Promise<int>::reject(Thrown::make(std::string("boom")))
        .catchError([&catchRan]() { ++catchRan; });
    runEventLoop();
    CP3_CHECK_EQ(catchRan, 1);
}

void testPromiseVoid() {
    bool done = false;
    Promise<void>::resolve().then([&done]() { done = true; });
    runEventLoop();
    CP3_CHECK(done);
}

void testCoroutines() {
    int syncResult = 0;
    asyncSum(2, 3).then([&syncResult](int v) { syncResult = v; });
    runEventLoop();
    CP3_CHECK_EQ(syncResult, 5);

    int plainResult = 0;
    asyncPlain(10).then([&plainResult](int v) { plainResult = v; });
    runEventLoop();
    CP3_CHECK_EQ(plainResult, 11);

    int sleepyResult = 0;
    asyncSleepy(1).then([&sleepyResult](int v) { sleepyResult = v; });
    runEventLoop();
    CP3_CHECK_EQ(sleepyResult, 123);
}

void testMicrotasksBeforeTimers() {
    std::string order;
    sleep(0).then([&order]() { order += 'T'; });
    Promise<void>::resolve().then([&order]() { order += 'A'; });
    Promise<void>::resolve().then([&order]() { order += 'B'; });
    runEventLoop();
    CP3_CHECK_EQ(order, std::string("ABT"));
}

}  // namespace

int main() {
    testResolveThen();
    testThenFlattening();
    testCatchError();
    testErrorPropagation();
    testFinally();
    testNullaryHandlers();
    testPromiseVoid();
    testCoroutines();
    testMicrotasksBeforeTimers();
    return cp3test::summary("promise_test");
}
