// simdutf_smoke.cpp -- smoke for vendored simdutf (third_party/simdutf).
//
// Exercises simdutf::get_active_implementation()->validate_utf8() (v6.1.0
// API, see third_party/simdutf/include/simdutf/implementation.h) and
// validate_utf8_with_errors() on known-valid and known-invalid inputs.
// The valid/invalid verdicts are fixed by the UTF-8 specification, not by
// upstream test data, so this smoke stays pinned-version independent.
//
// Build (see run_smokes.ps1 / docs/design/vendor-smoke.md):
//   g++ -std=c++20 -O2 -I third_party/simdutf/include third_party/smokes/simdutf_smoke.cpp \
//       -L third_party/simdutf/lib/windows-x64 -lsimdutf -o simdutf_smoke.exe
//
// Exit 0 on success, 1 on any mismatch. No input, no files, deterministic.

#include <cstddef>
#include <cstdio>
#include <string>

#include "simdutf.h"

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}  // namespace

int main() {
    std::printf("simdutf smoke (validate_utf8)\n");

    const simdutf::implementation* impl = simdutf::get_active_implementation();
    check(impl != nullptr, "active implementation present");
    if (!impl) return 1;
    std::printf("  active implementation: %s\n", impl->name().c_str());

    const auto valid = [&](const char* label, const std::string& s) {
        const bool ok = impl->validate_utf8(s.data(), s.size());
        std::printf("  %s: validate_utf8=%s\n", label, ok ? "true" : "false");
        check(ok, label);
        const simdutf::result r =
            impl->validate_utf8_with_errors(s.data(), s.size());
        check(r.error == simdutf::error_code::SUCCESS &&
                  r.count == s.size(),
              "  ^ validate_utf8_with_errors agrees (SUCCESS, count==len)");
    };
    const auto invalid = [&](const char* label, const std::string& s) {
        const bool ok = impl->validate_utf8(s.data(), s.size());
        std::printf("  %s: validate_utf8=%s\n", label, ok ? "true" : "false");
        check(!ok, label);
        const simdutf::result r =
            impl->validate_utf8_with_errors(s.data(), s.size());
        check(r.error != simdutf::error_code::SUCCESS, "  ^ with_errors errors");
    };

    // Valid.
    valid("empty string", "");
    valid("plain ASCII", "hello world");
    valid("2-byte sequence (U+00E9, C3 A9)", "\xC3\xA9");          // é
    valid("3-byte sequence (U+20AC, E2 82 AC)", "\xE2\x82\xAC");  // €
    valid("4-byte sequence (U+1F600, F0 9F 98 80)",
          "\xF0\x9F\x98\x80");                                    // 😀
    valid("mixed ASCII + multibyte", "a\xC3\xA9z\xE2\x82\xAC");

    // Invalid.
    invalid("lone 0xFF byte", std::string("\xFF", 1));
    invalid("bad continuation (C3 28)", std::string("\xC3\x28", 2));
    invalid("truncated 3-byte sequence (E2 82)", std::string("\xE2\x82", 2));
    invalid("bare continuation byte (80)", std::string("\x80", 1));
    invalid("overlong encoding (C0 AF)", std::string("\xC0\xAF", 2));

    std::printf(failures ? "simdutf smoke: %d failure(s)\n" : "simdutf smoke: OK\n",
                failures);
    return failures ? 1 : 0;
}
