// =============================================================================
// src/runtime/include/cp3/core/cycle.hpp - automatic cycle reclamation.
//
// A cycle-capable value is a reference whose target can hold a reference back
// to it. Plain reference counting cannot reclaim such an island: the objects
// keep each other alive and their counts never reach zero.
//
// This runtime reclaims them with Bacon-Rajan's trial deletion. An object
// whose count drops but does not reach zero becomes a suspect. At a loop safe
// point the collector walks the suspects' reachable set and *trials* the
// internal edges away: an object whose external count (its real count minus
// the trial decrements) is still positive is alive, and the whole island is
// restored; an island with no external reference left is garbage and is
// freed.
//
// This is NOT a tracing collector. There is no whole-heap scan, no
// stop-the-world pause and no background thread: only the suspects are
// visited, only at loop safe points, and only within a per-tick budget.
//
// Acyclic values keep plain reference counting: their control block, their
// layout and their generated code are unchanged. Only a type the compiler
// classified as cycle-capable uses `CycleRef`.
// =============================================================================
#ifndef CP3_CORE_CYCLE_HPP
#define CP3_CORE_CYCLE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cp3::runtime {

// The color of a cycle-capable object in the trial-deletion algorithm.
enum class CycleColor : std::uint8_t {
    White = 0,   // not a suspect: plain reference counting
    Gray = 1,    // the trial deletion reached it
    Purple = 2,  // a suspect, buffered on the suspects list
    Black = 3,   // garbage: the collector owns it and frees it
};

class CycleCollector;
template <class T>
class CycleRef;
template <class T>
class CycleWeakRef;

// The type-erased control block of a cycle-capable object. It lives in the
// same allocation as the object, so one allocation carries the value and its
// bookkeeping.
struct CycleControl {
    std::size_t owners = 1;       // the strong references
    std::size_t trial = 0;        // the trial-deletion count
    std::size_t weak = 0;         // the weak observers
    CycleColor color = CycleColor::White;
    bool buffered = false;        // linked on the suspects list
    bool expired = false;         // the object was collected
    bool valueDestroyed = false;  // the object is gone, the block remains
    CycleControl* next = nullptr; // the intrusive suspects link

    virtual ~CycleControl() = default;

    // The strong edges of the object, for the trial deletion. The generated
    // class implements this through `CycleObject::cp3_cycle_visit`.
    virtual void visitChildren(CycleCollector& collector) = 0;

    // Destroys the object but keeps the block: a weak observer may still
    // hold it, and its read must answer `undefined`.
    virtual void destroyValue() = 0;
};

// The base of every cycle-capable class: it exposes the strong edges the
// collector walks. The generated class overrides `cp3_cycle_visit`.
class CycleObject {
public:
    // The runtime type stamp (`instanceof` compares it with the ids of the
    // target class and its known descendants).
    std::uint32_t cp3_type_id = 0;
    // The back-link to this object's control block. Set by the generated
    // struct's control block at construction time; null only for objects
    // already collected.
    CycleControl* cp3_cycle_control_ = nullptr;

    virtual ~CycleObject() = default;
    virtual void cp3_cycle_visit(CycleCollector& collector) = 0;
};

// The per-loop collector state. One collector per event loop: the state is
// thread-local, and the loop that owns the thread owns the collector. There
// are no locks and no atomics in the collector.
class CycleCollector {
public:
    // The budget of one tick: at most this many objects are visited.
    static constexpr std::size_t kMaxVisitedPerTick = 4096;
    // The suspects list grows past this before a tick is triggered.
    static constexpr std::size_t kSuspectThreshold = 1024;
    // The time budget of one tick, in microseconds.
    static constexpr std::size_t kMaxMicrosPerTick = 200;

    // The collector of the current thread (the current event loop).
    static CycleCollector& current();

    // Buffers an object whose count dropped but did not reach zero. The
    // object becomes a suspect (purple) and is linked on the suspects list.
    void suspect(CycleControl* control);

    // One bounded collection pass. Returns the number of objects freed.
    // The pass is resumable: when the budget runs out, the next call
    // continues where this one stopped.
    std::size_t collect();

    // Drains the suspects list completely (the loop exit).
    std::size_t drain();

    // The number of objects this collector freed.
    std::size_t freedCount() const { return freed_; }
    // The number of suspects waiting for a pass.
    std::size_t suspectCount() const { return suspectCount_; }

    // The budget of one tick. The runtime test driver lowers it to prove
    // that a budgeted collection resumes where it stopped.
    void setBudget(std::size_t visited, std::size_t micros) {
        maxVisited_ = visited;
        maxMicros_ = micros;
    }

    // One strong edge of an object, visited by the trial deletion. In the
    // marking phase it is a trial decrement; the collector decides what the
    // edge means, so the generated visitor has a single shape.
    template <class T>
    void visitEdge(CycleRef<T>& edge);

private:
    // The gray set of the trial in progress.
    void markGray(CycleControl* control);
    void restore();
    std::size_t collectGarbage();

    CycleControl* suspects_ = nullptr;
    std::size_t suspectCount_ = 0;
    // The trial in progress.
    bool trialActive_ = false;
    bool trialAbort_ = false;
    std::size_t grayIndex_ = 0;
    std::vector<CycleControl*> gray_;    // the island
    std::vector<CycleControl*> touched_;  // every object with a trial count
    std::size_t visited_ = 0;
    std::size_t freed_ = 0;
    // The budget of one tick.
    std::size_t maxVisited_ = kMaxVisitedPerTick;
    std::size_t maxMicros_ = kMaxMicrosPerTick;
};

// The control block of a cycle-capable object, with the object inside it.
// The object sits in an `optional` so the collector can destroy it while the
// block survives for a weak observer (an explicit destructor call on a plain
// member would destroy it twice: once by the call, once by the member
// teardown).
template <class T>
struct CycleControlOf final : CycleControl {
    std::optional<T> value;

    template <class... Args>
    explicit CycleControlOf(std::uint32_t typeId, Args&&... args) {
        value.emplace(std::forward<Args>(args)...);
        value->cp3_type_id = typeId;
        value->cp3_cycle_control_ = this;
    }

    void visitChildren(CycleCollector& collector) override {
        if (value) value->cp3_cycle_visit(collector);
    }

    void destroyValue() override {
        if (valueDestroyed) return;
        valueDestroyed = true;
        value.reset();
    }

    ~CycleControlOf() override { destroyValue(); }
};

// A reference to a cycle-capable object. The handle is a counted pointer
// with the cycle bookkeeping; the object itself is reached through the
// control block.
template <class T>
class CycleRef {
public:
    using element_type = T;

    friend class CycleWeakRef<T>;

    CycleRef() noexcept = default;
    // The control block knows the object lives inside it; generated code
    // builds the handle from the back-pointer on `this`. (Public because
    // generated inline code builds it, not ordinary user code.)
    explicit CycleRef(CycleControl* control) noexcept : control_(control) {
        if (control_) ++control_->owners;
    }

    CycleRef(std::nullptr_t) noexcept {}

    template <class... Args>
    static CycleRef make(std::uint32_t typeId, Args&&... args) {
        CycleRef ref;
        ref.control_ = new CycleControlOf<T>(typeId, std::forward<Args>(args)...);
        return ref;
    }

    CycleRef(const CycleRef& other) noexcept : control_(other.control_) {
        retain();
    }
    CycleRef(CycleRef&& other) noexcept
        : control_(std::exchange(other.control_, nullptr)) {}
    ~CycleRef() { release(); }

    CycleRef& operator=(const CycleRef& other) noexcept {
        if (this == &other) return *this;
        CycleControl* next = other.control_;
        if (next) ++next->owners;
        release();
        control_ = next;
        return *this;
    }
    CycleRef& operator=(CycleRef&& other) noexcept {
        if (this == &other) return *this;
        release();
        control_ = std::exchange(other.control_, nullptr);
        return *this;
    }
    CycleRef& operator=(std::nullptr_t) noexcept {
        release();
        return *this;
    }

    T* get() noexcept {
        if (control_ == nullptr) return nullptr;
        auto& value = static_cast<CycleControlOf<T>*>(control_)->value;
        return value ? &*value : nullptr;
    }
    T* get() const noexcept {
        if (control_ == nullptr) return nullptr;
        const auto& value = static_cast<CycleControlOf<T>*>(control_)->value;
        // A const handle does not lock the pointee (shared_ptr semantics):
        // the control block may mark it destroyed, so `value` is const here.
        return value ? const_cast<T*>(&*value) : nullptr;
    }
    T& operator*() noexcept { return *get(); }
    T& operator*() const noexcept { return *get(); }
    T* operator->() noexcept { return get(); }
    T* operator->() const noexcept { return get(); }
    explicit operator bool() const noexcept { return control_ != nullptr; }
    std::size_t use_count() const noexcept {
        return control_ ? control_->owners : 0;
    }
    void reset() noexcept { release(); }

    friend bool operator==(const CycleRef& a, std::nullptr_t) noexcept {
        return a.control_ == nullptr;
    }
    friend bool operator==(std::nullptr_t, const CycleRef& b) noexcept {
        return b.control_ == nullptr;
    }
    friend bool operator!=(const CycleRef& a, std::nullptr_t) noexcept {
        return a.control_ != nullptr;
    }
    friend bool operator!=(std::nullptr_t, const CycleRef& b) noexcept {
        return b.control_ != nullptr;
    }
    friend bool operator==(const CycleRef& a, const CycleRef& b) noexcept {
        return a.control_ == b.control_;
    }
    friend bool operator!=(const CycleRef& a, const CycleRef& b) noexcept {
        return a.control_ != b.control_;
    }

    // The control block, for the collector and the weak link.
    CycleControl* control() const noexcept { return control_; }

private:
    void retain() noexcept {
        if (control_) ++control_->owners;
    }
    void release() noexcept {
        CycleControl* c = control_;
        if (!c) return;
        control_ = nullptr;
        if (--c->owners == 0) {
            if (c->color == CycleColor::Black) {
                // The collector owns this object: it frees it with the rest
                // of the garbage island.
                return;
            }
            c->destroyValue();
            c->expired = true;
            if (c->weak == 0 && !c->buffered) {
                delete c;
            }
            // Otherwise the object is on the suspects list with `buffered`
            // set: freeing it here would leave a dangling entry. The block
            // is released when the collector dequeues it (not earlier; the
            // list holds the only remaining reference).
            return;
        }
        // The object survives the release: it may be part of a cycle.
        if (c->color == CycleColor::White) {
            c->color = CycleColor::Purple;
            CycleCollector::current().suspect(c);
        }
    }

    CycleControl* control_ = nullptr;
};

template <class T>
void CycleCollector::visitEdge(CycleRef<T>& edge) {
    CycleControl* child = edge.control();
    if (child == nullptr || trialAbort_) return;
    if (child->trial == 0) touched_.push_back(child);
    ++child->trial;
    if (child->trial > child->owners) {
        // More internal references than the child has: an external
        // reference exists, so the island is alive.
        trialAbort_ = true;
        return;
    }
    if (child->trial == child->owners && child->color == CycleColor::White) {
        // Every reference to the child is internal: it is part of the island.
        child->color = CycleColor::Gray;
        markGray(child);
    }
}

// A strong self handle from a raw `this` pointer. Generated code uses it to
// return `this` from a method of a cycle-capable class: the raw object knows
// its control block through the back-pointer `CycleObject::cp3_cycle_control_`.
template <class T>
CycleRef<T> cp3_cycle_self(T& object) {
    return CycleRef<T>{object.cp3_cycle_control_};
}

// A strong read of a cycle-aware weak link: `CycleRef<T>`, or `undefined`
// (nullopt) when the target was collected.
template <class T>
using MaybeCycleRef = std::optional<CycleRef<T>>;

// The cycle-aware allocation: the control block carries the trial-deletion
// bookkeeping, so the object and its count live in one allocation.
template <class T, class... Args>
CycleRef<T> makeCycleRef(std::uint32_t typeId, Args&&... args) {
    return CycleRef<T>::make(typeId, std::forward<Args>(args)...);
}

// A weak link to a cycle-capable object. It does not keep its target alive;
// a read of a collected target answers `undefined`.
template <class T>
class CycleWeakRef {
public:
    CycleWeakRef() noexcept = default;
    CycleWeakRef(std::nullptr_t) noexcept {}
    CycleWeakRef(const CycleRef<T>& strong) noexcept : control_(strong.control_) {
        if (control_) ++control_->weak;
    }

    // The live target, or a null handle when the island was collected.
    CycleRef<T> lock() const noexcept {
        if (control_ == nullptr || control_->expired) {
            return CycleRef<T>{};
        }
        CycleRef<T> strong;
        strong.control_ = control_;
        ++strong.control_->owners;
        return strong;
    }

    CycleWeakRef(const CycleWeakRef& other) noexcept
        : control_(other.control_) {
        if (control_) ++control_->weak;
    }
    CycleWeakRef(CycleWeakRef&& other) noexcept
        : control_(std::exchange(other.control_, nullptr)) {}
    ~CycleWeakRef() { release(); }

    CycleWeakRef& operator=(const CycleWeakRef& other) noexcept {
        if (this == &other) return *this;
        retain(other.control_);
        release();
        control_ = other.control_;
        return *this;
    }
    CycleWeakRef& operator=(CycleWeakRef&& other) noexcept {
        if (this == &other) return *this;
        release();
        control_ = std::exchange(other.control_, nullptr);
        return *this;
    }
    CycleWeakRef& operator=(std::nullptr_t) noexcept {
        release();
        return *this;
    }
    // A weak link stores the target's strong handle: the handle is converted
    // (the strong count is not decremented by the store).
    CycleWeakRef& operator=(const CycleRef<T>& strong) noexcept {
        retain(strong.control_);
        release();
        control_ = strong.control_;
        return *this;
    }
    void reset() noexcept { release(); }

    // The weak link can be upgraded to a strong handle while the target is
    // alive; `operator bool` reports the same liveness.
    explicit operator bool() const noexcept {
        return control_ != nullptr && !control_->expired;
    }

private:
    void retain(CycleControl* control) noexcept {
        if (control) ++control->weak;
    }
    void release() noexcept {
        CycleControl* c = control_;
        if (!c) return;
        control_ = nullptr;
        if (--c->weak == 0 && c->owners == 0) {
            if (c->buffered) {
                // The object died and its entry is still queued on the
                // suspects list: freeing the block here would dangle. The
                // collector releases the block when it dequeues the entry.
                return;
            }
            delete c;
        }
    }

    CycleControl* control_ = nullptr;
};

// A strong read of a cycle-aware weak link: `CycleRef<T>`, or `undefined`
// (nullopt) when the target was collected.
template <class T>
MaybeCycleRef<T> readCycleWeak(const CycleWeakRef<T>& link) noexcept {
    if (CycleRef<T> strong = link.lock()) {
        return std::move(strong);
    }
    return std::nullopt;
}

}  // namespace cp3::runtime

#endif  // CP3_CORE_CYCLE_HPP
