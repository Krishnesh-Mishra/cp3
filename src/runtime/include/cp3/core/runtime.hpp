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
#include <exception>
#include <string>

namespace cp3 {

// Base type for cp3 runtime errors (the future backing of cp3 `throw`/`try`).
// Minimal on purpose: a message and the standard `what()`. Real error classes
// will derive from this without reshaping it.
class Error : public std::exception {
public:
    Error() = default;
    explicit Error(std::string message) : message_(std::move(message)) {}

    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
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
