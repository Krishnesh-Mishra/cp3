// =============================================================================
// src/cli/src/detail/lsp_intel_walk.hpp - the AST walker behind forEachNode
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// The visitor planner (visitChildren) lives in lsp_intel_walk.cpp and the
// per-family child enumerations in lsp_intel_walk_children.cpp; both include
// this header so the dispatcher and the families agree on the recursion
// contract. Only these declarations cross the file boundary; everything else
// stays in the anonymous namespaces of the two units.
// =============================================================================
#pragma once

#include <functional>

#include "cp3/ast.hpp"

namespace cp3::cli {

// One visit of one node. Recursion goes through visitSubtree.
using Visit = std::function<void(const Node&)>;

// Visits `node` (when not null) and then its children.
void visitSubtree(const Node* node, const Visit& visit);

// Child-list helpers shared by the family visitors.
void visitExprs(const ExprList& list, const Visit& visit);
void visitStmts(const StmtList& list, const Visit& visit);
void visitParams(const ParamList& list, const Visit& visit);
void visitTypeParams(const TypeParamList& list, const Visit& visit);
void visitTypes(const TypeList& list, const Visit& visit);

// Family visitors called by the planner (lsp_intel_walk_children.cpp).
void visitExprChildren(const Node& node, const Visit& visit);
void visitDeclStmtChildren(const Node& node, const Visit& visit);
void visitControlStmtChildren(const Node& node, const Visit& visit);

}  // namespace cp3::cli
