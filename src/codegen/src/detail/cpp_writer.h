// =============================================================================
// src/codegen/src/detail/cpp_writer.h - PRIVATE generated-C++ text helpers.
//
// The tiny emit-buffer utilities codegen shares: indented line emission plus the
// literal/spelling helpers (`cppStringLiteral`, `numberLiteralText`,
// `joinStrings`). These produce bytes that appear verbatim in the generated C++
// and are therefore behavior-frozen (STYLE.md §8: cpp_writer).
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3 {
namespace detail {

// Appends `text` to `out` indented `indent` levels (4 spaces each) plus '\n'.
void line(std::string& out, int indent, const std::string& text);

// A C++ string literal for `value` (UTF-8 bytes preserved; controls escaped).
std::string cppStringLiteral(const std::string& value);

// Shortest round-tripping C++ literal for a double (always a floating literal).
std::string numberLiteralText(double value);

// `parts` joined by `sep`.
std::string joinStrings(const std::vector<std::string>& parts,
                        const std::string& sep);

}  // namespace detail
}  // namespace cp3
