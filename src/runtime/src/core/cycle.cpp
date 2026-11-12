// =============================================================================
// src/runtime/src/core/cycle.cpp - the trial-deletion cycle collector.
//
// The collector runs at the loop safe points (between macrotasks, where the
// microtask queue already drained). It only ever visits the suspects: the
// objects whose count dropped without reaching zero. A pass is bounded by a
// visited-node budget and a time budget, and it is resumable: a pass that
// runs out of budget continues where it stopped on the next call.
//
// The algorithm is Bacon-Rajan's trial deletion. The suspects and everything
// they reach form the island under test. The walk *trials* the internal edges
// away: every edge into an island object decrements its trial count. An
// object whose trial count would exceed its real count has an external
// reference, so the island is alive and every trial decrement is undone. An
// island where every object is fully internal is garbage and is freed.
//
// There is no whole-heap scan, no stop-the-world pause and no background
// thread: the collector is per-loop (thread-local) and single-threaded.
// =============================================================================
#include "cp3/core/cycle.hpp"

#include <chrono>

namespace cp3::runtime {

CycleCollector& CycleCollector::current() {
    thread_local CycleCollector collector;
    return collector;
}

void CycleCollector::suspect(CycleControl* control) {
    if (control->buffered) return;
    control->buffered = true;
    control->next = suspects_;
    suspects_ = control;
    ++suspectCount_;
}

void CycleCollector::markGray(CycleControl* control) { gray_.push_back(control); }

void CycleCollector::restore() {
    // The island is alive: undo the trial decrements and return the objects
    // to plain reference counting.
    for (CycleControl* x : touched_) x->trial = 0;
    for (CycleControl* x : gray_) {
        if (x->color == CycleColor::Gray) x->color = CycleColor::White;
    }
    touched_.clear();
    gray_.clear();
    grayIndex_ = 0;
    trialAbort_ = false;
}

std::size_t CycleCollector::collectGarbage() {
    // The island is garbage: the collector owns it. Two phases. First every
    // object is destroyed; each member's edges are released as the object's
    // storage goes, and a Black control's release is a no-op (its block is
    // owned by the collector). Only once every value is destroyed may the
    // blocks be released: deleting a member's block mid-destruction would
    // leave the other members reading a freed control.
    for (CycleControl* x : gray_) {
        x->color = CycleColor::Black;
    }
    std::size_t freed = 0;
    for (CycleControl* x : gray_) {
        x->destroyValue();
        ++freed;
        ++freed_;
    }
    for (CycleControl* x : gray_) {
        if (x->weak == 0) {
            delete x;
        } else {
            // A weak observer still holds the block: the block survives until
            // the observer is gone, and its reads answer `undefined`.
            x->expired = true;
        }
    }
    touched_.clear();
    gray_.clear();
    grayIndex_ = 0;
    return freed;
}

std::size_t CycleCollector::collect() {
    const auto start = std::chrono::steady_clock::now();
    // The budget is per tick, not per trial: a resumed trial gets a fresh
    // budget.
    visited_ = 0;
    if (!trialActive_) {
        // Take the suspects buffer as the roots of the trial.
        gray_.clear();
        touched_.clear();
        grayIndex_ = 0;
        trialAbort_ = false;
        CycleControl* s = suspects_;
        suspects_ = nullptr;
        suspectCount_ = 0;
        while (s != nullptr) {
            CycleControl* next = s->next;
            s->next = nullptr;
            s->buffered = false;
            if (s->expired) {
                // Plain reference counting already destroyed the object; the
                // block only needs releasing now.
                if (s->weak == 0) delete s;
                s = next;
                continue;
            }
            if (s->color == CycleColor::White ||
                s->color == CycleColor::Purple) {
                s->color = CycleColor::Gray;
                gray_.push_back(s);
            }
            s = next;
        }
        trialActive_ = true;
    }
    // MarkGray: walk the island, within the budget.
    while (grayIndex_ < gray_.size()) {
        if (visited_ >= maxVisited_) break;
        if (visited_ > 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto micros = std::chrono::duration_cast<
                std::chrono::microseconds>(now - start).count();
            if (micros >= static_cast<long long>(maxMicros_)) break;
        }
        CycleControl* x = gray_[grayIndex_++];
        ++visited_;
        x->visitChildren(*this);
        if (trialAbort_) break;
    }
    if (trialAbort_) {
        // An external reference: the island is alive.
        restore();
        trialActive_ = false;
        return 0;
    }
    if (grayIndex_ < gray_.size()) {
        // The budget ran out: the next pass resumes the trial where this one
        // stopped.
        return 0;
    }
    // The scan: an object that still has an external reference keeps the
    // island alive.
    for (CycleControl* x : gray_) {
        if (x->trial < x->owners) {
            restore();
            trialActive_ = false;
            return 0;
        }
    }
    const std::size_t freed = collectGarbage();
    trialActive_ = false;
    return freed;
}

std::size_t CycleCollector::drain() {
    std::size_t freed = 0;
    for (;;) {
        if (!trialActive_ && suspectCount_ == 0) break;
        freed += collect();
    }
    return freed;
}

}  // namespace cp3::runtime
