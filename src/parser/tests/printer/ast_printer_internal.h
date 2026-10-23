// =============================================================================
// src/parser/tests/ast_printer_internal.h - shared declarations for the AST
// printer implementation units (ast_printer*.cpp). Test-only.
//
// Each category printer handles one slice of NodeKind and returns true when it
// printed the node, so printNode() can dispatch by category rather than by one
// oversized switch (STYLE.md sec.2 function budget).
// =============================================================================
#pragma once

#include "cp3/core/ast.hpp"

#include "ast_printer.h"

namespace cp3test {

using cp3::ExprList;
using cp3::Node;
using cp3::ParamList;
using cp3::StmtList;
using cp3::TypeList;
using cp3::TypeParamList;

// -- enum/mode name helpers ---------------------------------------------------
const char* varKindName(cp3::VarKind k);
const char* accessName(cp3::AccessModifier a);
const char* unaryName(cp3::UnaryOp o);
const char* updateName(cp3::UpdateOp o);
const char* binaryName(cp3::BinaryOp o);
const char* logicalName(cp3::LogicalOp o);
const char* assignName(cp3::AssignOp o);
const char* accessorName(cp3::AccessorKind k);
const char* litKindName(cp3::LiteralTypeKind k);
const char* importKindName(cp3::ImportKind k);
const char* exportKindName(cp3::ExportKind k);

// -- list printers ------------------------------------------------------------
void printStmts(const StmtList& l, int ind);
void printExprs(const ExprList& l, int ind);
void printTypes(const TypeList& l, int ind);
void printParams(const ParamList& l, int ind);
void printTypeParams(const TypeParamList& l, int ind);

// -- category printers (return true when the node kind was handled) ----------
bool printLiteralNode(const Node* n, int ind);
bool printObjectNode(const Node* n, int ind);
bool printExprNode(const Node* n, int ind);
bool printPatternNode(const Node* n, int ind);
bool printStmtNode(const Node* n, int ind);
bool printDeclNode(const Node* n, int ind);
bool printMemberNode(const Node* n, int ind);
bool printTypeNode(const Node* n, int ind);

}  // namespace cp3test
