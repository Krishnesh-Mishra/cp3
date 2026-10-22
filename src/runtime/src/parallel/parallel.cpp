// =============================================================================
// cp3/src/parallel/parallel.cpp - the thread pool backing `parallel.*`.
//
// No third-party libraries (spec names TBB; this is the equivalent structured
// surface built on std::thread only).
// =============================================================================
#include "cp3/parallel/parallel.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <thread>

namespace cp3::runtime {
namespace {

// True while executing on a pool worker thread. Used to avoid a self-deadlock
// when `parallel.*` is nested inside another `parallel.*` closure: the nested
// call runs inline instead of queueing behind the busy pool.
thread_local bool inPoolWorker = false;

std::size_t& globalPoolSizeOverride() {
    static std::size_t size = 0;
    return size;
}

}  // namespace

void ThreadPool::setGlobalSize(std::size_t threadCount) {
    globalPoolSizeOverride() = threadCount;
}

ThreadPool& ThreadPool::global() {
    static ThreadPool pool(globalPoolSizeOverride() != 0
                               ? globalPoolSizeOverride()
                               : std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    return pool;
}

ThreadPool::ThreadPool(std::size_t threadCount)
    : threadCount_(threadCount == 0 ? 1 : threadCount) {
    threads_.reserve(threadCount_);
    for (std::size_t i = 0; i < threadCount_; ++i) {
        threads_.emplace_back([this]() { workerMain(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    workCv_.notify_all();
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    workCv_.notify_one();
}

void ThreadPool::workerMain() {
    inPoolWorker = true;
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            workCv_.wait(lock, [this]() { return stopping_ || nextTask_ < tasks_.size(); });
            if (nextTask_ >= tasks_.size()) {
                if (stopping_) {
                    return;
                }
                continue;
            }
            task = std::move(tasks_[nextTask_]);
            ++nextTask_;
            ++active_;
        }
        task();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
            if (nextTask_ >= tasks_.size()) {
                tasks_.clear();
                nextTask_ = 0;
                if (active_ == 0) {
                    idleCv_.notify_all();
                }
            }
        }
    }
}

void ThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idleCv_.wait(lock, [this]() { return nextTask_ >= tasks_.size() && active_ == 0; });
}

namespace detail {

std::size_t parallelChunkCount(std::uint64_t total) {
    if (total == 0) {
        return 1;
    }
    // A nested call is already on a worker the outer pool is waiting for.
    // Running one inline chunk keeps the nested work off the pool queue and
    // avoids the guarded chunk/combine work that cannot be parallelized here.
    if (inPoolWorker) {
        return 1;
    }
    const std::size_t threads = ThreadPool::global().size();
    const std::size_t limit = threads == 0 ? 1 : threads;
    // Four chunks per worker: the machine's logical processors are not equal
    // (6 P-cores + 8 E-cores on the reference laptop), so a contiguous split
    // with one chunk per worker is paced by the slowest core. Oversubscribing
    // lets an idle worker take the next queued chunk; the ordered combine
    // keeps the result identical. Measured on compute-multi: 20 chunks 42.5 ms,
    // 40 -> 33.6, 80 -> 32.3, 160 -> 31.2, cpp 33.0.
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

void parallelRunAll(const std::vector<std::function<void()>>& tasks) {
    if (tasks.empty()) {
        return;
    }
    ThreadPool& pool = ThreadPool::global();
    if (inPoolWorker || pool.size() <= 1 || tasks.size() == 1) {
        for (const std::function<void()>& task : tasks) {
            task();
        }
        return;
    }
    for (const std::function<void()>& task : tasks) {
        pool.enqueue(task);
    }
    pool.waitIdle();
}

void parallelChunks(std::size_t chunkCount, const std::function<void(std::size_t)>& chunk) {
    if (chunkCount == 0) {
        return;
    }
    std::vector<std::function<void()>> tasks;
    tasks.reserve(chunkCount);
    for (std::size_t c = 0; c < chunkCount; ++c) {
        tasks.push_back([&chunk, c]() { chunk(c); });
    }
    parallelRunAll(tasks);
}

}  // namespace detail

}  // namespace cp3::runtime
