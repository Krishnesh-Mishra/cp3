// =============================================================================
// src/runtime/tests/format_test.cpp - D9 number formatting and D49 console
// rendering (cp3/core/runtime.hpp toString + cp3/std/console.hpp format*).
//
// Build/run by direct compile (see the runtime report):
//   clang++ -std=c++20 -I include -I tests format_test.cpp ../../build.../libcp3_runtime.a
// or through `-DCP3_RUNTIME_TESTS=ON` (target `format_test`).
// =============================================================================
#include <string>
#include <vector>

#include "cp3/std/console.hpp"
#include "cp3/core/runtime.hpp"
#include "harness.hpp"

using cp3::runtime::formatObject;
using cp3::runtime::formatValue;
using cp3::runtime::formatNested;
using cp3::runtime::quoteString;
using cp3::runtime::toString;
using cp3::runtime::undefined;

namespace {

void checkNumbers() {
    // DECISIONS D9: ECMAScript Number::toString(value, 10), exactly.
    CP3_CHECK_EQ(toString(0.0), std::string("0"));
    CP3_CHECK_EQ(toString(-0.0), std::string("0"));
    CP3_CHECK_EQ(toString(1.0), std::string("1"));
    CP3_CHECK_EQ(toString(-1.0), std::string("-1"));
    CP3_CHECK_EQ(toString(1.5), std::string("1.5"));
    CP3_CHECK_EQ(toString(-1.5), std::string("-1.5"));
    CP3_CHECK_EQ(toString(100.0), std::string("100"));
    CP3_CHECK_EQ(toString(123.456), std::string("123.456"));
    CP3_CHECK_EQ(toString(0.5), std::string("0.5"));
    CP3_CHECK_EQ(toString(0.001), std::string("0.001"));
    CP3_CHECK_EQ(toString(1e-6), std::string("0.000001"));
    CP3_CHECK_EQ(toString(1e-7), std::string("1e-7"));
    CP3_CHECK_EQ(toString(1e21), std::string("1e+21"));
    CP3_CHECK_EQ(toString(1.2345678901234568e20), std::string("123456789012345680000"));
    CP3_CHECK_EQ(toString(5e-324), std::string("5e-324"));
    CP3_CHECK_EQ(toString(1.0 / 3.0), std::string("0.3333333333333333"));

    const double inf = 1.0 / 0.0;
    const double nan = 0.0 / 0.0;
    CP3_CHECK_EQ(toString(inf), std::string("Infinity"));
    CP3_CHECK_EQ(toString(-inf), std::string("-Infinity"));
    CP3_CHECK_EQ(toString(nan), std::string("NaN"));
}

void checkScalars() {
    CP3_CHECK_EQ(toString(true), std::string("true"));
    CP3_CHECK_EQ(toString(false), std::string("false"));
    CP3_CHECK_EQ(toString(nullptr), std::string("null"));
    CP3_CHECK_EQ(toString(undefined), std::string("undefined"));
}

void checkNestedStrings() {
    CP3_CHECK_EQ(formatValue(std::string("hi")), std::string("hi"));
    CP3_CHECK_EQ(formatNested(std::string("hi")), std::string("'hi'"));
    CP3_CHECK_EQ(quoteString("a'b"), std::string("'a\\'b'"));
    CP3_CHECK_EQ(quoteString("a\\b"), std::string("'a\\\\b'"));
    CP3_CHECK_EQ(quoteString("a\nb"), std::string("'a\\nb'"));
}

void checkArraysObjects() {
    CP3_CHECK_EQ(formatValue(std::vector<double>{1.0, 2.0, 3.0}), std::string("[1, 2, 3]"));
    CP3_CHECK_EQ(formatValue(std::vector<double>{}), std::string("[]"));
    CP3_CHECK_EQ(formatValue(std::vector<std::string>{"a", "b"}), std::string("['a', 'b']"));
    CP3_CHECK_EQ(formatValue(std::vector<double>{1.5, 2.0}), std::string("[1.5, 2]"));

    CP3_CHECK_EQ(formatObject({{"a", "1"}, {"b", "2"}}), std::string("{ a: 1, b: 2 }"));
    CP3_CHECK_EQ(formatObject({}), std::string("{}"));
}

void checkConsoleJoin() {
    // `console.log(a, b, c)` renders each argument then joins with single spaces.
    const std::string joined =
        cp3::runtime::console::detail::renderAll(1.0, std::string("hi"), true, nullptr);
    CP3_CHECK_EQ(joined, std::string("1 hi true null"));
}

}  // namespace

int main() {
    checkNumbers();
    checkScalars();
    checkNestedStrings();
    checkArraysObjects();
    checkConsoleJoin();
    return cp3test::summary("format_test");
}
