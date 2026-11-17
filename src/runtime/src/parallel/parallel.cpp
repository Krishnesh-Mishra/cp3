// =============================================================================
// cp3/src/parallel/parallel.cpp - the scheduler backing `parallel.*`.
//
// OWNERSHIP: runtime module. Part of the generated-code ABI.
//
// parallel.* schedules its numeric chunks through the vendored oneTBB
// (vendor/third_party/oneTBB, v2023.1.0, static libtbb.a) since the pool
// retirement. The public surface (cp3/parallel/parallel.hpp) is unchanged and
// TBB-free; this translation unit is the only one that references oneTBB, so
// the generated code sees the same headers as before.
//
// The worker/channel machinery (cp3/parallel/worker*.hpp) does NOT use this
// scheduler and is untouched: message passing keeps its FIFO order and its
// drops-on-terminate semantics on the event loop.
//
// Error model (identical to the previous pool): a C++ exception escaping a
// parallel body never survives the region. oneTBB captures it inside the task
// boundary, stops calling sibling chunks (internal cancellation of the group),
// and rethrows it on the thread that waits - `parallel.*` then terminates the
// process with it (std::terminate). That is what an uncaught exception on one
// of the pool's worker threads did, so a `throw` inside a parallel closure
// still ends the program, the first error still wins, and the remaining
// chunks still stop. A parallel call made from inside a parallel closure
// never reaches the scheduler at all (see `inPoolWorker`), so no region can
// deadlock on a busy scheduler.
//
// Nested regions keep the pool's serial-inline rule: inside oneTBB tasks,
// `inPoolWorker` is set for the task's duration (the pool held it set for the
// worker's whole lifetime; here it is scoped per task, which is strictly
// tighter and cannot leak onto a scheduler thread that later runs unrelated
// work). parallelChunkCount therefore collapses to one inline chunk for the
// nested call, exactly as it did on the pool. No explicit this_task_arena is
// required: the nested path never touches TBB, and oneTBB's own work stealing
// would handle arbitrary nesting without deadlock anyway.
// =============================================================================
#include "cp3/parallel/parallel.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <vector>

#include <tbb/blocked_range.h>
#include <tbb/info.h>
#include <tbb/parallel_for.h>

namespace cp3::runtime {
namespace {

// True while executing inside a `parallel.*` task. Scoped per task (a TBB
// worker thread serves many tasks, so the flag is set on entry and restored
// on exit, even when the task unwinds). Used to keep nested `parallel.*`
// inline, matching the pool's inPoolWorker flag.
thread_local bool inPoolWorker = false;

class InPoolWorkerScope {
public:
    InPoolWorkerScope() noexcept {
        previous_ = inPoolWorker;
        inPoolWorker = true;
    }
    ~InPoolWorkerScope() { inPoolWorker = previous_; }

    InPoolWorkerScope(const InPoolWorkerScope&) = delete;
    InPoolWorkerScope& operator=(const InPoolWorkerScope&) = delete;

private:
    bool previous_ = false;
};

// The scheduler's notion of available parallelism. `tbb::info::default_concurrency`
// is the size TBB would give a default arena; -1 on failure, which maps to 1
// (inline execution). Resolved once on first use and frozen, like the pool
// size was; -1 (or an environment where TBB cannot see its topology) means
// "run inline", never "fail".
std::size_t schedulerConcurrency() {
    static const std::size_t cached = [] {
        const int concurrency = tbb::info::default_concurrency();
        return concurrency <= 0 ? std::size_t{1} : static_cast<std::size_t>(concurrency);
    }();
    return cached;
}

}  // namespace

namespace detail {

std::size_t parallelChunkCount(std::uint64_t total) {
    if (total == 0) {
        return 1;
    }
    // A nested call is already on a scheduler task the outer region is
    // waiting for. Running one inline chunk keeps the nested work off the
    // scheduler and avoids the guarded chunk/combine work that cannot be
    // parallelized here.
    if (inPoolWorker) {
        return 1;
    }
    const std::size_t threads = schedulerConcurrency();
    const std::size_t limit = threads == 0 ? 1 : threads;
    // Four chunks per worker: the machine's logical processors are not equal
    // (6 P-cores + 8 E-cores on the reference laptop), so a contiguous split
    // with one chunk per worker is paced by the slowest core. Oversubscribing
    // lets an idle scheduler thread take the next queued chunk; the ordered
    // combine keeps the result identical. Measured on compute-multi: 20 chunks
    // 42.5 ms, 40 -> 33.6, 80 -> 32.3, 160 -> 31.2, cpp 33.0.
    const std::size_t chunks =
        limit <= std::numeric_limits<std::size_t>::max() / 4 ? limit * 4
                                                              : limit;
    const std::uint64_t cap = static_cast<std::uint64_t>(chunks);
    return static_cast<std::size_t>(total < cap ? total : cap);
}

std::int64_t parallelChunkBoundary(std::int64_t begin, std::uint64_t total,
                                   std::size_t chunk, std::size_t chunks) {
    if (chunks == 0) {
        return begin;
    }
    // Balanced split: q elements per chunk, and the first r chunks get one
    // extra. Every intermediate value is bounded by `total`, so the old
    // `total * chunk` product (which could overflow uint64/2^64) is gone.
    const std::uint64_t q = total / chunks;
    const std::uint64_t r = total % chunks;
    const std::uint64_t c = static_cast<std::uint64_t>(chunk);
    const std::uint64_t extra = c < r ? c : r;
    const std::uint64_t offset = q * c + extra;
    // The unsigned addition wraps modulo 2^64; the C++20 conversion back to
    // int64 is the same two's-complement value, so a range that starts
    // negative or crosses zero keeps exact boundaries.
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(begin) + offset);
}

void parallelChunks(std::size_t chunkCount,
                    const std::function<void(std::size_t)>& chunk) {
    if (chunkCount == 0) {
        return;
    }
    if (chunkCount == 1 || inPoolWorker || schedulerConcurrency() <= 1) {
        // Inline: the nested-region and single-worker path. Exceptions
        // propagate to the caller exactly as the previous pool's inline path
        // let them propagate on the calling thread.
        for (std::size_t c = 0; c < chunkCount; ++c) {
            chunk(c);
        }
        return;
    }
    try {
        tbb::parallel_for(
            tbb::blocked_range<std::size_t>(0, chunkCount),
            [&chunk](const tbb::blocked_range<std::size_t>& range) {
                const InPoolWorkerScope scope;
                for (std::size_t c = range.begin(); c != range.end(); ++c) {
                    chunk(c);
                }
            });
    } catch (...) {
        // An exception escaped a chunk. oneTBB captured it, cancelled the
        // region's remaining work and rethrew it on this thread. The previous
        // pool carried it off a worker thread, where no handler existed and
        // the process terminated; terminating here keeps the same visible
        // behaviour, and the first error still wins.
        std::terminate();
    }
}

void parallelRunAll(const std::vector<std::function<void()>>& tasks) {
    if (tasks.empty()) {
        return;
    }
    parallelChunks(tasks.size(),
                   [&tasks](std::size_t c) { tasks[c](); });
}

}  // namespace detail

}  // namespace cp3::runtime
