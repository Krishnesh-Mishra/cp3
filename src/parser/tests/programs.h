// =============================================================================
// src/parser/tests/programs.h - built-in representative programs.
// =============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cp3test {

// (name, source) pair, matching the driver's original `cases` vector.
using Case = std::pair<std::string, std::string>;

// Appends the built-in representative programs (module items, declarations,
// classes, statements, expressions, types, error cases, edge cases).
void builtinCases(std::vector<Case>& out);

}  // namespace cp3test
