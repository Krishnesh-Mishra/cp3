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
//   `e.emit(ev, a, b)`     -> `(e)->emit(ev, EventValue(a), EventValue(b))`
//   a function-typed value -> `::cp3::runtime::Listener`
//   an arrow value         -> `::cp3::runtime::Listener::box([&](...) { ... })`
//
// EMITTED VALUES. The listener surface is `(...args: unknown[]) => void`
// (spec §14), so an emit carries a LIST of values, not one: `emit(ev, a, b)`
// hands the listener both. The values are erased into `EventValue`s (the
// runtime has no dynamic value), and a listener takes the values it declares:
// a nullary listener takes none, a one-parameter listener takes the first, and
// a listener that declares more parameters than were emitted sees `undefined`
// for the missing ones (JS parity).
//
// LISTENER IDENTITY (D74). `off(event, listener)` removes the listener that is
// the same box the matching `on`/`once` registered: `Listener` is a handle to a
// heap box and `same()` compares the boxes. A function-typed local
// (`let h = () => { ... }`) is boxed once at its declaration, so the `h` handed
// to `on` and the one handed to `off` are the same box. Two inline arrows are
// two different boxes, so `off("go", () => { ... })` never removes an earlier
// `on("go", () => { ... })`, matching JS function identity. A value that is not
// boxed at its declaration (a named function, a `std::function` local) is boxed
// with an identity - the address of the value it came from - so every box of
// that value is the same listener.
//
// The include is gated by codegen: a program that never mentions the builtin
// does not see this header (code_generator.cpp's include assembly).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/core/runtime.hpp"
#include "cp3/core/value.hpp"

namespace cp3::runtime {

// The most values one listener can take (and so the most one `emit` carries
// to a listener that declares that many parameters).
inline constexpr std::size_t kMaxListenerArgs = 8;

// The erased value handed to a listener. cp3's listener surface is
// `(...args: unknown[]) => void` and C++ callables are arity-fixed, so the
// runtime hands a listener the values it declares: a nullary listener ignores
// them, and a listener parameter sees the erased `EventValue` (or the value
// the codegen converted it to).
class EventValue {
public:
    EventValue() = default;  // cp3 `undefined`
    EventValue(std::nullptr_t) : kind_(Kind::Null) {}
    EventValue(double v) : kind_(Kind::Number), number_(v) {}
    EventValue(bool v) : kind_(Kind::Bool), boolean_(v) {}
    EventValue(std::string v) : kind_(Kind::String), text_(std::move(v)) {}
    EventValue(const char* v) : kind_(Kind::String), text_(v ? v : "") {}
    EventValue(Undefined) {}
    // An object payload (cp3.Error, a class instance, a generated struct):
    // kept BY HANDLE so listeners observe one live object, exactly like JS
    // carries references. The erased carrier lets the bus stay homogenous;
    // typed listeners unbox to the declared payload type.
    EventValue(std::shared_ptr<void> object)
        : kind_(Kind::Object), object_(std::move(object)) {}

    // The value's cp3 text (DECISIONS D9 formatting for numbers).
    std::string toString() const {
        switch (kind_) {
            case Kind::Null: return "null";
            case Kind::Number: return cp3::runtime::toString(number_);
            case Kind::Bool: return cp3::runtime::toString(boolean_);
            case Kind::String: return text_;
            case Kind::Object: return "[object]";
            default: return cp3::runtime::toString(undefined);
        }
    }

    // The value as a number (the ECMAScript `Number(value)` conversion): a
    // number is itself, a boolean is 1/0, `null` is 0, a string is its numeric
    // value (0 when it is empty, NaN when it is not a number) and `undefined`
    // is NaN.
    double toNumber() const {
        switch (kind_) {
            case Kind::Number: return number_;
            case Kind::Bool: return boolean_ ? 1.0 : 0.0;
            case Kind::Null: return 0.0;
            case Kind::String: return stringToNumber(text_);
            default: return std::numeric_limits<double>::quiet_NaN();
        }
    }

    // JS truthiness of the carried value ("" and 0 and NaN are falsy).
    bool truthy() const {
        switch (kind_) {
            case Kind::Null: return false;
            case Kind::Number: return number_ != 0.0 && number_ == number_;
            case Kind::Bool: return boolean_;
            case Kind::String: return !text_.empty();
            case Kind::Object: return static_cast<bool>(object_);
            default: return false;
        }
    }

    // The object handle when this value carries one, nullptr otherwise.
    // (Listeners cast it to the declared payload type.)
    const void* object() const { return object_.get(); }
    const std::shared_ptr<void>& objectHandle() const { return object_; }
    // The payload cast to the declared type: usable only at the typed
    // listener boundary, where the emit site built the payload with
    // exactly that type.
    template <class T> std::shared_ptr<T> objectCast() const {
        return std::static_pointer_cast<T>(object_);
    }
    double asNumber() const { return number_; }
    bool asBool() const { return boolean_; }
    const std::string& asString() const { return text_; }

private:
    enum class Kind { Undefined, Null, Number, Bool, String, Object };
    Kind kind_ = Kind::Undefined;
    double number_ = 0.0;
    bool boolean_ = false;
    std::string text_;
    std::shared_ptr<void> object_;

    // The numeric value of a string (ECMAScript `Number(string)`).
    static double stringToNumber(const std::string& text) {
        const char* begin = text.c_str();
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin) return 0.0;
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
            ++end;
        }
        if (*end != '\0') return std::numeric_limits<double>::quiet_NaN();
        return value;
    }
};

namespace detail {

// The type-erased box behind a `Listener`; the box's address is the listener's
// identity (see the header comment).
struct ListenerBox {
    virtual ~ListenerBox() = default;
    // The emitted values (0..kMaxListenerArgs of them reach a listener).
    virtual void invoke(const EventValue* args, std::size_t count) = 0;
};

// The EventValue arguments a listener callable takes: `N` of them.
template <std::size_t>
struct EventValueArg {
    using type = const EventValue&;
};

// Can the callable take `N` EventValue arguments?
template <class F, std::size_t... I>
constexpr bool acceptsValues(std::index_sequence<I...>) {
    return std::is_invocable_v<F&, typename EventValueArg<I>::type...>;
}

template <class F>
struct TypedListenerBox final : ListenerBox {
    explicit TypedListenerBox(F fn) : fn_(std::move(fn)) {}

    void invoke(const EventValue* args, std::size_t count) override {
        // The callable takes a fixed number of values: the largest number it
        // accepts (at most kMaxListenerArgs). The emitted values fill it, the
        // missing ones are `undefined`, and the extra ones are dropped (JS
        // drops the extra arguments too).
        Dispatch<kMaxListenerArgs>::run(*this, args, count);
    }

    // Calls the boxed callable with `N` values.
    template <std::size_t N>
    void callWith(const EventValue* args, std::size_t count) {
        call(args, count, std::make_index_sequence<N>{});
    }

private:
    template <std::size_t... I>
    void call(const EventValue* args, std::size_t count, std::index_sequence<I...>) {
        const EventValue undef;
        fn_(count > I ? args[I] : undef...);
    }

    // The dispatch: the largest number of values the callable accepts.
    template <std::size_t N>
    struct Dispatch {
        static void run(TypedListenerBox& box, const EventValue* args,
                        std::size_t count) {
            if constexpr (N == 0) {
                box.template callWith<0>(args, count);
            } else if constexpr (acceptsValues<F>(std::make_index_sequence<N>{})) {
                box.template callWith<N>(args, count);
            } else {
                Dispatch<N - 1>::run(box, args, count);
            }
        }
    };

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

    // Boxes a callable with an identity (the address of the value it came
    // from: a named function, a `std::function` local). Every box of that
    // value is the same listener, so `off(event, handler)` removes the
    // listener `on(event, handler)` registered.
    template <class F>
    static Listener boxIdentified(F fn, const void* id) {
        Listener out;
        out.box_ = std::make_shared<detail::TypedListenerBox<F>>(std::move(fn));
        out.id_ = id;
        return out;
    }

    bool valid() const noexcept { return static_cast<bool>(box_); }

    // Identity: true when both handles refer to the same box (or to the same
    // identified value).
    bool same(const Listener& other) const noexcept {
        if (id_ != nullptr && other.id_ != nullptr) return id_ == other.id_;
        return box_ == other.box_;
    }

    void invoke() const { invoke(nullptr, 0); }

    void invoke(const EventValue& arg) const { invoke(&arg, 1); }

    void invoke(const EventValue* args, std::size_t count) const {
        if (box_) box_->invoke(args, count);
    }

private:
    std::shared_ptr<detail::ListenerBox> box_;
    const void* id_ = nullptr;
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

    void emit(const std::string& event) { emitWith(event, nullptr, 0); }

    void emit(const std::string& event, const EventValue& arg) {
        emitWith(event, &arg, 1);
    }

    void emit(const std::string& event, const EventValue& first,
              const EventValue& second) {
        const EventValue args[2] = {first, second};
        emitWith(event, args, 2);
    }

    void emit(const std::string& event, const EventValue& first,
              const EventValue& second, const EventValue& third) {
        const EventValue args[3] = {first, second, third};
        emitWith(event, args, 3);
    }

    // The wide form: an emit with more values than the fixed-arity overloads.
    void emit(const std::string& event, const std::vector<EventValue>& args) {
        emitWith(event, args.empty() ? nullptr : args.data(), args.size());
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
    void emitWith(const std::string& event, const EventValue* args,
                  std::size_t count) {
        const auto it = listeners_.find(event);
        if (it == listeners_.end()) return;
        const std::vector<Entry> snapshot = it->second;
        for (const Entry& entry : snapshot) {
            if (!entry.listener.valid()) continue;
            if (entry.once) removeById(event, entry.id);
            entry.listener.invoke(args, count);
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
