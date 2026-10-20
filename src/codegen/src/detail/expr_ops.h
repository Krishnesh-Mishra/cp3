// =============================================================================
// src/codegen/src/detail/expr_ops.h - PRIVATE binary-operator spelling helpers.
//
// Shared by the expression and equality emitters (STYLE section 8): the C++
// token for the arithmetic/relational operators, and the comparison set whose
// operands may use the no-negative-zero `%` lowering.
// =============================================================================
#pragma once

#include "cp3/ast.hpp"

namespace cp3 {
namespace detail {

inline const char* binaryOpSpelling(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Subtract: return "-";
        case BinaryOp::Multiply: return "*";
        case BinaryOp::Divide: return "/";
        case BinaryOp::LessThan: return "<";
        case BinaryOp::GreaterThan: return ">";
        case BinaryOp::LessThanEqual: return "<=";
        case BinaryOp::GreaterThanEqual: return ">=";
        default: return nullptr;
    }
}

// A comparison sees two numbers as equal when their difference is zero, so
// the sign of a zero operand is unobservable to it (IEEE -0 == +0).
inline bool isComparisonOp(BinaryOp op) {
    switch (op) {
        case BinaryOp::LessThan:
        case BinaryOp::GreaterThan:
        case BinaryOp::LessThanEqual:
        case BinaryOp::GreaterThanEqual:
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
        case BinaryOp::StrictEqual:
        case BinaryOp::StrictNotEqual:
            return true;
        default:
            return false;
    }
}

}  // namespace detail
}  // namespace cp3
