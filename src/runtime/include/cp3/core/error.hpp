// =============================================================================
// cp3/core/error.hpp - thrown values and unhandled-rejection reporting.
//
// OWNERSHIP: runtime module. This header is part of the generated-code ABI.
//
// cp3 error handling is JS-shaped (docs/language-spec/07-error-handling.md) and
// DECISIONS D39 says `throw` accepts ANY value - there is no built-in `Error`
// hierarchy in M1. A `catch` parameter therefore needs a single, uniform C++
// type that can carry a value of any cp3 type. That type is `Thrown`.
//
// CODEGEN CONTRACT (try/catch/throw):
//   * `throw expr`      -> `throw ::cp3::runtime::Thrown::make(expr);`
//   * `try { } catch (e) { }`
//        -> `try { ... }
//            catch (const ::cp3::runtime::Thrown& __thrown) {
//                 auto e = __thrown.getOrThrow<T>();   // T = sema's type for e
//                 ...
//            }`
//     (a `catch { }` with no binding is the same without the unbox line);
//   * if the catch binding's cp3 type is `unknown`/a union, use
//     `__thrown.get<T>()` (returns `const T*`, null on mismatch) and narrow.
//   * a `catch (...)` for native C++ exceptions from `cpp/` interop should
//     box via `::cp3::runtime::Thrown::fromCurrentException()`.
//
// `finally` is a codegen concern (scope guard, spec §07) - the runtime offers
// no finally primitive.
// =============================================================================
#pragma once

#include <exception>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "cp3/core/runtime.hpp"
#include "cp3/core/value.hpp"

namespace cp3::runtime {

// Renders a value of any cp3-representable type. Prefers an ADL/overload
// `toString(v)` (the generated classes can provide one), falls back to a
// generic placeholder for un-renderable user types.
template <class T>
std::string renderThrownValue(const T& value) {
    if constexpr (requires { toString(value); }) {
        return toString(value);
    } else {
        return std::string("<value>");
    }
}

// A type-erased thrown value. Copyable, handle-like (copying a `Thrown` copies
// the reference to the boxed value, exactly like a cp3 reference handle).
//
// `Thrown` is what a rejected `Promise` carries (see cp3/async/promise.hpp) and what
// `await` re-throws on a rejected promise, so a plain cp3 `try`/`catch` around
// `await` works unchanged.
class Thrown {
public:
    // The zero/empty case is cp3 `undefined` (throws of `undefined`).
    Thrown() = default;
    explicit Thrown(std::nullptr_t) : kind_(Kind::Null) {}

    // Box a copy of `value`. `T` must be cloneable by `cp3::runtime::clone`.
    template <class T>
    static Thrown make(T value) {
        Thrown t;
        t.kind_ = Kind::Boxed;
        t.type_ = &typeid(T);
        T* raw = new T(std::move(value));
        t.box_ = std::shared_ptr<void>(raw, [](void* p) { delete static_cast<T*>(p); });
        t.render_ = [](const void* p) -> std::string {
            return renderThrownValue(*static_cast<const T*>(p));
        };
        t.copy_ = [](const void* p) -> std::shared_ptr<void> {
            T* copy = new T(cp3::runtime::clone(*static_cast<const T*>(p)));
            return std::shared_ptr<void>(copy, [](void* q) { delete static_cast<T*>(q); });
        };
        return t;
    }

    // Box the current C++ exception (`std::current_exception`), for catch-all
    // sites bridging native exceptions into cp3.
    static Thrown fromCurrentException();

    // True when this carries a boxed value (not the empty/undefined default).
    bool hasValue() const noexcept { return kind_ == Kind::Boxed && static_cast<bool>(box_); }

    // True for the empty `Thrown` (cp3 `undefined`).
    bool isEmpty() const noexcept { return !hasValue() && kind_ != Kind::Null; }

    // True when this carries cp3 `null`.
    bool isNull() const noexcept { return kind_ == Kind::Null; }

    // The dynamic type of the boxed value; `typeid(void)` when empty.
    const std::type_info& type() const noexcept {
        return hasValue() && type_ != nullptr ? *type_ : typeid(void);
    }

    // Typed access. Returns nullptr on a type mismatch (so generated code can
    // narrow a union-typed catch binding without a hard failure).
    template <class T>
    const std::remove_cv_t<T>* get() const noexcept {
        using U = std::remove_cv_t<T>;
        if (!hasValue() || type_ == nullptr) {
            return nullptr;
        }
        if (*type_ != typeid(U)) {
            return nullptr;
        }
        return static_cast<const U*>(box_.get());
    }

    // Typed access that never returns null: on a mismatch it throws
    // `std::bad_cast` (an internal codegen bug, not a cp3-level condition).
    template <class T>
    const T& getOrThrow() const {
        const T* p = get<std::remove_cvref_t<T>>();
        if (p == nullptr) {
            throw std::bad_cast();
        }
        return *p;
    }

    // A deep copy of the thrown value (used when a rejection crosses a `worker`
    // boundary; spec §13 requires genuine copies, never shared handles).
    Thrown deepCopy() const {
        if (!hasValue()) {
            Thrown t;
            t.kind_ = kind_;
            return t;
        }
        Thrown t;
        t.kind_ = Kind::Boxed;
        t.type_ = type_;
        t.box_ = copy_ ? copy_(box_.get()) : box_;
        t.render_ = render_;
        t.copy_ = copy_;
        return t;
    }

    // Human-readable rendering of the thrown value, for diagnostics and for the
    // unhandled-rejection message.
    std::string toString() const;

private:
    enum class Kind { Undefined, Null, Boxed };

    Kind kind_ = Kind::Undefined;
    std::shared_ptr<void> box_;
    const std::type_info* type_ = nullptr;
    std::string (*render_)(const void*) = nullptr;
    std::shared_ptr<void> (*copy_)(const void*) = nullptr;
};

// Terminate the process because a rejected Promise was never handled
// (DECISIONS D43). Prints a diagnostic-style line to stderr and exits with
// `kUnhandledRejectionExitCode` (declared in cp3/async/promise.hpp).
[[noreturn]] void fatalUnhandledRejection(const Thrown& reason);

// The exit code used for both unhandled rejections and worker failures.
// DECISIONS D43/D36 defer the exact value; this is the single place to change.
inline constexpr int kFailureExitCode = 1;

}  // namespace cp3::runtime
