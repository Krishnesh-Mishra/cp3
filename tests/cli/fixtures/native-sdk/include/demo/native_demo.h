// =============================================================================
// tests/cli/fixtures/native-sdk/include/demo/native_demo.h
//
// Deterministic libclang fixture for `native-install`: primitives, strings,
// a pointer+length Buffer, a number array, an enum, a plain struct, a simple
// class, and declarations beta one must reject by name.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>

namespace demo {

enum class Color { Red, Green, Blue };

struct Point {
    double x;
    double y;

    // Rejected: operator overloading is not supported in beta one.
    Point operator+(const Point& other) const;
};

int add(int a, int b);
double scale(double value, float factor);
bool is_ready();
std::string greet(const std::string& name);
const char* version();
void log_message(const char* text);

// Buffer: a const byte pointer immediately followed by its length.
int checksum(const unsigned char* data, std::size_t length);

// number[]: a const arithmetic pointer immediately followed by its length.
double sum_values(const double* values, int count);

class Counter {
public:
    int value() const;
    void increment(int by);
    static Counter create();

private:
    int value_;
};

#ifdef DEMO_FEATURE
int feature_enabled();
#endif

// Rejected: cp3 Buffer values are immutable, so a non-const output pointer has
// no beta-one representation. The command must name it in `rejected`.
void fill_bytes(unsigned char* bytes, std::size_t length);

// Rejected: overload sets are not supported in beta one.
int overloaded(int value);
double overloaded(double value);

// Rejected: variadic functions are not supported in beta one.
int sum_all(int count, ...);

// Rejected: templates are not supported in beta one.
template <typename T>
T identity(T value);

}  // namespace demo
