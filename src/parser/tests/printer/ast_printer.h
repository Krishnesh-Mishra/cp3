// =============================================================================
// src/parser/tests/ast_printer.h - AST pretty-printer for the parser driver.
//
// Prints each node as: kind name, nodeId, start position, then the fields the
// driver's golden output records. Recurses over children with a 2-space indent.
// =============================================================================
#pragma once

#include "cp3/core/ast.hpp"

namespace cp3test {

// Prints `n` (and its children) at the given indentation. A null node prints
// "<null>".
void printNode(const cp3::Node* n, int ind);

}  // namespace cp3test
