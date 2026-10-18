// =============================================================================
// src/runtime/tests/arith_test.cpp - unit driver for cp3/core/arith.hpp (D64).
//
// Covers the two contracts of the header:
//   * `mod`/`modConst<K>` are bit-exact with `std::fmod` for a curated edge
//     matrix (negative operands, +-0.0, NaN, +-Infinity, fractionals,
//     |x| >= 2^53, y = 0) plus randomized integral/fractional/bit-pattern
//     pairs;
//   * the int64 guard helpers return the exact value where the invariant
//     holds and bail exactly where it cannot (bounds, zero divisors,
//     non-exact quotients, and every `-0.0`-producing zero).
// =============================================================================
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "cp3/core/arith.hpp"
#include "harness.hpp"

namespace {

using cp3::runtime::arith::IntBail;
using cp3::runtime::arith::kIntLimit;

bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }

// NaN compares by payload, so NaN results are checked by classification only.
void expectSame(double actual, double expected) {
    if (std::isnan(expected)) {
        CP3_CHECK(std::isnan(actual));
        return;
    }
    CP3_CHECK(sameBits(actual, expected));
}

std::vector<double> edgeValues() {
    const double two53 = 9007199254740992.0;
    const double two63 = 9223372036854775808.0;
    return {
        0.0, -0.0, 1.0, -1.0, 2.0, -2.0, 3.0, -3.0, 4.0, -4.0, 5.0, -5.0,
        0.5, -0.5, 2.5, -2.5, 7.0, -7.0, 1e15, -1e15, 1e16, 1000000007.0,
        two53, -two53, two53 + 2.0, two53 - 2.0, two53 * 2.0, -two53 * 2.0,
        two53 * 128.0, -two53 * 128.0, two63, -two63, two63 + 2048.0,
        1e300, -1e300, std::numeric_limits<double>::max(),
        std::numeric_limits<double>::min(), 1e-300, 2.0e-100,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
    };
}

void testModEdges() {
    const std::vector<double> values = edgeValues();
    for (double a : values) {
        for (double b : values) {
            expectSame(cp3::runtime::arith::mod(a, b), std::fmod(a, b));
        }
    }
}

void testModConstEdges() {
    const std::vector<double> values = edgeValues();
    for (double a : values) {
        expectSame(cp3::runtime::arith::modConst<2>(a), std::fmod(a, 2.0));
        expectSame(cp3::runtime::arith::modConst<3>(a), std::fmod(a, 3.0));
        expectSame(cp3::runtime::arith::modConst<-2>(a), std::fmod(a, -2.0));
        expectSame(cp3::runtime::arith::modConst<1>(a), std::fmod(a, 1.0));
        expectSame(cp3::runtime::arith::modConst<-1>(a), std::fmod(a, -1.0));
        expectSame(cp3::runtime::arith::modConst<7>(a), std::fmod(a, 7.0));
        expectSame(cp3::runtime::arith::modConst<9007199254740992LL>(a),
                   std::fmod(a, 9007199254740992.0));
    }
}

// The remainder of two integral doubles below 2^63 is an integer; the fast
// path must reproduce `fmod` exactly, including the sign of a zero result.
void testModZeroSign() {
    CP3_CHECK(!std::signbit(cp3::runtime::arith::mod(4.0, 2.0)));
    CP3_CHECK(std::signbit(cp3::runtime::arith::mod(-4.0, 2.0)));
    CP3_CHECK(std::signbit(cp3::runtime::arith::mod(-4.0, -2.0)));
    CP3_CHECK(!std::signbit(cp3::runtime::arith::mod(0.0, 3.0)));
    CP3_CHECK(std::signbit(cp3::runtime::arith::mod(-0.0, 3.0)));
    expectSame(cp3::runtime::arith::mod(-0.0, 3.0), std::fmod(-0.0, 3.0));
    CP3_CHECK(std::signbit(cp3::runtime::arith::modConst<2>(-6.0)));
    CP3_CHECK(!std::signbit(cp3::runtime::arith::modConst<2>(6.0)));
}

double randomOperand(std::mt19937_64& rng) {
    switch (rng() % 4) {
        case 0: {
            // A random bit pattern with a finite, moderate exponent.
            const std::uint64_t bits = rng();
            const double v = std::bit_cast<double>(bits & 0x800FFFFFFFFFFFFFULL);
            return (bits & 1) != 0 ? -v : v;
        }
        case 1:
            return static_cast<double>(static_cast<std::int64_t>(rng() >> 11)) *
                   (rng() % 2 == 0 ? 1.0 : -1.0);
        case 2:
            return static_cast<double>(rng() % 1000) - 500.0;
        default:
            return static_cast<double>(rng() % 1000) - 500.5;
    }
}

void testModRandom() {
    std::mt19937_64 rng(0x637033ULL);
    for (int i = 0; i < 400000; ++i) {
        const double a = randomOperand(rng);
        const double b = randomOperand(rng);
        expectSame(cp3::runtime::arith::mod(a, b), std::fmod(a, b));
        if (i % 4 == 0) {
            expectSame(cp3::runtime::arith::modConst<2>(a), std::fmod(a, 2.0));
        }
    }
}

// Runs a guarded int64 operation and reports whether it bailed.
struct Outcome {
    bool bailed = false;
    std::int64_t value = 0;
};

template <class F>
Outcome eval64(F fn) {
    try {
        return Outcome{false, fn()};
    } catch (const IntBail&) {
        return Outcome{true, 0};
    }
}

void testToInt64Exact() {
    std::int64_t out = 123;
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(0.0, out));
    CP3_CHECK_EQ(out, 0);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(-0.0, out) == false);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(9007199254740992.0, out));
    CP3_CHECK_EQ(out, 9007199254740992LL);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(-9007199254740992.0, out));
    // 9007199254740993.0 (2^53 + 1) is not a double: the literal rounds to
    // 2^53 and is accepted. 2^53 + 2 is the first double above the limit.
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(9007199254740993.0, out));
    CP3_CHECK_EQ(out, 9007199254740992LL);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(9007199254740994.0, out) == false);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(0.5, out) == false);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(1e300, out) == false);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(std::nan(""), out) == false);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(
                  std::numeric_limits<double>::infinity(), out) == false);
    CP3_CHECK(cp3::runtime::arith::toInt64Exact(-7.0, out));
    CP3_CHECK_EQ(out, -7);
}

void testAddSubNegate() {
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::iadd(2, 3); }).value, 5);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::iadd(-1, 1); }).value, 0);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::iadd(kIntLimit, 1); }).bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::iadd(-kIntLimit, -1); }).bailed);
    CP3_CHECK(!eval64([] { return cp3::runtime::arith::iadd(kIntLimit, 0); }).bailed);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::isub(10, 3); }).value, 7);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::isub(-kIntLimit, 1); }).bailed);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::ineg(-9); }).value, 9);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::ineg(0); }).bailed);
}

void testMul() {
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imul(3, 7); }).value, 21);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imul(-3, 7); }).value, -21);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imul(0, -7); }).bailed, true);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imul(0, 7); }).value, 0);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::imul(kIntLimit, 2); }).bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::imul(2, kIntLimit); }).bailed);
    CP3_CHECK(!eval64([] { return cp3::runtime::arith::imul(kIntLimit, 1); }).bailed);
    CP3_CHECK_EQ(
        eval64([] { return cp3::runtime::arith::imulConst<3>(56991483520LL); }).value,
        170974450560LL);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::imulConst<3>(kIntLimit); }).bailed);
    CP3_CHECK(!eval64([] { return cp3::runtime::arith::imulConst<3>(3002399751580330LL); })
                   .bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::imulConst<-1>(0); }).bailed);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imulConst<-1>(-8); }).value, 8);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imulConst<2>(3); }).value, 6);
}

void testDivMod() {
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::idiv(6, 2); }).value, 3);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::idiv(-6, 2); }).value, -3);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::idiv(7, 2); }).bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::idiv(1, 0); }).bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::idiv(0, -3); }).bailed);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::idiv(0, 3); }).value, 0);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imod(7, 2); }).value, 1);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imod(-7, 2); }).value, -1);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imod(6, 2); }).value, 0);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::imod(-6, 2); }).bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::imod(5, 0); }).bailed);
}

void testIncDec() {
    CP3_CHECK_EQ(eval64([] {
                     std::int64_t v = 5;
                     return cp3::runtime::arith::preInc(v);
                 }).value,
                 6);
    CP3_CHECK_EQ(eval64([] {
                     std::int64_t v = 5;
                     return cp3::runtime::arith::postInc(v);
                 }).value,
                 5);
    CP3_CHECK(eval64([] {
                  std::int64_t v = kIntLimit;
                  return cp3::runtime::arith::preInc(v);
              }).bailed);
    CP3_CHECK(eval64([] {
                  std::int64_t v = -kIntLimit;
                  return cp3::runtime::arith::preDec(v);
              }).bailed);
    CP3_CHECK_EQ(eval64([] {
                     std::int64_t v = -3;
                     return cp3::runtime::arith::postDec(v);
                 }).value,
                 -3);
}

void testDeferredCorners() {
    // `imodCmp` is `imod` without the -0.0 bail: same magnitudes and signs for
    // every non-zero remainder, and it only differs by returning +0 where the
    // double result was -0.0 - which a comparison cannot tell apart.
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imodCmp(7, 2); }).value, 1);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imodCmp(-7, 2); }).value, -1);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imodCmp(6, -4); }).value, 2);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::imodCmp(-6, 2); }).value, 0);
    CP3_CHECK(!eval64([] { return cp3::runtime::arith::imodCmp(-6, 2); }).bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::imodCmp(5, 0); }).bailed);
    for (std::int64_t a : {-9LL, -1LL, 0LL, 1LL, 9LL}) {
        for (std::int64_t b : {-4LL, -3LL, 3LL, 4LL}) {
            const double want = std::fmod(static_cast<double>(a), static_cast<double>(b));
            const auto got = eval64([&] { return cp3::runtime::arith::imodCmp(a, b); });
            CP3_CHECK(!got.bailed);
            CP3_CHECK_EQ(got.value, static_cast<std::int64_t>(want));
        }
    }
    // `pass` restores the invariant a deferred write suspended, and bails
    // exactly where the value has left [-2^53, 2^53].
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::pass(0); }).value, 0);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::pass(kIntLimit); }).value, kIntLimit);
    CP3_CHECK_EQ(eval64([] { return cp3::runtime::arith::pass(-kIntLimit); }).value, -kIntLimit);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::pass(kIntLimit + 1); }).bailed);
    CP3_CHECK(eval64([] { return cp3::runtime::arith::pass(-kIntLimit - 1); }).bailed);
}

}  // namespace

int main() {
    testModEdges();
    testModConstEdges();
    testModZeroSign();
    testModRandom();
    testToInt64Exact();
    testAddSubNegate();
    testMul();
    testDivMod();
    testIncDec();
    testDeferredCorners();
    return cp3test::summary("arith");
}
