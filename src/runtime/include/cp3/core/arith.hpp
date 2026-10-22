// =============================================================================
// cp3/core/arith.hpp - `number` arithmetic backing for generated code (DECISIONS D64).
//
// ADDITIVE to the generated-code ABI (D14): every entry point is `inline`,
// header-only, so the required runtime translation-unit set is unchanged.
// Two layers live here:
//
// 1. `%` lowering (D64 item 2). `mod(a, b)` is a bit-exact replacement for
//    `std::fmod(a, b)` with an exact-integer fast path, so an integral hot
//    loop never calls libm; `modConst<K>(a)` is the same for a compile-time
//    divisor (`x % 2`), which also removes the int64 division instruction.
//    Every non-integral or out-of-range pair falls back to `std::fmod`, so
//    the observable result is identical for *all* doubles: negative operands,
//    -0.0, NaN, +-Infinity, fractionals, |x| >= 2^53, y = 0, +-Inf.
//
// 2. The int64 fast path for `number` (D64 item 3). `toInt64Exact` is the
//    entry guard; `iadd`/`isub`/`imul`/`imulConst`/`idiv`/`imod`/`ineg`/
//    `preInc`/... are the guarded int64 operations codegen emits inside a
//    fast body. The invariant is `|v| <= kIntLimit` (2^53) for every value in
//    the region, which makes every int64 value exactly representable as a
//    double and every double operation inside the region exact - so the int64
//    body and the ordinary double body compute identical values. Each
//    operation checks that the exact result still satisfies the invariant and
//    calls `intBail()` the moment it cannot; the generated double wrapper
//    catches `IntBail` and re-runs the ordinary double body, so a bail is
//    never observable.
//
// WHY THE `%` FAST PATH IS EXACT (the full correctness argument):
//   * IEEE-754 `fmod` (as required of `std::fmod`) returns the exact
//     mathematical remainder `a - trunc(a/b)*b` - it performs no rounding -
//     and that value is always exactly representable as a double.
//   * C++ `%` on int64 truncates toward zero (like `fmod`) and takes the
//     dividend's sign.
//   * If `a` and `b` are integral doubles with `|a|,|b| < 2^63`, then
//     `ai = (int64)a` and `bi = (int64)b` are exact and `ai % bi` IS the
//     mathematical remainder, so `(double)(ai % bi) == std::fmod(a, b)` as
//     soon as that remainder is representable - and it is, by the same
//     argument that makes `fmod` exact: `|r| < |b|`; if `|a| < |b|` then
//     `r = a` is a double; else if `|b| <= 2^53` then `r` is an integer below
//     2^53; else `b` is a multiple of `2^(eb-52)` (`eb = floor(log2 |b|)`),
//     `a` is a multiple of the same power (its exponent is >= `eb`), so `r`
//     is a multiple of it with `|r| < 2^(eb+1)`: fewer than 53 significant
//     bits, i.e. exactly representable.
//   * A zero remainder takes the dividend's sign (`copysign`), matching
//     `fmod(-4, 2) == -0.0` (the fast path must not return `+0.0`).
//   * `b == 0` (including `-0.0`), NaN and any out-of-range operand stay on
//     the `std::fmod` path: the range compares are false for NaN, and the
//     explicit `bi != 0` test keeps `int64 % 0` from ever being evaluated.
//
// WHY THE INT64 REGION IS EXACT (the invariant, operation by operation):
//   * Entry (`toInt64Exact`) accepts only an integral double in `[-2^53,2^53]`
//     and rejects `-0.0` (the sign of zero is not representable in int64 and
//     `-0.0` cannot be created inside the region: `ineg(0)`, `imod` with a
//     zero remainder of a negative dividend, `idiv` of a zero dividend by a
//     negative divisor, and a negative constant product of zero all bail
//     instead of returning a zero).
//   * `iadd`/`isub`: `|a|,|b| <= 2^53` means the int64 result is exact; the
//     result is returned only while `|r| <= 2^53`, and a zero result is
//     `+0.0` in both domains (`x + -x` and `x - x` are `+0.0` in IEEE
//     round-to-nearest).
//   * `imul`/`imulConst<K>`: the product is computed only after proving
//     `|a*b| <= 2^53`, so it is exact in both domains; a zero product is
//     returned only when its double sign is `+0.0`.
//   * `idiv`: non-zero only for exact quotients (`a % b == 0`); an exact
//     quotient `q` with `1 <= |q| <= |a| <= 2^53` has the same sign in both
//     domains, and a zero dividend over a negative divisor bails.
//   * `imod`: `|r| < |b| <= 2^53` (representable); sign follows the dividend,
//     matching `fmod`; a zero remainder of a negative dividend bails.
//   * `ineg`: `-0` in int64 is `+0.0` whereas the double result would be
//     `-0.0`, so `ineg(0)` bails.
//   * `preInc`/`postInc`/`preDec`/`postDec` keep `|v| <= 2^53` by checking
//     the bound before the increment.
//
// TWO OPTIMIZED CORNERS (both narrow, both argued above):
//   * `imodCmp(a, b)` drops the zero-sign bail. It is exact for every
//     *magnitude* and differs from `imod` only by returning `+0` instead of
//     `-0` for a negative dividend with a zero remainder. IEEE comparison
//     treats `-0` and `+0` as equal, so the call is only valid where the
//     result is an operand of a comparison and nothing else (codegen emits it
//     exactly there), and the sign of that zero is then unobservable.
//   * `pass(v)` re-checks a value whose bound check codegen deferred. The
//     deferred write may temporarily leave `[-2^53, 2^53]`, so the value is
//     only ever read through `pass`, which restores the invariant or bails
//     before the value can be observed (returned, compared, printed). A
//     deferred write is only emitted for operations that cannot overflow
//     int64 given in-range operands, so the value stays well-defined.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>

// The int64 fast body is deliberately not inlined into its double wrapper:
// GCC otherwise duplicates the whole checked loop into the wrapper and bloats
// the hot function to save one call.
#if defined(_MSC_VER)
#define CP3_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define CP3_NOINLINE __attribute__((noinline))
#else
#define CP3_NOINLINE
#endif

namespace cp3::runtime::arith {

// 2^53, as a double and as an int64: the largest magnitude at which every
// integer is exactly representable in both domains (and the int64<->double
// conversions used below are exactly round-tripping inside it).
inline constexpr std::int64_t kIntLimit = 9007199254740992LL;
inline constexpr double kIntLimitD = 9007199254740992.0;
inline constexpr double kTwo63 = 9223372036854775808.0;

// -----------------------------------------------------------------------------
// `%` - the double-domain operator (D64 item 2)
// -----------------------------------------------------------------------------

// `a % b`, bit-exact with `std::fmod(a, b)` (proof in the file header).
inline double mod(double a, double b) {
    if (a > -kTwo63 && a < kTwo63 && b > -kTwo63 && b < kTwo63) {
        const std::int64_t ai = static_cast<std::int64_t>(a);
        const std::int64_t bi = static_cast<std::int64_t>(b);
        // `(double)ai == a` also proves `a` integral: the conversion truncates.
        if (bi != 0 && static_cast<double>(ai) == a && static_cast<double>(bi) == b) {
            const std::int64_t r = ai % bi;
            return r == 0 ? std::copysign(0.0, a) : static_cast<double>(r);
        }
    }
    return std::fmod(a, b);
}

// `a % K` for an integral literal divisor K (codegen specializes `x % 2` and
// friends). |K| <= 2^53 and K != 0 are codegen guarantees; `ai % K` is a
// constant-divisor remainder, so no divide instruction is emitted.
template <std::int64_t K>
inline double modConst(double a) {
    static_assert(K != 0, "modConst: the divisor must be non-zero");
    static_assert(K >= -kIntLimit && K <= kIntLimit,
                  "modConst: the divisor must fit the exact-integer range");
    if (a > -kTwo63 && a < kTwo63) {
        const std::int64_t ai = static_cast<std::int64_t>(a);
        if (static_cast<double>(ai) == a) {
            const std::int64_t r = ai % K;
            return r == 0 ? std::copysign(0.0, a) : static_cast<double>(r);
        }
    }
    return std::fmod(a, static_cast<double>(K));
}

// -----------------------------------------------------------------------------
// The int64 fast path (D64 item 3)
// -----------------------------------------------------------------------------

// The control-flow signal of a fast body: thrown by a guard the moment the
// exact-integer invariant cannot be upheld, caught by the generated wrapper,
// which then re-runs the ordinary double body. Never escapes user code.
struct IntBail {};

[[noreturn]] inline void intBail() { throw IntBail{}; }

// Entry guard: `v` is an integral double in [-2^53, 2^53] (so `out` is the
// exact value) and not `-0.0`. Everything else takes the double body.
inline bool toInt64Exact(double v, std::int64_t& out) {
    if (v >= -kIntLimitD && v <= kIntLimitD) {
        const std::int64_t i = static_cast<std::int64_t>(v);
        if (static_cast<double>(i) == v && !(v == 0.0 && std::signbit(v))) {
            out = i;
            return true;
        }
    }
    return false;
}

// Guarded int64 arithmetic. Precondition (codegen invariant): every operand
// satisfies `|v| <= kIntLimit`; every call either returns the exact result
// (still within the invariant) or bails.
inline std::int64_t iadd(std::int64_t a, std::int64_t b) {
    const std::int64_t r = a + b;  // |a|,|b| <= 2^53: no int64 overflow
    if (r > kIntLimit || r < -kIntLimit) intBail();
    return r;
}

inline std::int64_t isub(std::int64_t a, std::int64_t b) {
    const std::int64_t r = a - b;
    if (r > kIntLimit || r < -kIntLimit) intBail();
    return r;
}

inline std::int64_t ineg(std::int64_t a) {
    if (a == 0) intBail();  // the double counterpart would be -0.0
    return -a;
}

// `K * a` for an integral literal K; the bound check makes the product exact.
template <std::int64_t K>
inline std::int64_t imulConst(std::int64_t a) {
    static_assert(K != 0, "imulConst: the constant factor must be non-zero");
    constexpr std::int64_t kLim = kIntLimit / (K < 0 ? -K : K);
    if (a > kLim || a < -kLim) intBail();
    const std::int64_t r = K * a;
    if (K < 0 && r == 0) intBail();  // the double product would be -0.0
    return r;
}

inline std::int64_t imul(std::int64_t a, std::int64_t b) {
    // |a|,|b| <= 2^53, so the magnitudes cannot overflow int64 and the
    // unsigned comparison decides `|a*b| <= kIntLimit` exactly.
    const std::uint64_t ua = static_cast<std::uint64_t>(a < 0 ? -a : a);
    const std::uint64_t ub = static_cast<std::uint64_t>(b < 0 ? -b : b);
    if (ua != 0 && ub > static_cast<std::uint64_t>(kIntLimit) / ua) intBail();
    const std::int64_t r = a * b;
    if (r == 0 && ((a < 0) != (b < 0))) intBail();  // would be -0.0 in double
    return r;
}

inline std::int64_t idiv(std::int64_t a, std::int64_t b) {
    if (b == 0) intBail();          // the double result would be +-Infinity/NaN
    if (a % b != 0) intBail();      // a fractional quotient stays in the double domain
    if (a == 0 && b < 0) intBail(); // the double quotient would be -0.0
    return a / b;                   // |a| <= 2^53: INT64_MIN / -1 cannot occur
}

inline std::int64_t imod(std::int64_t a, std::int64_t b) {
    if (b == 0) intBail();  // the double result would be NaN
    const std::int64_t r = a % b;
    if (r == 0 && a < 0) intBail();  // the double remainder would be -0.0
    return r;
}

// `a % b` where the caller has established that the result is compared with
// something: the sign of a zero remainder is then unobservable (see the header),
// so the zero-sign bail is dropped and only the divisor must be non-zero.
inline std::int64_t imodCmp(std::int64_t a, std::int64_t b) {
    if (b == 0) intBail();
    return a % b;
}

// Restores the invariant for a value whose bound check was deferred (see the
// header): used as `(pass(v), v)` at every read of such a value.
inline std::int64_t pass(std::int64_t v) {
    if (v > kIntLimit || v < -kIntLimit) intBail();
    return v;
}

inline std::int64_t preInc(std::int64_t& v) {
    if (v >= kIntLimit) intBail();
    return ++v;
}

inline std::int64_t postInc(std::int64_t& v) {
    if (v >= kIntLimit) intBail();
    return v++;
}

inline std::int64_t preDec(std::int64_t& v) {
    if (v <= -kIntLimit) intBail();
    return --v;
}

inline std::int64_t postDec(std::int64_t& v) {
    if (v <= -kIntLimit) intBail();
    return v--;
}

}  // namespace cp3::runtime::arith
