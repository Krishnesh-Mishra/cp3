// =============================================================================
// cp3/src/promise.cpp — unhandled-rejection tracking and `sleep`.
//
// The Promise machinery itself is header-only templates (cp3/promise.hpp); this
// file holds the pieces that must exist exactly once per program: the global
// unhandled-rejection registry that enforces DECISIONS D43.
// =============================================================================
#include "cp3/promise.hpp"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace cp3::runtime {
namespace detail {
namespace {

struct UnhandledEntry {
    // Held strongly on purpose: the entry must keep the flag alive even if the
    // promise state that produced it is destroyed before the loop drains (D43).
    std::shared_ptr<bool> flag;
    Thrown reason;
};

std::mutex& registryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<UnhandledEntry>& registry() {
    static std::vector<UnhandledEntry> entries;
    return entries;
}

// Called by `EventLoop::run()` when the loop has drained. Any rejection that is
// still unobserved at that point is fatal (DECISIONS D43).
void reportUnhandledRejections() {
    std::vector<Thrown> stillUnhandled;
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        std::vector<UnhandledEntry>& entries = registry();
        for (UnhandledEntry& entry : entries) {
            if (entry.flag && *entry.flag) {
                stillUnhandled.push_back(entry.reason);
            }
        }
        entries.clear();
    }
    if (!stillUnhandled.empty()) {
        fatalUnhandledRejection(stillUnhandled.front());
    }
}

}  // namespace

void trackUnhandled(const std::shared_ptr<bool>& flag, const Thrown& reason) {
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().push_back(UnhandledEntry{flag, reason});
    }
    installUnhandledRejectionHook(currentLoop());
}

void clearUnhandled(const std::shared_ptr<bool>& flag) {
    std::lock_guard<std::mutex> lock(registryMutex());
    std::vector<UnhandledEntry>& entries = registry();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const UnhandledEntry& entry) {
                                     return entry.flag == flag;
                                 }),
                  entries.end());
}

void installUnhandledRejectionHook(EventLoop& loop) {
    loop.setDrainHook(&reportUnhandledRejections);
}

}  // namespace detail

Promise<void> sleep(double milliseconds) {
    Promise<void> promise = Promise<void>::pending();
    const double clamped = milliseconds < 0 ? 0.0 : milliseconds;
    currentLoop().addTimer(static_cast<std::int64_t>(clamped), [promise]() mutable {
        promise.fulfill();
    });
    return promise;
}

}  // namespace cp3::runtime
