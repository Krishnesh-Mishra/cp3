// =============================================================================
// src/runtime/tests/cycle_test.cpp - the automatic cycle collector driver.
//
// The tests pin the reclamation contract:
//
//   * an unreachable island is freed,
//   * a reachable island is not freed,
//   * a weak link reads `undefined` after the collection,
//   * a budgeted collection resumes where it stopped.
// =============================================================================
#include "harness.hpp"

#include "cp3/core/cycle.hpp"

namespace {

using cp3::runtime::CycleCollector;
using cp3::runtime::CycleObject;
using cp3::runtime::CycleRef;
using cp3::runtime::CycleWeakRef;

// The live-object counter the tests read.
int liveNodes = 0;

// A node with one strong edge: the shape the collector walks.
class TestNode : public CycleObject {
public:
    CycleRef<TestNode> next;
    int value = 0;

    explicit TestNode(int v) : value(v) { ++liveNodes; }
    ~TestNode() override { --liveNodes; }

    void cp3_cycle_visit(CycleCollector& collector) override {
        collector.visitEdge(next);
    }
};

}  // namespace

int main() {
    // --- an unreachable island is freed ---------------------------------------
    {
        CycleRef<TestNode> a = CycleRef<TestNode>::make(1, 1);
        CycleRef<TestNode> b = CycleRef<TestNode>::make(1, 2);
        a->next = b;
        b->next = a;
        CP3_CHECK_EQ(liveNodes, 2);
        // The island now only reaches itself: the counts never reach zero.
        a.reset();
        b.reset();
        CP3_CHECK_EQ(liveNodes, 2);
        CycleCollector::current().drain();
        CP3_CHECK_EQ(liveNodes, 0);
    }

    // --- a reachable island is not freed --------------------------------------
    {
        CycleRef<TestNode> a = CycleRef<TestNode>::make(1, 1);
        CycleRef<TestNode> b = CycleRef<TestNode>::make(1, 2);
        a->next = b;
        b->next = a;
        CycleRef<TestNode> keep = a;  // an outside reference
        a.reset();
        b.reset();
        CycleCollector::current().drain();
        // The island is alive: `keep` still holds it.
        CP3_CHECK_EQ(liveNodes, 2);
        keep.reset();
        CycleCollector::current().drain();
        CP3_CHECK_EQ(liveNodes, 0);
    }

    // --- a weak link reads undefined after the collection ---------------------
    {
        CycleWeakRef<TestNode> weak;
        {
            CycleRef<TestNode> a = CycleRef<TestNode>::make(1, 1);
            CycleRef<TestNode> b = CycleRef<TestNode>::make(1, 2);
            weak = CycleWeakRef<TestNode>(b);
            a->next = b;
            b->next = a;
            a.reset();
            b.reset();
            CycleCollector::current().drain();
            CP3_CHECK_EQ(liveNodes, 0);
            // The target is gone: the weak link reads undefined.
            CP3_CHECK(weak.lock() == nullptr);
        }
    }

    // --- a budgeted collection resumes ----------------------------------------
    {
        CycleCollector& collector = CycleCollector::current();
        collector.setBudget(2, 1000000);  // two nodes per pass
        {
            CycleRef<TestNode> nodes[8];
            for (int i = 0; i < 8; ++i) {
                nodes[i] = CycleRef<TestNode>::make(1, i);
            }
            for (int i = 0; i < 8; ++i) {
                nodes[i]->next = nodes[(i + 1) % 8];
            }
            for (int i = 0; i < 8; ++i) {
                nodes[i].reset();
            }
            CP3_CHECK_EQ(liveNodes, 8);
            // The budget cuts every pass short; the passes resume until the
            // island is reclaimed.
            std::size_t freed = 0;
            for (int pass = 0; pass < 32 && freed == 0; ++pass) {
                freed = collector.collect();
            }
            CP3_CHECK(freed > 0);
            CP3_CHECK_EQ(liveNodes, 0);
        }
        collector.setBudget(CycleCollector::kMaxVisitedPerTick,
                            CycleCollector::kMaxMicrosPerTick);
    }

    return cp3test::summary("cycle_test");
}
