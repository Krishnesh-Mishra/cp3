// =============================================================================
// cp3/core/runtime.hpp - the generated-code runtime ABI.
//
// FROZEN INTERFACE (Beta One, frozen by the contracts pass - DECISIONS D14).
//
// This is the surface that *emitted C++* calls into; codegen and the runtime
// owner both obey it. Keeping it small and stable is the point: generated code
// includes this header and links `cp3_runtime`.
//
// Value representations used by M1 codegen (must match cp3/types.hpp):
//   * cp3 `number`    -> `double`
//   * cp3 `string`    -> `std::string`
//   * cp3 `boolean`   -> `bool`
//   * cp3 `null`      -> `std::nullptr_t`   (the value `nullptr`)
//   * cp3 `undefined` -> `cp3::runtime::Undefined` (the singleton `undefined`)
//
// `console.log`/`print` back onto toString + println (DECISIONS D11: standard
// C++ only; no fmt in M1). Number formatting is DECISIONS D9 (ECMAScript
// Number::toString) and is exact - tests compare stdout byte-for-byte.
// =============================================================================
#pragma once

#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cp3 {

// The cp3 Error hierarchy. Every thrown value is one of these classes (or a
// user subclass), so a `catch (e)` binding has one static representation:
// `Ref<Error>`. The fields are plain data, not runtime metadata: `name` and
// `message` are ordinary strings, `cause` is an ordinary reference handle, and
// `stack` is captured when the error is constructed.
class Error : public std::exception {
public:
    std::string message;
    std::string name = "Error";
    std::string stack;
    std::shared_ptr<Error> cause;

    Error() { refreshStack(); }
    explicit Error(std::string message,
                   std::shared_ptr<Error> cause = nullptr)
        : message(std::move(message)), cause(std::move(cause)) {
        refreshStack();
    }

    const char* what() const noexcept override { return message.c_str(); }

    virtual std::string toString() const {
        if (message.empty()) return name;
        return name + ": " + message;
    }

protected:
    void refreshStack() {
        stack = toString();
    }
};

// The standard subclasses. Their only behavior is the default `name` and, for
// AggregateError, the contained error list.
class TypeError : public Error {
public:
    TypeError() { name = "TypeError"; refreshStack(); }
    explicit TypeError(std::string message,
                       std::shared_ptr<Error> cause = nullptr)
        : Error(std::move(message), std::move(cause)) {
        name = "TypeError";
        refreshStack();
    }
};

class RangeError : public Error {
public:
    RangeError() { name = "RangeError"; refreshStack(); }
    explicit RangeError(std::string message,
                        std::shared_ptr<Error> cause = nullptr)
        : Error(std::move(message), std::move(cause)) {
        name = "RangeError";
        refreshStack();
    }
};

class SyntaxError : public Error {
public:
    SyntaxError() { name = "SyntaxError"; refreshStack(); }
    explicit SyntaxError(std::string message,
                         std::shared_ptr<Error> cause = nullptr)
        : Error(std::move(message), std::move(cause)) {
        name = "SyntaxError";
        refreshStack();
    }
};

class ReferenceError : public Error {
public:
    ReferenceError() { name = "ReferenceError"; refreshStack(); }
    explicit ReferenceError(std::string message,
                            std::shared_ptr<Error> cause = nullptr)
        : Error(std::move(message), std::move(cause)) {
        name = "ReferenceError";
        refreshStack();
    }
};

class EvalError : public Error {
public:
    EvalError() { name = "EvalError"; refreshStack(); }
    explicit EvalError(std::string message,
                       std::shared_ptr<Error> cause = nullptr)
        : Error(std::move(message), std::move(cause)) {
        name = "EvalError";
        refreshStack();
    }
};

class URIError : public Error {
public:
    URIError() { name = "URIError"; refreshStack(); }
    explicit URIError(std::string message,
                      std::shared_ptr<Error> cause = nullptr)
        : Error(std::move(message), std::move(cause)) {
        name = "URIError";
        refreshStack();
    }
};

class AggregateError : public Error {
public:
    // cp3 arrays are reference values (`Array<Error>`), so the list is shared
    // just like any other cp3 array; codegen's field read yields this handle.
    std::shared_ptr<std::vector<std::shared_ptr<Error>>> errors;

    AggregateError()
        : errors(std::make_shared<std::vector<std::shared_ptr<Error>>>()) {
        name = "AggregateError";
        refreshStack();
    }
    AggregateError(std::shared_ptr<std::vector<std::shared_ptr<Error>>> errors,
                   std::string message = {},
                   std::shared_ptr<Error> cause = nullptr)
        : Error(std::move(message), std::move(cause)),
          errors(errors ? std::move(errors)
                        : std::make_shared<std::vector<std::shared_ptr<Error>>>()) {
        name = "AggregateError";
        refreshStack();
    }
};

}  // namespace cp3

namespace cp3::runtime {

// The runtime representation of cp3 `undefined` (distinct from `null`, which is
// `std::nullptr_t`). An empty value type with a single shared instance.
struct Undefined {
    friend constexpr bool operator==(Undefined, Undefined) noexcept { return true; }
    friend constexpr bool operator!=(Undefined, Undefined) noexcept { return false; }
};

// The one cp3 `undefined` value. Generated code refers to this.
inline constexpr Undefined undefined{};

// --- formatting (the toString family used by console.log/print) --------------
//
// `toString(double)` implements DECISIONS D9: ECMAScript Number::toString -
// integers print with no decimal point, `-0` prints `"0"`,
// Infinity/-Infinity/NaN print as those words, and finite values use the
// shortest round-tripping decimal.
std::string toString(double value);
std::string toString(const std::string& value);
std::string toString(bool value);
std::string toString(std::nullptr_t);   // -> "null"
std::string toString(Undefined);        // -> "undefined"

// --- native interop (spec 19) ------------------------------------------------
//
// A native function that returns a `const char*` yields a cp3 `string`. A null
// pointer is the empty string: the C++ contract of `std::string(nullptr)` is
// undefined behavior, and a native library that reports "no result" must not
// take the program down.
inline std::string nativeString(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

// --- object utilities (spec 15.2) --------------------------------------------
//
// `Object.assign` lowers to explicit field copying in the generated unit and
// `Object.freeze` is a no-op on shared handles; the only ABI helper is
// `Object.is`, which follows JS exactly: `NaN === NaN`, `-0 !== +0`, and
// reference handles compare by identity.
inline bool cp3_object_is(double a, double b) {
    // Object.is: NaN equals NaN (unlike ==), and -0 is not equal to +0.
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    const bool sa = std::signbit(a);
    const bool sb = std::signbit(b);
    return a == b && sa == sb;
}
inline bool cp3_object_is(const std::string& a, const std::string& b) {
    return a == b;
}
inline bool cp3_object_is(bool a, bool b) { return a == b; }
// The reference-handle overload lives in value.hpp (which includes this
// header), so it can name `Ref<T>` without a cycle.

// --- global numeric parsing builtins (STDLIB docs section 3) ----------------
//
// `parseInt` / `parseFloat` are core-language globals in cp3, not string
// methods. parseInt skips leading whitespace, accepts an optional sign, a
// `0x`/`0X` prefix selects base 16 (DECISIONS D46), otherwise base 10, and
// parsing stops at the first character that is not a digit of that base; no
// digits at all yields NaN. parseFloat accepts the longest decimal prefix
// (fraction and exponent included); no digits yields NaN. Both return a cp3
// `number` (a 64-bit double).
double parseInt(const std::string& text);
double parseFloat(const std::string& text);

// --- stdout -----------------------------------------------------------------
//
// print: writes exactly the string, no trailing newline.
// println: writes the string followed by a newline.
void print(const std::string& text);
void println(const std::string& text);

// A fatal runtime error: the program cannot continue (a call whose arguments
// do not fit the callee). The message follows the std families' spec-
// violation path: a diagnostic line on stderr and exit code 1.
[[noreturn]] inline void fatalError(const char* message) {
    std::fprintf(stderr, "cp3: Error: %s\n", message);
    std::fflush(stderr);
    std::exit(1);
}

// --- provenance (unchanged) --------------------------------------------------
//
// Identifies the runtime support library (for diagnostics / provenance).
// Stable within a build.
const char* name() noexcept;

// Human-readable runtime version, independent of the compiler version.
std::string version();

}  // namespace cp3::runtime

// The three-way nullish value (`T | null | undefined`). Included last: it
// builds on the `Undefined` value and `toString` declarations above. Additive
// to the frozen surface (no existing signature changed).
#include "cp3/core/maybe.hpp"
