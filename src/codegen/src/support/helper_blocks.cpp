// =============================================================================
// src/codegen/src/support/helper_blocks.cpp - PRIVATE generated-code helper
// blocks (see detail/helper_blocks.h).
//
// Two families whose generated-code helpers would otherwise sit unused in
// every unit's .cpp: the primitive union formatter/truthiness pair (nameable
// only with cp3/core/union.hpp) and the object value shape dispatchers (SFINAE
// over a generated struct's own members). Each is emitted only when
// Generator::run saw the family in the unit's text.
// =============================================================================
#include "detail/helper_blocks.h"

#include <string>

namespace cp3 {
namespace detail {

// The per-TU value-model helpers: formatting and truthiness, then the
// `finally` guard (spec 7, D37). Kept as two raw blocks so neither
// function crosses the layout budget.
const char* commonValueHelperBlock() {
    return R"CP3HELPERS(namespace {

// ----- cp3 value formatting (routes into the D9 runtime formatter) -----------
inline std::string cp3_ts(double v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(const std::string& v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(bool v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(cp3::runtime::Undefined v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(std::nullptr_t v) { return cp3::runtime::toString(v); }
// Defensive: keeps a raw C string from binding to cp3_ts(bool) through the
// pointer->bool conversion (which would format every literal as "true").
inline std::string cp3_ts(const char* v) {
    return cp3::runtime::toString(std::string(v ? v : ""));
}
inline std::string cp3_ts(const std::optional<double>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("undefined");
}
inline std::string cp3_ts(const std::optional<std::string>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("undefined");
}
inline std::string cp3_ts(const std::optional<bool>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("undefined");
}
// A `T | null` optional: its empty case is `null`, not `undefined`.
inline std::string cp3_ts_null(const std::optional<double>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("null");
}
inline std::string cp3_ts_null(const std::optional<std::string>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("null");
}
inline std::string cp3_ts_null(const std::optional<bool>& v) {
    return v.has_value() ? cp3::runtime::toString(*v) : std::string("null");
}
// A three-way nullable (`T | null | undefined`): Maybe stores the flavor, so
// the empty case prints the flavor itself.
template <class T>
inline std::string cp3_ts(const cp3::runtime::Maybe<T>& v) {
    if (v.isNull()) return std::string("null");
    if (v.isUndefined()) return std::string("undefined");
    return cp3::runtime::toString(v.value());
}
// A boxed erroneous value (cp3/core/error.hpp): `catch (e)` binds one, and cp3
// renders it as the thrown value itself.
inline std::string cp3_ts(const ::cp3::runtime::Thrown& v) { return v.toString(); }

// ----- JS truthiness ---------------------------------------------------------
inline bool cp3_truthy(double v) { return v != 0.0 && !std::isnan(v); }
// The int64 fast path (D64) keeps a `number` as std::int64_t inside a fast
// body; its truthiness and its formatting are those of the exactly equal
// double, so these two overloads are what keeps `cp3_truthy(0LL)` and
// `cp3_ts(5LL)` unambiguous (and correct) in an int64 body.
inline bool cp3_truthy(std::int64_t v) { return v != 0; }
inline std::string cp3_ts(std::int64_t v) {
    return cp3::runtime::toString(static_cast<double>(v));
}
inline bool cp3_truthy(const std::string& v) { return !v.empty(); }
inline bool cp3_truthy(bool v) { return v; }
inline bool cp3_truthy(cp3::runtime::Undefined) { return false; }
inline bool cp3_truthy(std::nullptr_t) { return false; }
// Defensive: JS truthiness of a C string is "non-empty", not "non-null".
inline bool cp3_truthy(const char* v) { return v != nullptr && *v != '\0'; }
inline bool cp3_truthy(const std::optional<double>& v) {
    return v.has_value() && cp3_truthy(*v);
}
inline bool cp3_truthy(const std::optional<std::string>& v) {
    return v.has_value() && cp3_truthy(*v);
}
inline bool cp3_truthy(const std::optional<bool>& v) {
    return v.has_value() && cp3_truthy(*v);
}
template <class T>
inline bool cp3_truthy(const cp3::runtime::Maybe<T>& v) {
    return v.hasValue() && cp3_truthy(v.value());
}

}  // namespace

)CP3HELPERS";
}

const char* commonFinallyHelperBlock() {
    return R"CP3HELPERS(namespace {

// ----- cp3 `finally` (spec §07, DECISIONS D37) -------------------------------
// Runs the finally block on every exit of the enclosing scope - normal
// completion, `break`/`continue`/`return`, or a propagating throw - exactly
// once. The normal path calls `runNow()`, which deactivates the guard before
// invoking the block, so a throw from `finally` propagates instead of being
// silently repeated. The destructor is `noexcept(false)` for the same reason
// on a `return`/`break` path (JS replaces that completion with the throw); a
// throw while another exception is already in flight terminates the process.
template <class F>
class cp3_finally_guard {
public:
    explicit cp3_finally_guard(F fn) : fn_(fn) {}
    cp3_finally_guard(const cp3_finally_guard&) = delete;
    cp3_finally_guard& operator=(const cp3_finally_guard&) = delete;
    ~cp3_finally_guard() noexcept(false) {
        if (active_) {
            active_ = false;
            fn_();
        }
    }
    void runNow() {
        if (active_) {
            active_ = false;
            fn_();
        }
    }

private:
    F fn_;
    bool active_ = true;
};

}  // namespace
)CP3HELPERS";
}

std::string commonHelperBlock() {
    return std::string(commonValueHelperBlock()) + commonFinallyHelperBlock();
}

const char* unionHelperBlock() {
    return R"CP3HELPERS(namespace {

// ----- primitive unions (cp3/core/union.hpp) ---------------------------------
template <class... Ts>
inline std::string cp3_ts(const std::variant<Ts...>& v) {
    return std::visit(
        [](const auto& held) { return ::cp3::runtime::toString(held); }, v);
}
template <class... Ts>
inline bool cp3_truthy(const std::variant<Ts...>& v) {
    return ::cp3::runtime::unions::truthy(v);
}

}  // namespace

)CP3HELPERS";
}

const char* objectHelperBlock() {
    return R"CP3HELPERS(namespace {

// ----- object value shapes ---------------------------------------------------
template <class T>
inline auto cp3_ts(const T& v) -> decltype(v.cp3ToText()) {
    return v.cp3ToText();
}
template <class T>
inline auto cp3_truthy(const T& v) -> decltype(v.cp3Truthy()) {
    return v.cp3Truthy();
}

}  // namespace

)CP3HELPERS";
}

}  // namespace detail
}  // namespace cp3
