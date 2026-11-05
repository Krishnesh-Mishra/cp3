// concurrentqueue_smoke.cpp -- smoke for vendored moodycamel
// ConcurrentQueue (third_party/moodycamel, header-only).
//
// Basic, deterministic behaviour the cp3 Channel / async log sink will rely
// on: empty-queue try_dequeue fails, enqueue/try_dequeue round-trips every
// element exactly once, and a 4-producer/4-consumer run (std::thread)
// delivers the full expected multiset. Ordering across producers is not
// asserted (unspecified); completeness (count + sum + no duplicates) is.
//
// Build (see run_smokes.ps1 / docs/design/vendor-smoke.md) -- header-only,
// no archive to link; -pthread for the std::thread section:
//   g++ -std=c++20 -O2 -pthread -I third_party/moodycamel/include \
//       third_party/smokes/concurrentqueue_smoke.cpp -o concurrentqueue_smoke.exe
//
// Exit 0 on success, 1 on any mismatch. No input, no files, deterministic
// verdict (thread interleaving varies, the checked totals do not).

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "concurrentqueue.h"

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}  // namespace

int main() {
    std::printf("concurrentqueue smoke (ConcurrentQueue<int>)\n");

    moodycamel::ConcurrentQueue<int> q;

    int out = -1;
    check(!q.try_dequeue(out), "try_dequeue on empty queue fails");

    constexpr int kN = 1000;
    for (int i = 0; i < kN; ++i) q.enqueue(i);

    std::vector<int> seen;
    seen.reserve(kN);
    while (q.try_dequeue(out)) seen.push_back(out);
    check(seen.size() == static_cast<std::size_t>(kN), "all enqueued items dequeued");
    std::sort(seen.begin(), seen.end());
    bool complete = seen.size() == static_cast<std::size_t>(kN);
    for (int i = 0; i < kN && complete; ++i) complete = seen[i] == i;
    check(complete, "dequeued multiset is exactly 0..999");
    check(!q.try_dequeue(out), "queue empty after drain");

    // MPMC section: 4 producers x 250 items, 4 consumers, exact totals.
    constexpr int kProducers = 4, kPerProducer = 250;
    constexpr int kTotal = kProducers * kPerProducer;
    moodycamel::ConcurrentQueue<int> mq;
    std::atomic<int> consumed{0};
    std::atomic<long long> sum{0};

    std::vector<std::thread> consumers;
    for (int c = 0; c < 4; ++c) {
        consumers.emplace_back([&] {
            int v;
            while (consumed.load() < kTotal) {
                if (mq.try_dequeue(v)) {
                    sum.fetch_add(v);
                    consumed.fetch_add(1);
                }
            }
        });
    }
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) mq.enqueue(p * kPerProducer + i);
        });
    }
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    const long long expected = static_cast<long long>(kTotal - 1) * kTotal / 2;
    check(consumed.load() == kTotal, "MPMC: all items consumed");
    check(sum.load() == expected, "MPMC: sum matches 0..999");

    std::printf(failures ? "concurrentqueue smoke: %d failure(s)\n"
                         : "concurrentqueue smoke: OK\n",
                failures);
    return failures ? 1 : 0;
}
