// =============================================================================
// cp3/src/parallel.cpp — the thread pool backing `parallel.*`.
//
// No third-party libraries (spec names TBB; this is the equivalent structured
// surface built on std::thread only).
// =============================================================================
#include "cp3/parallel.hpp"

#include <algorithm>
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
    const std::size_t threads = inPoolWorker ? 1 : ThreadPool::global().size();
    const std::size_t limit = threads == 0 ? 1 : threads;
    return static_cast<std::size_t>(std::min<std::uint64_t>(total, limit));
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
