// =============================================================================
// cp3/maybe.hpp — the runtime representation of a three-way nullish value
// (`T | null | undefined`).
//
// OWNERSHIP: runtime module. Included by cp3/runtime.hpp, so every generated
// translation unit sees it when it includes the runtime ABI header.
//
// `std::optional<T>` covers `T | null` and `T | undefined`: the empty state has
// a single flavor. A three-way union needs both at once, so `Maybe<T>` stores
// the flavor alongside the value, matching JS semantics:
//   * `null` and `undefined` are distinct values (`null === undefined` is false);
//   * both are falsy and both trigger `??`;
//   * an uninitialized binding (`let x: T | null | undefined;`) is `undefined`;
//   * printing renders the flavor itself when empty.
// =============================================================================
#pragma once

#include <cstddef>
#include <utility>

#include "cp3/runtime.hpp"

namespace cp3::runtime {

template <class T>
class Maybe {
public:
    /// An uninitialized `let x: T | null | undefined` is `undefined` in JS.
    Maybe() noexcept = default;
    Maybe(T value) : flavor_(Flavor::Value), value_(std::move(value)) {}
    Maybe(std::nullptr_t) noexcept : flavor_(Flavor::Null) {}
    Maybe(Undefined) noexcept : flavor_(Flavor::Undefined) {}

    bool hasValue() const noexcept { return flavor_ == Flavor::Value; }
    bool isNull() const noexcept { return flavor_ == Flavor::Null; }
    bool isUndefined() const noexcept { return flavor_ == Flavor::Undefined; }
    bool isNullish() const noexcept { return flavor_ != Flavor::Value; }

    /// Precondition: `hasValue()`.
    const T& value() const noexcept { return value_; }

    /// Strict (JS `===`) identity: the flavor must agree, and a value compares
    /// by value. The flavor mismatch is what keeps `null` and `undefined`
    /// distinct.
    friend bool operator==(const Maybe& a, const Maybe& b) noexcept {
        if (a.flavor_ != b.flavor_) return false;
        if (a.flavor_ != Flavor::Value) return true;
        return a.value_ == b.value_;
    }
    friend bool operator!=(const Maybe& a, const Maybe& b) noexcept {
        return !(a == b);
    }

private:
    enum class Flavor { Undefined, Null, Value };
    Flavor flavor_ = Flavor::Undefined;
    T value_{};
};

}  // namespace cp3::runtime
