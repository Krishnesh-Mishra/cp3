// fast_float_smoke.cpp -- smoke for vendored fast_float
// (vendor/third_party/fast_float, header-only).
//
// Correctly-rounded decimal -> binary parsing via fast_float::from_chars
// (the call the cp3 std/json, std/env and std/url number parsers will use).
// Expected values are the same doubles the C++ literals denote: a correctly
// rounded parser must produce bit-identical results. Also checks the error
// contract (ec set + ptr at start on no-conversion, ptr at the first
// unconsumed character on trailing garbage) and full-consumption parsing.
//
// Build (see run_smokes.ps1 / docs/design/vendor-smoke.md) -- header-only,
// no archive to link:
//   g++ -std=c++20 -O2 -I vendor/third_party/fast_float/include \
//       vendor/third_party/smokes/fast_float_smoke.cpp -o fast_float_smoke.exe
//
// Exit 0 on success, 1 on any mismatch. No input, no files, deterministic.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include <fast_float/fast_float.h>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Parse `s` as a double; must succeed and consume the whole string.
void check_parse(const char* label, const std::string& s, double expected) {
    double value = 0.0;
    const auto r = fast_float::from_chars(s.data(), s.data() + s.size(), value);
    const bool ok = r.ec == std::errc() &&
                    r.ptr == s.data() + s.size() && value == expected;
    std::printf("  %s: got %.17g expected %.17g ec=%d consumed=%td/%td\n", label,
                value, expected, static_cast<int>(r.ec),
                static_cast<std::ptrdiff_t>(r.ptr - s.data()),
                static_cast<std::ptrdiff_t>(s.size()));
    check(ok, label);
}
}  // namespace

int main() {
    std::printf("fast_float smoke (from_chars, double)\n");

    check_parse("plain integer", "42", 42.0);
    check_parse("fraction", "3.14159", 3.14159);
    check_parse("negative fraction", "-0.5", -0.5);
    check_parse("scientific notation", "1.5e10", 1.5e10);
    check_parse("negative exponent", "-2.5E-3", -2.5e-3);
    check_parse("leading zero", "007.25", 7.25);
    check_parse("plus on exponent", "1e+5", 1e5);

    // float overload (the other consumer-visible instantiation).
    {
        float value = 0.f;
        const char* s = "0.1";
        const auto r = fast_float::from_chars(s, s + 3, value);
        check(r.ec == std::errc() && r.ptr == s + 3 && value == 0.1f,
              "float parse of 0.1");
    }

    // No conversion: ec set, ptr untouched.
    {
        double value = 123.0;
        const char* s = "not-a-number";
        const auto r = fast_float::from_chars(s, s + 12, value);
        check(r.ec != std::errc() && r.ptr == s, "invalid input rejected");
    }

    // Trailing garbage: value parsed, ptr stops at the first bad char.
    {
        double value = 0.0;
        const char* s = "123abc";
        const auto r = fast_float::from_chars(s, s + 6, value);
        check(r.ec == std::errc() && value == 123.0 && r.ptr == s + 3,
              "trailing garbage stops ptr");
    }

    // Empty input: no conversion.
    {
        double value = 0.0;
        const char* s = "";
        const auto r = fast_float::from_chars(s, s, value);
        check(r.ec != std::errc() && r.ptr == s, "empty input rejected");
    }

    std::printf(failures ? "fast_float smoke: %d failure(s)\n" : "fast_float smoke: OK\n",
                failures);
    return failures ? 1 : 0;
}
