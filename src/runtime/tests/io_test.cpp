// =============================================================================
// src/runtime/tests/io_test.cpp - `std/io` reactor unit driver.
//
// Covers the v0.2 phase 1-2 acceptance cases against real socket descriptors:
//   1. a 100 ms timer keeps firing while a socket wait resolves after ~2 s;
//   2. 1000 concurrent waiters on distinct sockets all settle;
//   3. closing a descriptor while a wait is registered rejects;
//   4. readable and writable waiters can share one descriptor;
//   5. `pollUntilDone` re-reads the socket every iteration and honours every
//      POLL_* state.
//
// The driver links the runtime directly and uses the tiny harness in
// harness.hpp (STYLE.md section 7). Build with -DCP3_RUNTIME_TESTS=ON.
// =============================================================================
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/std/io.hpp"
#include "harness.hpp"

using cp3::runtime::awaitValue;
using cp3::runtime::currentLoop;
using cp3::runtime::Promise;
using cp3::runtime::runEventLoop;
using cp3::runtime::Thrown;
using cp3::runtime::io::pollUntilDone;
using cp3::runtime::io::waitReadable;
using cp3::runtime::io::waitWritable;
using cp3::runtime::io::POLL_DONE;
using cp3::runtime::io::POLL_FAILED;
using cp3::runtime::io::POLL_READING;
using cp3::runtime::io::POLL_WRITING;

namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;

void ensureSockets() {
    static const bool started = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
}

void closeSocket(Socket socket) {
    if (socket != kInvalidSocket) closesocket(socket);
}

bool makePair(Socket& left, Socket& right) {
    ensureSockets();
    Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        closeSocket(listener);
        return false;
    }

    int length = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        closeSocket(listener);
        return false;
    }

    Socket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == kInvalidSocket) {
        closeSocket(listener);
        return false;
    }
    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket(client);
        closeSocket(listener);
        return false;
    }

    Socket server = accept(listener, nullptr, nullptr);
    closeSocket(listener);
    if (server == kInvalidSocket) {
        closeSocket(client);
        return false;
    }
    left = client;
    right = server;
    return true;
}

bool sendByte(Socket socket) {
    char value = 'x';
    return send(socket, &value, 1, 0) == 1;
}
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;

void ensureSockets() {}

void closeSocket(Socket socket) {
    if (socket != kInvalidSocket) ::close(socket);
}

bool makePair(Socket& left, Socket& right) {
    int sockets[2] = {kInvalidSocket, kInvalidSocket};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return false;
    left = sockets[0];
    right = sockets[1];
    return true;
}

bool sendByte(Socket socket) {
    char value = 'x';
    return ::send(socket, &value, 1, 0) == 1;
}
#endif

struct Observation {
    bool resolved = false;
    bool rejected = false;
    std::string error;
};

Promise<void> observeReadable(Socket socket, Observation& observation) {
    try {
        co_await awaitValue(waitReadable(static_cast<double>(socket)));
        observation.resolved = true;
    } catch (const Thrown& thrown) {
        observation.rejected = true;
        observation.error = thrown.asError()->message;
    }
}

Promise<void> observeWritable(Socket socket, Observation& observation) {
    try {
        co_await awaitValue(waitWritable(static_cast<double>(socket)));
        observation.resolved = true;
    } catch (const Thrown& thrown) {
        observation.rejected = true;
        observation.error = thrown.asError()->message;
    }
}

Promise<void> observePoll(std::function<double()> getSocket,
                          std::function<double()> checkProgress,
                          Observation& observation) {
    try {
        co_await awaitValue(pollUntilDone(std::move(getSocket), std::move(checkProgress)));
        observation.resolved = true;
    } catch (const Thrown& thrown) {
        observation.rejected = true;
        observation.error = thrown.asError()->message;
    }
}

void closePair(Socket& left, Socket& right) {
    closeSocket(left);
    closeSocket(right);
    left = kInvalidSocket;
    right = kInvalidSocket;
}

// 1. A 100 ms timer fires repeatedly while a socket wait resolves after ~2 s.
void testTimerBeatsSocketWait() {
    Socket reader = kInvalidSocket;
    Socket writer = kInvalidSocket;
    CP3_CHECK(makePair(reader, writer));

    Observation observation;
    int ticks = 0;
    const auto watchdog = currentLoop().addTimer(4000, [] { currentLoop().stop(); });
    for (int i = 1; i <= 20; ++i) {
        currentLoop().addTimer(i * 100, [&ticks] { ++ticks; });
    }
    currentLoop().addTimer(2000, [writer] { (void)sendByte(writer); });

    auto waiter = observeReadable(reader, observation);
    auto guard = waiter.then([watchdog] { currentLoop().cancelTimer(watchdog); });
    runEventLoop();
    currentLoop().cancelTimer(watchdog);

    CP3_CHECK(observation.resolved);
    CP3_CHECK(!observation.rejected);
    CP3_CHECK(ticks >= 10);
    CP3_CHECK(ticks <= 20);
    closePair(reader, writer);
}

// 2. 1000 concurrent waiters on distinct sockets all settle on one loop.
void testThousandWaiters() {
    constexpr int kWaiters = 1000;
    std::vector<Socket> readers(kWaiters, kInvalidSocket);
    std::vector<Socket> writers(kWaiters, kInvalidSocket);
    std::vector<Observation> observations(kWaiters);
    std::vector<Promise<void>> waiters;
    std::vector<Promise<void>> guards;
    waiters.reserve(kWaiters);
    guards.reserve(kWaiters);

    bool allPairs = true;
    for (int i = 0; i < kWaiters; ++i) {
        if (!makePair(readers[i], writers[i])) {
            allPairs = false;
            break;
        }
    }
    CP3_CHECK(allPairs);
    if (!allPairs) {
        for (int i = 0; i < kWaiters; ++i) closePair(readers[i], writers[i]);
        return;
    }

    const auto watchdog = currentLoop().addTimer(4000, [] { currentLoop().stop(); });
    int settled = 0;
    for (int i = 0; i < kWaiters; ++i) {
        auto waiter = observeReadable(readers[i], observations[i]);
        waiters.push_back(waiter);
        guards.push_back(waiter.then([&settled, watchdog] {
            if (++settled == kWaiters) currentLoop().cancelTimer(watchdog);
        }));
    }

    currentLoop().addTimer(100, [&writers] {
        for (Socket writer : writers) (void)sendByte(writer);
    });
    runEventLoop();
    currentLoop().cancelTimer(watchdog);

    int resolved = 0;
    int rejected = 0;
    for (const Observation& observation : observations) {
        if (observation.resolved) ++resolved;
        if (observation.rejected) ++rejected;
    }
    CP3_CHECK_EQ(resolved, kWaiters);
    CP3_CHECK_EQ(rejected, 0);

    for (int i = 0; i < kWaiters; ++i) closePair(readers[i], writers[i]);
}

// 3. Readable and writable waiters share one descriptor.
void testBothDirections() {
    Socket reader = kInvalidSocket;
    Socket writer = kInvalidSocket;
    CP3_CHECK(makePair(reader, writer));

    Observation readable;
    Observation writable;
    const auto watchdog = currentLoop().addTimer(2000, [] { currentLoop().stop(); });
    int settled = 0;
    auto readWait = observeReadable(reader, readable);
    auto readGuard = readWait.then([&settled, watchdog] {
        if (++settled == 2) currentLoop().cancelTimer(watchdog);
    });
    auto writeWait = observeWritable(reader, writable);
    auto writeGuard = writeWait.then([&settled, watchdog] {
        if (++settled == 2) currentLoop().cancelTimer(watchdog);
    });
    currentLoop().addTimer(100, [writer] { (void)sendByte(writer); });
    runEventLoop();
    currentLoop().cancelTimer(watchdog);

    CP3_CHECK(readable.resolved);
    CP3_CHECK(writable.resolved);
    closePair(reader, writer);
}

// 4. `pollUntilDone` re-reads the socket and honours READING then DONE.
void testPollUntilDoneChangesSocket() {
    Socket firstReader = kInvalidSocket;
    Socket firstWriter = kInvalidSocket;
    Socket secondReader = kInvalidSocket;
    Socket secondWriter = kInvalidSocket;
    CP3_CHECK(makePair(firstReader, firstWriter));
    CP3_CHECK(makePair(secondReader, secondWriter));

    int checks = 0;
    int gets = 0;
    Observation observation;
    auto getSocket = [&] {
        ++gets;
        return static_cast<double>(gets == 1 ? firstReader : secondReader);
    };
    auto checkProgress = [&] {
        ++checks;
        return checks <= 2 ? POLL_READING : POLL_DONE;
    };

    const auto watchdog = currentLoop().addTimer(2000, [] { currentLoop().stop(); });
    auto waiter = observePoll(getSocket, checkProgress, observation);
    auto guard = waiter.then([watchdog] { currentLoop().cancelTimer(watchdog); });
    currentLoop().addTimer(50, [firstWriter] { (void)sendByte(firstWriter); });
    currentLoop().addTimer(100, [secondWriter] { (void)sendByte(secondWriter); });
    runEventLoop();
    currentLoop().cancelTimer(watchdog);

    CP3_CHECK(observation.resolved);
    CP3_CHECK_EQ(checks, 3);
    CP3_CHECK_EQ(gets, 2);
    closePair(firstReader, firstWriter);
    closePair(secondReader, secondWriter);
}

// 5. `POLL_WRITING` resolves on a connected socket, then DONE.
void testPollWritable() {
    Socket reader = kInvalidSocket;
    Socket writer = kInvalidSocket;
    CP3_CHECK(makePair(reader, writer));

    int checks = 0;
    int gets = 0;
    Observation observation;
    auto getSocket = [&] {
        ++gets;
        return static_cast<double>(reader);
    };
    auto checkProgress = [&] {
        ++checks;
        return checks == 1 ? POLL_WRITING : POLL_DONE;
    };

    const auto watchdog = currentLoop().addTimer(2000, [] { currentLoop().stop(); });
    auto waiter = observePoll(getSocket, checkProgress, observation);
    auto guard = waiter.then([watchdog] { currentLoop().cancelTimer(watchdog); });
    runEventLoop();
    currentLoop().cancelTimer(watchdog);

    CP3_CHECK(observation.resolved);
    CP3_CHECK_EQ(checks, 2);
    CP3_CHECK_EQ(gets, 1);
    closePair(reader, writer);
}

// 6. `POLL_FAILED` rejects, and an unknown state rejects.
void testPollFailures() {
    Observation failed;
    int failedChecks = 0;
    auto failedWait = observePoll(
        [] { return 0.0; },
        [&failedChecks] {
            ++failedChecks;
            return POLL_FAILED;
        },
        failed);
    runEventLoop();
    CP3_CHECK(failed.rejected);
    CP3_CHECK(failed.error.find("failed") != std::string::npos);

    Observation unknown;
    auto unknownWait = observePoll(
        [] { return 0.0; },
        [] { return 99.0; },
        unknown);
    runEventLoop();
    CP3_CHECK(unknown.rejected);
    CP3_CHECK(unknown.error.find("unknown") != std::string::npos);
}

// 7. Closing the descriptor while the wait is registered rejects the promise.
// Runs last: the watchdog calls EventLoop::stop() on failure, which is terminal.
void testCloseRejects() {
    Socket reader = kInvalidSocket;
    Socket writer = kInvalidSocket;
    CP3_CHECK(makePair(reader, writer));

    bool timedOut = false;
    Observation observation;
    const auto watchdog = currentLoop().addTimer(2000, [&timedOut] {
        timedOut = true;
        currentLoop().stop();
    });
    auto waiter = observeReadable(reader, observation);
    auto guard = waiter.then([watchdog] { currentLoop().cancelTimer(watchdog); });
    currentLoop().addTimer(100, [&reader, &writer] { closePair(reader, writer); });
    runEventLoop();
    currentLoop().cancelTimer(watchdog);

    CP3_CHECK(!timedOut);
    CP3_CHECK(observation.rejected);
    CP3_CHECK(!observation.resolved);
    closePair(reader, writer);
}

}  // namespace

int main() {
    ensureSockets();
    testTimerBeatsSocketWait();
    testThousandWaiters();
    testBothDirections();
    testPollUntilDoneChangesSocket();
    testPollWritable();
    testPollFailures();
    testCloseRejects();
    return cp3test::summary("io_test");
}
