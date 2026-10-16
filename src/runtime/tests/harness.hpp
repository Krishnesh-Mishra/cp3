// =============================================================================
// src/runtime/tests/harness.hpp — a minimal, dependency-free assertion harness
// for the runtime unit drivers (STYLE.md §7: no third-party framework).
//
// Usage: call CP3_CHECK / CP3_CHECK_EQ, then `return cp3test::summary("name");`
// from `main`. A non-zero summary exit code fails the driver.
// =============================================================================
#pragma once

#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace cp3test {

inline int& checkCount() {
    static int count = 0;
    return count;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

// Render a value for a failure message: uses `operator<<` when available,
// otherwise a placeholder. Never throws.
template <class T>
std::string toText(const T& value) {
    if constexpr (requires { std::declval<std::ostream&>() << value; }) {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    } else {
        return std::string("<value>");
    }
}

inline void checkTrue(bool passed, const char* expr, const char* file, int line) {
    ++checkCount();
    if (passed) {
        return;
    }
    ++failureCount();
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
}

template <class A, class B>
void checkEq(const A& actual, const B& expected, const char* expr, const char* file,
             int line) {
    ++checkCount();
    if (actual == expected) {
        return;
    }
    ++failureCount();
    std::fprintf(stderr, "FAIL %s:%d: %s\n  actual:   %s\n  expected: %s\n", file, line, expr,
                 toText(actual).c_str(), toText(expected).c_str());
}

inline int summary(const char* name) {
    std::fprintf(stderr, "%s: %d checks, %d failure(s)\n", name, checkCount(), failureCount());
    return failureCount() == 0 ? 0 : 1;
}

}  // namespace cp3test

#define CP3_CHECK(cond) ::cp3test::checkTrue(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

#define CP3_CHECK_EQ(actual, expected) \
    ::cp3test::checkEq((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
