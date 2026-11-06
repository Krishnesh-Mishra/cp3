// =============================================================================
// src/codegen/src/support/enum_values.cpp - the compile-time value of an enum
// member initializer (spec 04).
//
// An enum member carries a compile-time constant: a literal, a constant
// expression over literals (`A = 1 + 1`), or a link to an earlier member of the
// same enum (`B = A`). The sema accepts exactly the initializers this
// evaluator can read (src/sema/src/checks/enum_check.cpp keeps the two in step:
// it rejects a non-constant initializer before codegen runs), so a nullopt
// here means the initializer slipped through and is reported as an error.
// =============================================================================
#include "detail/int/enums.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "cp3/core/ast.hpp"

namespace cp3 {
namespace detail {

namespace {

// The member of `known` with this name, or nullptr. A linked member
// initializer (`B = A`) reads the value of an earlier member.
const EnumCppMember* linkedMember(const std::vector<EnumCppMember>& known,
                                   const std::string& name) {
    for (const EnumCppMember& m : known) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

// The linked member an initializer names, or nullptr: a bare name (`A`) or the
// enum's own name with a member (`Color.A`).
const EnumCppMember* linkedTarget(const Expr* e,
                                   const std::vector<EnumCppMember>& known,
                                   const std::string& enumName) {
    if (e == nullptr) return nullptr;
    if (e->kind == NodeKind::Identifier) {
        return linkedMember(known, static_cast<const Identifier&>(*e).name);
    }
    if (e->kind != NodeKind::MemberExpr) return nullptr;
    const auto& m = static_cast<const MemberExpr&>(*e);
    if (!m.object || m.object->kind != NodeKind::Identifier) return nullptr;
    if (static_cast<const Identifier&>(*m.object).name != enumName) return nullptr;
    return linkedMember(known, m.property);
}

// A unary sign or bit-not over a number constant.
std::optional<EnumConst> evalEnumUnary(
    const UnaryExpr& u, const std::vector<EnumCppMember>& known,
    const std::string& enumName) {
    if (u.op != UnaryOp::Negate && u.op != UnaryOp::Plus &&
        u.op != UnaryOp::BitNot) {
        return std::nullopt;
    }
    const std::optional<EnumConst> operand =
        evalEnumInit(u.operand.get(), known, enumName);
    if (!operand || !operand->isNumber) return std::nullopt;
    EnumConst value;
    value.isNumber = true;
    if (u.op == UnaryOp::Negate) {
        value.number = -operand->number;
    } else if (u.op == UnaryOp::BitNot) {
        value.number =
            static_cast<double>(~static_cast<long long>(operand->number));
    } else {
        value.number = operand->number;
    }
    return value;
}

// A binary operator over two constants: `+` adds numbers or concatenates
// strings, the arithmetic operators work on numbers.
std::optional<EnumConst> evalEnumBinary(
    const BinaryExpr& b, const std::vector<EnumCppMember>& known,
    const std::string& enumName) {
    const std::optional<EnumConst> left =
        evalEnumInit(b.left.get(), known, enumName);
    const std::optional<EnumConst> right =
        evalEnumInit(b.right.get(), known, enumName);
    if (!left || !right) return std::nullopt;
    EnumConst value;
    if (b.op == BinaryOp::Add) {
        if (left->isNumber && right->isNumber) {
            value.isNumber = true;
            value.number = left->number + right->number;
            return value;
        }
        if (!left->isNumber && !right->isNumber) {
            value.text = left->text + right->text;
            return value;
        }
        return std::nullopt;
    }
    if (!left->isNumber || !right->isNumber) return std::nullopt;
    value.isNumber = true;
    switch (b.op) {
        case BinaryOp::Subtract:
            value.number = left->number - right->number;
            break;
        case BinaryOp::Multiply:
            value.number = left->number * right->number;
            break;
        case BinaryOp::Divide:
            // A division by zero has no value: the initializer is not a
            // constant the enum can carry.
            if (right->number == 0.0) return std::nullopt;
            value.number = left->number / right->number;
            break;
        case BinaryOp::Modulo:
            if (right->number == 0.0) return std::nullopt;
            value.number = std::fmod(left->number, right->number);
            break;
        default:
            return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<EnumConst> evalEnumInit(const Expr* init,
                                      const std::vector<EnumCppMember>& known,
                                      const std::string& enumName) {
    if (init == nullptr) return std::nullopt;
    switch (init->kind) {
        case NodeKind::NumberLiteral: {
            EnumConst value;
            value.isNumber = true;
            value.number = static_cast<const NumberLiteral&>(*init).value;
            return value;
        }
        case NodeKind::StringLiteral: {
            EnumConst value;
            value.text = static_cast<const StringLiteral&>(*init).value;
            return value;
        }
        case NodeKind::UnaryExpr:
            return evalEnumUnary(static_cast<const UnaryExpr&>(*init), known,
                                 enumName);
        case NodeKind::BinaryExpr:
            return evalEnumBinary(static_cast<const BinaryExpr&>(*init), known,
                                  enumName);
        default:
            break;
    }
    // A linked member (`B = A`): the value of the member it names.
    const EnumCppMember* target = linkedTarget(init, known, enumName);
    if (target == nullptr) return std::nullopt;
    EnumConst value;
    if (target->isString) {
        value.text = target->stringValue;
        return value;
    }
    value.isNumber = true;
    value.number = static_cast<double>(target->value);
    return value;
}

}  // namespace detail
}  // namespace cp3
