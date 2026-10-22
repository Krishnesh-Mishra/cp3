// =============================================================================
// cp3/parallel/parallel.hpp - runtime support for `parallel.for` / `parallel.reduce` /
// `parallel.run` (docs/language-spec/13-concurrency-...).
//
// OWNERSHIP: runtime module. Part of the generated-code ABI.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   cp3 `parallel.for(0..n, (i) => body)`
//        -> `::cp3::runtime::parallelFor(0, n, [](std::int64_t i) { body })`
//   cp3 `parallel.reduce(0..n, init, (acc, i) => acc)`
//        -> with a synthesized combine op:
//           `::cp3::runtime::parallelReduce(0, n, init, body, [](Acc a, Acc b){...})`
//        -> without one (sequential, still correct):
//           `::cp3::runtime::parallelReduce(0, n, init, body)`
//   cp3 `parallel.run(f1, f2, f3)`
//        -> `::cp3::runtime::parallelRun(f1, f2, f3)`  (`std::array<R, 3>`)
//
// -----------------------------------------------------------------------------
// SEMANTICS (spec §13)
// -----------------------------------------------------------------------------
//   * `parallel.*` is SYNCHRONOUS: these functions block until every closure has
//     finished, which is exactly why captured values may be borrowed.
//   * Closures capture raw, NON-OWNING borrows (spec §13 Rule 1): the runtime
//     never takes a reference-counted handle of anything a closure captured.
//     Generated lambdas capture by reference/pointer; the synchronous contract
//     makes that safe.
//   * The one allowed write is through `parallel.for`'s own loop index
//     (disjoint by construction) - a codegen/sema rule, invisible here.
//   * Results combine DETERMINISTICALLY: each chunk writes its own slot, and
//     the slots are combined in chunk-index order on the calling thread, so
//     the result does not depend on the unordered task schedule.
//   * Backed by a plain thread pool (spec names TBB; no third-party libraries
//     are allowed here, so this is the equivalent structured surface).
// =============================================================================
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/core/runtime.hpp"

namespace cp3::runtime {

// ---------------------------------------------------------------------------
// The thread pool backing parallel.*
// ---------------------------------------------------------------------------
class ThreadPool {
public:
    // The process-wide pool. Created on first use with
    // `std::thread::hardware_concurrency()` threads (at least 1).
    static ThreadPool& global();

    explicit ThreadPool(std::size_t threadCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t size() const noexcept { return threadCount_; }

    // Queue work. Safe to call from any thread.
    void enqueue(std::function<void()> task);

    // Block until no queued or running work remains.
    void waitIdle();

    // Override the pool size for `global()` before first use (tests / env).
    static void setGlobalSize(std::size_t threadCount);

private:
    void workerMain();

    std::size_t threadCount_ = 1;
    std::vector<std::thread> threads_;
    std::vector<std::function<void()>> tasks_;  // FIFO
    std::size_t nextTask_ = 0;
    std::size_t active_ = 0;
    bool stopping_ = false;
    std::mutex mutex_;
    std::condition_variable workCv_;
    std::condition_variable idleCv_;
};

namespace detail {

// Number of chunks to split a range of `total` elements into (pool size capped
// by the element count; at least 1).
std::size_t parallelChunkCount(std::uint64_t total);

// The first element of chunk `chunk` for a range of `total` elements starting
// at `begin`. The split is balanced (the first `total % chunks` chunks get one
// extra element) and never forms the overflowing `total * chunk` product the
// old floor split used.
std::int64_t parallelChunkBoundary(std::int64_t begin, std::uint64_t total,
                                   std::size_t chunk, std::size_t chunks);

// True while an accumulator is still an exact integer strictly below 2^53.
// At the boundary the next integer is unrepresentable, so the chunked path
// abandons its work and reruns sequentially. The chunked overload is only
// emitted for bodies codegen proved pure, so re-running is observationally
// identical to the single-threaded loop.
template <class T>
bool parallelExactAccumulator(T value) {
    static_assert(std::is_floating_point_v<T>,
                  "the chunked reduce guard is defined for floating accumulators");
    constexpr double kExactIntegerLimit = 9007199254740992.0;  // 2^53
    // The magnitude test also rejects NaN and infinities. Inside the bound the
    // value fits int64, so the round trip proves it is integral.
    if (!(std::fabs(value) < kExactIntegerLimit)) return false;
    return static_cast<double>(static_cast<std::int64_t>(value)) == value;
}

// A checked exact integer (parallelExactAccumulator passed) as int64.
inline std::int64_t parallelExactInt(double value) noexcept {
    return static_cast<std::int64_t>(value);
}

inline std::int64_t parallelExactAbsInt(double value) noexcept {
    const std::int64_t v = parallelExactInt(value);
    return v < 0 ? -v : v;
}

// The addition-fold proof. `total` is the exact ordered-combine prefix, and
// `lo`/`hi` are the chunk-local accumulator extrema. The sequential fold at
// this chunk visits `total + prefix` for every local prefix, so both extrema
// must stay strictly inside 2^53; then every sequential addition is exact and
// the chunked result equals the left-to-right double loop bit for bit.
// Two exact integers below 2^53 sum below 2^54, so int64 cannot overflow.
inline bool parallelAddPrefixFits(double total, double lo, double hi) noexcept {
    constexpr std::int64_t kExactIntegerLimit = 9007199254740992LL;  // 2^53
    const std::int64_t t = parallelExactInt(total);
    const std::int64_t l = parallelExactInt(lo);
    const std::int64_t h = parallelExactInt(hi);
    return t + l > -kExactIntegerLimit && t + l < kExactIntegerLimit &&
           t + h > -kExactIntegerLimit && t + h < kExactIntegerLimit;
}

// The multiplication-fold proof: if the ordered prefix magnitude times the
// largest chunk-local magnitude stays below 2^53, every sequential product is
// exact. Division avoids the overflowing intermediate product.
inline bool parallelMultiplyPrefixFits(std::int64_t totalAbs,
                                       std::int64_t chunkAbs) noexcept {
    constexpr std::int64_t kExactIntegerLimit = 9007199254740992LL;  // 2^53
    if (chunkAbs == 0) return true;
    return totalAbs <= (kExactIntegerLimit - 1) / chunkAbs;
}

// Run `chunk(c)` for every c in [0, chunkCount) on the pool, then block until
// all of them have finished. `chunk` is borrowed for the duration of the call.
void parallelChunks(std::size_t chunkCount, const std::function<void(std::size_t)>& chunk);

// Enqueue every task and block until all have run.
void parallelRunAll(const std::vector<std::function<void()>>& tasks);

// Build and run one task per callable, storing each result in `results`.
template <class R, class Tuple, std::size_t... I>
void parallelInvokeAll(std::array<R, sizeof...(I)>& results, Tuple& tuple,
                       std::index_sequence<I...>) {
    std::vector<std::function<void()>> tasks{
        [&results, &tuple]() {
            if constexpr (std::is_void_v<R>) {
                std::get<I>(tuple)();
            } else {
                results[I] = std::get<I>(tuple)();
            }
        }...};
    parallelRunAll(tasks);
}

}  // namespace detail

// ---------------------------------------------------------------------------
// parallel.for(range, body)  - `body(i)`
// ---------------------------------------------------------------------------
template <class F>
void parallelFor(std::int64_t begin, std::int64_t end, F body) {
    if (end <= begin) {
        return;
    }
    const std::uint64_t total =
        static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
    const std::size_t chunks = detail::parallelChunkCount(total);
    detail::parallelChunks(chunks, [&](std::size_t c) {
        const std::int64_t lo =
            detail::parallelChunkBoundary(begin, total, c, chunks);
        const std::int64_t hi =
            detail::parallelChunkBoundary(begin, total, c + 1, chunks);
        for (std::int64_t i = lo; i < hi; ++i) {
            body(i);
        }
    });
}

// ---------------------------------------------------------------------------
// parallel.reduce(range, init, body)  - exact sequential semantics.
//
// This is the safe fallback when codegen cannot synthesize a combine op: the
// result is exactly what a plain left-to-right loop would produce. Still
// deterministic, just not parallel.
// ---------------------------------------------------------------------------
template <class Acc, class F>
Acc parallelReduce(std::int64_t begin, std::int64_t end, Acc init, F body) {
    Acc acc = std::move(init);
    for (std::int64_t i = begin; i < end; ++i) {
        acc = body(acc, i);
    }
    return acc;
}

// ---------------------------------------------------------------------------
// parallel.reduce(range, init, body, combine)  - chunked + ordered combine.
//
// Each chunk folds `body` privately from `init`; chunk results are then
// combined left-to-right with `combine` on the calling thread. The task
// schedule is unordered, so codegen only passes this form when the body is
// proven pure and integer exact (integer addition/multiplication) or an exact
// associative max/min shape. Deterministic by construction.
//
// Exactness at the 2^53 boundary is enforced at run time: if any chunk
// accumulator or combined partial is non-finite, fractional, or not strictly
// below 2^53, every chunk's work is discarded and the call reruns through the
// sequential overload. Purity is what makes that rerun unobservable.
//
// The guard also proves the *sequential* prefix order, which differs from the
// chunk order: a chunk starts from `init` while the sequential fold enters it
// with the running total. For `+` the chunk-local accumulator extrema are
// checked offset by that total; for `*` the prefix magnitude is checked against
// the chunk's largest local magnitude. Either check failing reruns sequentially.
// Without it, large positive values in one chunk and a cancellation in the
// next can make the chunked sum exact while the sequential double fold rounded.
// ---------------------------------------------------------------------------
enum class ParallelCombineKind { Add, Multiply, Max };

namespace detail {

// Per-chunk evidence for the prefix guard. `slots` holds each chunk's folded
// accumulator; the other vectors hold the local extrema/magnitudes the runtime
// needs to prove the sequential prefix order exact. Only the fields for `Kind`
// are allocated.
template <class Acc>
struct ParallelChunkProofs {
    std::vector<Acc> slots;
    std::vector<Acc> low;
    std::vector<Acc> high;
    std::vector<std::int64_t> magnitude;
    std::atomic<bool> exact{true};
};

// Fold every chunk privately from `init`, recording the exactness evidence.
// On any non-exact local accumulator the whole chunked attempt is abandoned
// (the caller reruns the sequential overlay, which is observationally
// identical because codegen proved the body pure).
template <ParallelCombineKind Kind, class Acc, class F>
void parallelChunkedPass(std::int64_t begin, std::int64_t end, const Acc& init,
                         F& body, ParallelChunkProofs<Acc>& proofs) {
    const std::uint64_t total =
        static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
    const std::size_t chunks = proofs.slots.size();
    parallelChunks(chunks, [&](std::size_t c) {
        const std::int64_t lo = parallelChunkBoundary(begin, total, c, chunks);
        const std::int64_t hi =
            parallelChunkBoundary(begin, total, c + 1, chunks);
        Acc acc = init;
        Acc low = init;
        Acc high = init;
        std::int64_t maxAbs = parallelExactAbsInt(init);
        for (std::int64_t i = lo; i < hi; ++i) {
            acc = body(acc, i);
            if (!parallelExactAccumulator(acc)) {
                proofs.exact.store(false, std::memory_order_relaxed);
                break;
            }
            if constexpr (Kind == ParallelCombineKind::Add) {
                if (acc < low) low = acc;
                if (acc > high) high = acc;
            } else if constexpr (Kind == ParallelCombineKind::Multiply) {
                const std::int64_t magnitude = parallelExactAbsInt(acc);
                if (magnitude > maxAbs) maxAbs = magnitude;
            }
        }
        proofs.slots[c] = std::move(acc);
        if constexpr (Kind == ParallelCombineKind::Add) {
            proofs.low[c] = low;
            proofs.high[c] = high;
        } else if constexpr (Kind == ParallelCombineKind::Multiply) {
            proofs.magnitude[c] = maxAbs;
        }
    });
}

// True when the sequential fold at chunk `c` is exact for every intermediate
// value in that chunk. Max/min are idempotent and need no magnitude proof.
template <ParallelCombineKind Kind, class Acc>
bool parallelPrefixFits(const Acc& acc, std::size_t chunk,
                        const ParallelChunkProofs<Acc>& proofs) {
    if constexpr (Kind == ParallelCombineKind::Add) {
        return parallelAddPrefixFits(acc, proofs.low[chunk],
                                     proofs.high[chunk]);
    } else if constexpr (Kind == ParallelCombineKind::Multiply) {
        return parallelMultiplyPrefixFits(parallelExactAbsInt(acc),
                                          proofs.magnitude[chunk]);
    } else {
        return true;
    }
}

}  // namespace detail

// The 5-argument form has no proven combine kind, so it fails closed to the
// exact sequential loop. Codegen always passes the kind template argument.
template <class Acc, class F, class Combine>
Acc parallelReduce(std::int64_t begin, std::int64_t end, Acc init, F body,
                   Combine combine) {
    (void)combine;
    return parallelReduce<Acc, F>(begin, end, std::move(init), std::move(body));
}

template <ParallelCombineKind Kind, class Acc, class F, class Combine>
Acc parallelReduce(std::int64_t begin, std::int64_t end, Acc init, F body,
                   Combine combine) {
    if constexpr (!std::is_same_v<std::remove_cv_t<Acc>, double>) {
        // The exactness guard is defined for cp3's double number type; any
        // other accumulator keeps the sequential semantics rather than risking
        // native integer overflow or a narrower float format.
        (void)combine;
        return parallelReduce<Acc, F>(begin, end, std::move(init),
                                      std::move(body));
    } else {
        if constexpr (Kind == ParallelCombineKind::Add) {
            if (init != 0) {
                return parallelReduce<Acc, F>(begin, end, std::move(init),
                                              std::move(body));
            }
        } else if constexpr (Kind == ParallelCombineKind::Multiply) {
            if (init != 1) {
                return parallelReduce<Acc, F>(begin, end, std::move(init),
                                              std::move(body));
            }
        }
        if (end <= begin) return init;
        if (!detail::parallelExactAccumulator(init)) {
            return parallelReduce<Acc, F>(begin, end, std::move(init),
                                          std::move(body));
        }
        const std::uint64_t total =
            static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
        const std::size_t chunks = detail::parallelChunkCount(total);
        if (chunks <= 1) {
            return parallelReduce<Acc, F>(begin, end, std::move(init),
                                          std::move(body));
        }
        detail::ParallelChunkProofs<Acc> proofs;
        proofs.slots.assign(chunks, init);
        if constexpr (Kind == ParallelCombineKind::Add) {
            proofs.low.assign(chunks, init);
            proofs.high.assign(chunks, init);
        } else if constexpr (Kind == ParallelCombineKind::Multiply) {
            proofs.magnitude.assign(chunks,
                                    detail::parallelExactAbsInt(init));
        }
        detail::parallelChunkedPass<Kind>(begin, end, init, body, proofs);
        if (!proofs.exact.load(std::memory_order_relaxed)) {
            return parallelReduce<Acc, F>(begin, end, std::move(init),
                                          std::move(body));
        }
        Acc acc = init;
        for (std::size_t c = 0; c < chunks; ++c) {
            if (!detail::parallelPrefixFits<Kind>(acc, c, proofs)) {
                return parallelReduce<Acc, F>(begin, end, std::move(init),
                                              std::move(body));
            }
            acc = combine(std::move(acc), std::move(proofs.slots[c]));
            if (!detail::parallelExactAccumulator(acc)) {
                return parallelReduce<Acc, F>(begin, end, std::move(init),
                                              std::move(body));
            }
        }
        return acc;
    }
}

// ---------------------------------------------------------------------------
// parallel.run(f1, f2, ...)  - results in argument order.
// ---------------------------------------------------------------------------
template <class... Fs>
auto parallelRun(Fs&&... fs) {
    using R = std::common_type_t<std::invoke_result_t<std::remove_cvref_t<Fs>>...>;
    constexpr std::size_t N = sizeof...(Fs);
    std::array<R, N> results{};
    auto tuple = std::forward_as_tuple(std::forward<Fs>(fs)...);
    detail::parallelInvokeAll<R>(results, tuple, std::make_index_sequence<N>{});
    return results;
}

}  // namespace cp3::runtime
