// =============================================================================
// src/codegen/src/emit/assignment.cpp - codegen assignment lowering
// (STYLE.md §8).
//
// The `AssignmentExpr` forms kept out of emit/expr.cpp:
//
//   * the compound arithmetic writes (`+=`, `-=`, `*=`, `/=`, `%=`, `**=`);
//   * the bitwise/shift writes (`&=`, `|=`, `^=`, `<<=`, `>>=`, `>>>=`), which
//     go through cp3/core/bitwise.hpp like their binary forms;
//   * the short-circuit writes (`&&=`, `||=`, `??=`): the ternaries reproduce
//     both the assigned value and the skipped right-hand evaluation.
//
// `emitBinary`/`emitUpdate` stay in emit/expr.cpp; the `IndexExpr` target keeps
// its family write path (src/std/typed-arrays/codegen_methods.cpp).
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>

#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// `x += y` and the other arithmetic compounds.
std::string arithAssignText(AssignOp op, const std::string& target,
                            const std::string& value) {
    const char* text = nullptr;
    switch (op) {
        case AssignOp::Add: text = "+="; break;
        case AssignOp::Subtract: text = "-="; break;
        case AssignOp::Multiply: text = "*="; break;
        default: text = "/="; break;
    }
    return "(" + target + " " + text + " " + value + ")";
}

// The cp3/core/bitwise.hpp entry point of a compound write, or nullptr when
// `op` is not a bitwise/shift one.
const char* bitwiseHelper(AssignOp op) {
    switch (op) {
        case AssignOp::BitAnd: return "band";
        case AssignOp::BitOr: return "bor";
        case AssignOp::BitXor: return "bxor";
        case AssignOp::ShiftLeft: return "shl";
        case AssignOp::ShiftRight: return "shr";
        case AssignOp::UnsignedShiftRight: return "ushr";
        default: return nullptr;
    }
}

// `x &&= y` / `x ||= y`: the ternary assigns the right operand only when the
// short circuit demands it.
std::string logicalAssignText(AssignOp op, const std::string& target,
                              const std::string& value) {
    if (op == AssignOp::LogicalAnd) {
        return "(" + target + " = cp3_truthy(" + target + ") ? (" + value +
               ") : (" + target + "))";
    }
    return "(" + target + " = cp3_truthy(" + target + ") ? (" + target +
           ") : (" + value + "))";
}

// `x ??= y`. nullopt when the target shape has no spelling here (a Maybe
// target with a nullish right operand; a non-value target). The `decltype`
// re-spells the target's own C++ type so the assignment stays exact.
std::optional<std::string> nullishAssignText(const CppInfo& T, const Expr& rhs,
                                             const std::string& target,
                                             const std::string& value) {
    const bool nullishRhs = isNullishLiteral(rhs);
    if (T.shape == Shape::Optional) {
        const std::string rhsText = nullishRhs ? "std::nullopt" : value;
        return "(" + target + " = " + target + ".has_value() ? " + target +
               " : decltype(" + target + ")(" + rhsText + "))";
    }
    if (T.shape == Shape::Maybe && !nullishRhs) {
        return "(" + target + " = " + target + ".hasValue() ? " + target +
               " : decltype(" + target + ")(" + value + "))";
    }
    if (isPrimitiveShape(T.shape) || T.shape == Shape::Union) {
        // The target can never be nullish: the right operand is not evaluated
        // at all (JS).
        return "(" + target + ")";
    }
    return std::nullopt;
}

}  // namespace

std::string Generator::emitAssignment(const AssignmentExpr& a) {
    if (a.target->kind != NodeKind::Identifier) {
        // Array/typed-view element writes (`a[1] = 9`, `u8[0] = 5`) lower to
        // the family's checked writes first
        // (src/std/typed-arrays/codegen_methods.cpp).
        if (a.target->kind == NodeKind::IndexExpr) {
            if (std::string w = emitFamilyIndexWrite(a); !w.empty()) return w;
        }
        notImplemented(a, "assignment to a non-identifier target");
        return "0.0";
    }
    const std::string target =
        mapIdent(static_cast<const Identifier&>(*a.target).name);
    const CppInfo T = info(*a.target);
    const std::string value = expr(*a.value);

    if (const char* helper = bitwiseHelper(a.op)) {
        // ECMAScript ToInt32 on both operands, a double result.
        return "(" + target + " = ::cp3::runtime::bitwise::" + helper + "(" +
               target + ", " + value + "))";
    }
    switch (a.op) {
        case AssignOp::Assign:
            if (T.shape == Shape::Optional && isNullishLiteral(*a.value)) {
                return "(" + target + " = std::nullopt)";
            }
            return "(" + target + " = " + value + ")";
        case AssignOp::Add:
        case AssignOp::Subtract:
        case AssignOp::Multiply:
        case AssignOp::Divide:
            return arithAssignText(a.op, target, value);
        case AssignOp::Modulo:
            return "(" + target + " = " + modCall(target, *a.value, value) + ")";
        case AssignOp::Power:
            return "(" + target + " = std::pow(" + target + ", " + value + "))";
        case AssignOp::LogicalAnd:
        case AssignOp::LogicalOr:
            if (T.representable) {
                return logicalAssignText(a.op, target, value);
            }
            break;
        case AssignOp::Nullish:
            if (auto text = nullishAssignText(T, *a.value, target, value)) {
                return *text;
            }
            break;
        default:
            break;
    }
    notImplemented(a, "compound assignment operator");
    return "0.0";
}

}  // namespace cp3
