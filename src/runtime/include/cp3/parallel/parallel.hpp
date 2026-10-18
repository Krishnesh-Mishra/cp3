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
//   * Results combine DETERMINISTICALLY: chunks are processed in index order and
//     combined in index order, so the result does not depend on scheduling.
//   * Backed by a plain thread pool (spec names TBB; no third-party libraries
//     are allowed here, so this is the equivalent structured surface).
// =============================================================================
#pragma once

#include <array>
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
    const std::uint64_t total = static_cast<std::uint64_t>(end - begin);
    const std::size_t chunks = detail::parallelChunkCount(total);
    detail::parallelChunks(chunks, [&](std::size_t c) {
        const auto lo = begin + static_cast<std::int64_t>(total * c / chunks);
        const auto hi = begin + static_cast<std::int64_t>(total * (c + 1) / chunks);
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
// combined left-to-right with `combine` on the calling thread. Deterministic.
// `init` must be an identity of `combine` (0 for `+`, 1 for `*`, ...), which is
// what `parallel.reduce(..., init, ...)` means in the spec.
// ---------------------------------------------------------------------------
template <class Acc, class F, class Combine>
Acc parallelReduce(std::int64_t begin, std::int64_t end, Acc init, F body, Combine combine) {
    if (end <= begin) {
        return init;
    }
    const std::uint64_t total = static_cast<std::uint64_t>(end - begin);
    const std::size_t chunks = detail::parallelChunkCount(total);
    if (chunks <= 1) {
        return parallelReduce<Acc, F>(begin, end, std::move(init), std::move(body));
    }
    std::vector<Acc> partials;
    partials.reserve(chunks);
    std::vector<Acc> slots(chunks, init);  // one private accumulator per chunk
    detail::parallelChunks(chunks, [&](std::size_t c) {
        const auto lo = begin + static_cast<std::int64_t>(total * c / chunks);
        const auto hi = begin + static_cast<std::int64_t>(total * (c + 1) / chunks);
        Acc acc = init;
        for (std::int64_t i = lo; i < hi; ++i) {
            acc = body(acc, i);
        }
        slots[c] = std::move(acc);
    });
    Acc acc = std::move(slots[0]);
    for (std::size_t c = 1; c < chunks; ++c) {
        acc = combine(std::move(acc), std::move(slots[c]));
    }
    return acc;
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
