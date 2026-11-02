// =============================================================================
// src/codegen/src/support/helper_blocks.cpp - PRIVATE generated-code helper
// blocks (see detail/helper_blocks.h).
//
// The always-on per-TU block plus the per-family additions, shared by the
// single-unit assembly (code_generator.cpp) and the whole-program assembler
// (emit/program.cpp) so both paths splice identical bytes. Each family block
// is emitted only when Generator::run saw the family in the unit's text.
// =============================================================================
#include "detail/generator/helper_blocks.h"

namespace cp3 {
namespace detail {

// The generated per-TU helper block (anonymous namespace in the .cpp).
const char* const kHelperBlock = R"CP3HELPERS(namespace {

// A generic function's type-parameter receiver (spec 16) is either a generated
// value struct or a `Ref<T>` class handle. Dereference the handle so one member
// spelling (`cp3_member_base(x).field`) works for both.
template <typename T>
decltype(auto) cp3_member_base(T&& value) {
    if constexpr (requires { *value; }) {
        return *value;
    } else {
        return (value);
    }
}

// ----- cp3 value formatting (routes into the D9 runtime formatter) -----------
inline std::string cp3_ts(double v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(const std::string& v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(bool v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(cp3::runtime::Undefined v) { return cp3::runtime::toString(v); }
inline std::string cp3_ts(std::nullptr_t v) { return cp3::runtime::toString(v); }
// Native integers (D65) and the int64 fast path (D64) format exact digits.
template <class T, std::enable_if_t<std::is_integral_v<T> &&
                                    !std::is_same_v<T, bool>, int> = 0>
inline std::string cp3_ts(T v) { return std::to_string(v); }
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
// A native integer option (`r_'i64' | undefined`, D65) and the int64 fast
// path: format the exact value through the ordinary overload.
template <class T>
inline std::string cp3_ts(const std::optional<T>& v) {
    return v.has_value() ? cp3_ts(*v) : std::string("undefined");
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
template <class T>
inline std::string cp3_ts_null(const std::optional<T>& v) {
    return v.has_value() ? cp3_ts(*v) : std::string("null");
}
// A three-way nullable (`T | null | undefined`): Maybe stores the flavor, so
// the empty case prints the flavor itself.
template <class T>
inline std::string cp3_ts(const cp3::runtime::Maybe<T>& v) {
    if (v.isNull()) return std::string("null");
    if (v.isUndefined()) return std::string("undefined");
    return cp3_ts(v.value());
}
// A boxed erroneous value (cp3/core/error.hpp): `catch (e)` binds one, and cp3
// renders it as the thrown value itself.
inline std::string cp3_ts(const ::cp3::runtime::Thrown& v) { return v.toString(); }
// The Error hierarchy is a reference type; a null handle prints as null, and a
// live one uses its own toString.
inline std::string cp3_ts(const ::cp3::runtime::Ref<::cp3::Error>& v) {
    return v ? v->toString() : std::string("null");
}

// ----- JS truthiness ---------------------------------------------------------
inline bool cp3_truthy(double v) { return v != 0.0 && !std::isnan(v); }
// The int64 fast path (D64) keeps a `number` as std::int64_t inside a fast
// body; a native integer (D65) is std::int8_t .. std::uint64_t. Both keep their
// exact value in the formatter (no double round-trip) and are truthy when
// non-zero.
template <class T, std::enable_if_t<std::is_integral_v<T> &&
                                        !std::is_same_v<T, bool>,
                                    int> = 0>
inline bool cp3_truthy(T v) {
    return v != 0;
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
inline bool cp3_truthy(const std::optional<T>& v) {
    return v.has_value() && cp3_truthy(*v);
}
template <class T>
inline bool cp3_truthy(const cp3::runtime::Maybe<T>& v) {
    return v.hasValue() && cp3_truthy(v.value());
}

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

// The family's per-TU helpers (formatter + truthiness), added only when the
// unit uses the family: the templates name cp3::runtime types that exist only
// with the cp3/typed_array.hpp include.
const char* const kTypedArrayHelperBlock = R"CP3HELPERS(namespace {

// ----- the JS-compat typed-array family (D69) --------------------------------
// A whole typed view formats as `Int8Array(2) [1, 0]`. The overload must be a
// member template: a Ref<T> would otherwise bind to `cp3_ts(bool)` through the
// pointer conversion.
template <class Tag>
inline std::string cp3_ts(const cp3::runtime::TypedArray<Tag>& a) {
    return cp3::runtime::typedArrayText(a);
}
// A whole regular array formats as `[1, 2, 3]` (the pinned corpus form:
// element text in ", " spacing, no class prefix and no inner padding).
template <class E>
inline std::string cp3_ts(const cp3::runtime::Array<E>& a) {
    return "[" + cp3::runtime::arrayJoin(a, std::string(", ")) + "]";
}
// Every array is an object: JS truthiness is always true, the empty array
// included. Without these, `if (a)` finds no overload (Ref's bool conversion
// is explicit).
template <class Tag>
inline bool cp3_truthy(const cp3::runtime::TypedArray<Tag>&) { return true; }
template <class E>
inline bool cp3_truthy(const cp3::runtime::Array<E>&) { return true; }

}  // namespace

)CP3HELPERS";

// std/buffer's per-TU helpers: `console.log(buf)` renders the Node form and a
// Buffer is always truthy. Added only when the unit uses the module (the
// overloads name ::cp3::runtime::buffer::Buffer, which exists only with the
// cp3/std/buffer.hpp include).
const char* const kBufferHelperBlock = R"CP3HELPERS(namespace {

// ----- std/buffer (the nominal Buffer) ---------------------------------------
inline std::string cp3_ts(const ::cp3::runtime::buffer::Buffer& b) {
    return ::cp3::runtime::buffer::inspect(b);
}
inline bool cp3_truthy(const ::cp3::runtime::buffer::Buffer&) { return true; }

}  // namespace

)CP3HELPERS";

// The builtin EventEmitter's per-TU helpers (D74), added only when the unit
// uses the builtin: `EventValue` exists only with the cp3/async/emitter.hpp
// include, and a listener parameter arrives as one.
const char* const kEventEmitterHelperBlock = R"CP3HELPERS(namespace {

// ----- the builtin EventEmitter (D74) -----------------------------------------
// A listener parameter is the erased emitted value; formatting and truthiness
// follow the value it carries.
inline std::string cp3_ts(const cp3::runtime::EventValue& v) {
    return v.toString();
}
inline bool cp3_truthy(const cp3::runtime::EventValue& v) {
    return v.truthy();
}

}  // namespace

)CP3HELPERS";

// The spec 15 object surface helpers: a class/interface/object handle (and an
// optional over one) is an object, so JS truthiness is always true. Added only
// when the unit uses `Ref<T>` (cp3/core/value.hpp); without these, `if (obj)`
// finds no overload (Ref's bool conversion is explicit).
const char* const kRefHelperBlock = R"CP3HELPERS(namespace {

// ----- spec 15 handles (Ref<T> / interfaces) ----------------------------------
template <class T>
inline bool cp3_truthy(const ::cp3::runtime::Ref<T>&) { return true; }
template <class T>
inline bool cp3_truthy(const ::cp3::runtime::LocalRef<T>&) { return true; }
// A generated object value shape is formatted by its pointee; without this,
// `console.log(obj)` binds a `Ref<T>` to `cp3_ts(bool)` through the pointer
// conversion. SFINAE keeps it out of the overload set for handles without a
// `cp3ToText()` (class instances format through their own surface).
template <class T>
inline auto cp3_ts(const ::cp3::runtime::Ref<T>& v) -> decltype(v->cp3ToText()) {
    return v->cp3ToText();
}
template <class T>
inline auto cp3_ts(const ::cp3::runtime::LocalRef<T>& v) -> decltype(v->cp3ToText()) {
    return v->cp3ToText();
}

}  // namespace

)CP3HELPERS";

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
