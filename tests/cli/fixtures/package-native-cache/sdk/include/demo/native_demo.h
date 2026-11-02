// =============================================================================
// tests/cli/fixtures/native-build-sdk/include/demo/native_demo.h
//
// The compiler-half fixture: the same beta-one surface as the native-install
// fixture, but every function is defined `inline`, so a program that calls it
// links without a separately built archive. The binding's recorded library is
// the one the build integration puts on the link line.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>

namespace demo {

enum class Color { Red, Green, Blue };

// Primitives.
inline int add(int a, int b) { return a + b; }
inline double scale(double value, float factor) {
    return value * static_cast<double>(factor);
}
inline bool is_ready() { return true; }

// A std::string result and a `const char*` result (the cp3 `string` mapping).
inline std::string greet(const std::string& name) { return "hello " + name; }
inline const char* version() { return "1.0.0"; }
inline void log_message(const char* text) { (void)text; }

// A byte pointer immediately followed by its length (the cp3 `Buffer`).
inline int checksum(const unsigned char* data, std::size_t length) {
    int total = 0;
    for (std::size_t i = 0; i < length; ++i) total += data[i];
    return total;
}

// A number pointer immediately followed by its length (the cp3 `number[]`).
inline double sum_values(const double* values, int count) {
    double total = 0.0;
    for (int i = 0; i < count; ++i) total += values[i];
    return total;
}

}  // namespace demo
