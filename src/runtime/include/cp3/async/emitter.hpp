// =============================================================================
// cp3/async/emitter.hpp - `EventEmitter`, the language builtin (D74, spec §14).
//
// OWNERSHIP: runtime module. This header is the generated-code ABI for the
// builtin event type; there is no `std/events` module and no native dependency
// (the emitter is a data structure over listener callbacks).
//
// CODEGEN CONTRACT
//   cp3 `EventEmitter`     -> `::cp3::runtime::Ref<::cp3::runtime::EventEmitter>`
//   `new EventEmitter()`   -> `::cp3::runtime::makeRef<::cp3::runtime::EventEmitter>()`
//   `e.on(ev, f)`          -> `(e)->on(ev, <boxed listener>)`
//   `e.once(ev, f)`        -> `(e)->once(ev, <boxed listener>)`
//   `e.off(ev, f)`         -> `(e)->off(ev, <boxed listener>)`
//   `e.emit(ev)`           -> `(e)->emit(ev)`
//   `e.emit(ev, a)`        -> `(e)->emit(ev, ::cp3::runtime::EventValue(a))`
//   a function-typed value -> `::cp3::runtime::Listener`
//   an arrow value         -> `::cp3::runtime::Listener::box([&](...) { ... })`
//
// LISTENER IDENTITY (D74). `off(event, listener)` removes the listener that is
// the same box the matching `on`/`once` registered: `Listener` is a handle to a
// heap box and `same()` compares the boxes. A function-typed local
// (`let h = () => { ... }`) is boxed once at its declaration, so the `h` handed
// to `on` and the one handed to `off` are the same box. Two inline arrows are
// two different boxes, so `off("go", () => { ... })` never removes an earlier
// `on("go", () => { ... })`, matching JS function identity.
//
// The include is gated by codegen: a program that never mentions the builtin
// does not see this header (code_generator.cpp's include assembly).
// =============================================================================
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/core/runtime.hpp"
#include "cp3/core/value.hpp"

namespace cp3::runtime {

// The erased value handed to a listener. cp3's listener surface is
// `(...args: unknown[]) => void` and C++ callables are arity-fixed, so the
// runtime passes at most the first emitted value; a nullary listener ignores
// it and an untyped listener parameter sees it as an `EventValue`.
class EventValue {
public:
    EventValue() = default;  // cp3 `undefined`
    EventValue(std::nullptr_t) : kind_(Kind::Null) {}
    EventValue(double v) : kind_(Kind::Number), number_(v) {}
    EventValue(bool v) : kind_(Kind::Bool), boolean_(v) {}
    EventValue(std::string v) : kind_(Kind::String), text_(std::move(v)) {}
    EventValue(const char* v) : kind_(Kind::String), text_(v ? v : "") {}
    EventValue(Undefined) {}

    // The value's cp3 text (DECISIONS D9 formatting for numbers).
    std::string toString() const {
        switch (kind_) {
            case Kind::Null: return "null";
            case Kind::Number: return cp3::runtime::toString(number_);
            case Kind::Bool: return cp3::runtime::toString(boolean_);
            case Kind::String: return text_;
            default: return cp3::runtime::toString(undefined);
        }
    }

    // JS truthiness of the carried value ("" and 0 and NaN are falsy).
    bool truthy() const {
        switch (kind_) {
            case Kind::Null: return false;
            case Kind::Number: return number_ != 0.0 && number_ == number_;
            case Kind::Bool: return boolean_;
            case Kind::String: return !text_.empty();
            default: return false;
        }
    }

private:
    enum class Kind { Undefined, Null, Number, Bool, String };
    Kind kind_ = Kind::Undefined;
    double number_ = 0.0;
    bool boolean_ = false;
    std::string text_;
};

namespace detail {

// The type-erased box behind a `Listener`; the box's address is the listener's
// identity (see the header comment).
struct ListenerBox {
    virtual ~ListenerBox() = default;
    virtual void invoke(const EventValue* arg) = 0;
};

template <class F>
struct TypedListenerBox final : ListenerBox {
    explicit TypedListenerBox(F fn) : fn_(std::move(fn)) {}

    void invoke(const EventValue* arg) override {
        if (arg != nullptr) {
            if constexpr (std::is_invocable_v<F&, const EventValue&>) {
                fn_(*arg);
            } else {
                (void)arg;  // a nullary listener ignores the emitted value
                fn_();
            }
        } else if constexpr (std::is_invocable_v<F&>) {
            fn_();
        } else {
            fn_(EventValue{});  // the listener's parameter is `undefined`
        }
    }

    F fn_;
};

}  // namespace detail

// A boxed callable: the representation of a function-typed cp3 value.
class Listener {
public:
    Listener() = default;

    // Boxes one callable (codegen: an arrow expression, or a function-typed
    // local's initializer). Copies of the returned handle are the same box.
    template <class F>
    static Listener box(F fn) {
        Listener out;
        out.box_ = std::make_shared<detail::TypedListenerBox<F>>(std::move(fn));
        return out;
    }

    bool valid() const noexcept { return static_cast<bool>(box_); }

    // Identity: true when both handles refer to the same box.
    bool same(const Listener& other) const noexcept {
        return box_ == other.box_;
    }

    void invoke() const {
        if (box_) box_->invoke(nullptr);
    }

    void invoke(const EventValue& arg) const {
        if (box_) box_->invoke(&arg);
    }

private:
    std::shared_ptr<detail::ListenerBox> box_;
};

// The builtin event type (spec §14). Listeners fire in registration order;
// `emit` with no listener is a quiet no-op.
class EventEmitter {
public:
    void on(const std::string& event, Listener listener) {
        entries(event).push_back(Entry{std::move(listener), false, nextId_++});
    }

    // Removed before it runs, so it fires at most once even when the emitter is
    // re-emitted from inside the listener's own body.
    void once(const std::string& event, Listener listener) {
        entries(event).push_back(Entry{std::move(listener), true, nextId_++});
    }

    // Removes the most recently added listener that is the same box as
    // `listener` (Node's removeListener semantics); a no-op when none matches.
    void off(const std::string& event, const Listener& listener) {
        const auto it = listeners_.find(event);
        if (it == listeners_.end()) return;
        std::vector<Entry>& list = it->second;
        for (std::size_t i = list.size(); i > 0; --i) {
            if (list[i - 1].listener.same(listener)) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i - 1));
                return;
            }
        }
    }

    void emit(const std::string& event) { emitWith(event, nullptr); }

    void emit(const std::string& event, const EventValue& arg) {
        emitWith(event, &arg);
    }

private:
    struct Entry {
        Listener listener;
        bool once = false;
        std::size_t id = 0;
    };

    std::vector<Entry>& entries(const std::string& event) {
        return listeners_[event];
    }

    // The snapshot is what fires (Node's clone-then-iterate): a listener added
    // while emitting does not run in this emit, and one removed while emitting
    // still runs in it unless it was a `once` that already fired.
    void emitWith(const std::string& event, const EventValue* arg) {
        const auto it = listeners_.find(event);
        if (it == listeners_.end()) return;
        const std::vector<Entry> snapshot = it->second;
        for (const Entry& entry : snapshot) {
            if (!entry.listener.valid()) continue;
            if (entry.once) removeById(event, entry.id);
            if (arg != nullptr) {
                entry.listener.invoke(*arg);
            } else {
                entry.listener.invoke();
            }
        }
    }

    void removeById(const std::string& event, std::size_t id) {
        const auto it = listeners_.find(event);
        if (it == listeners_.end()) return;
        std::vector<Entry>& list = it->second;
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].id == id) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

    std::map<std::string, std::vector<Entry>> listeners_;
    std::size_t nextId_ = 0;
};

}  // namespace cp3::runtime
