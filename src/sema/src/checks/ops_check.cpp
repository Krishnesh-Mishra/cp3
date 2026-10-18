// =============================================================================
// src/sema/src/checks/ops_check.cpp - operator expression inference.
//
// Owns unary/update, binary (arithmetic + comparison), logical, conditional and
// assignment inference, including the compound-assignment result rules and the
// assignment-target checks (`targetTypeOf` / `checkAssignToTarget`). Primary and
// member/call inference live in expr_check.cpp / member_check.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Unary + update
// =============================================================================
Type Analyzer::inferUnary(UnaryExpr& u) {
    Type ot = u.operand ? inferExpr(*u.operand) : Type::unknown();
    Type t = Type::unknown();
    switch (u.op) {
        case UnaryOp::Not:
            t = Type::boolean();
            break;
        case UnaryOp::Typeof:
            t = Type::string();
            break;
        case UnaryOp::Delete:
            checkBannedDelete(u.span);
            t = Type::boolean();
            break;
        case UnaryOp::Void:
            t = Type::undefined();
            break;
        case UnaryOp::Negate:
        case UnaryOp::Plus:
        case UnaryOp::BitNot:
            t = arithmeticUnaryResult(u, ot);
            break;
    }
    return withType(u, t);
}

Type Analyzer::arithmeticUnaryResult(UnaryExpr& u, const Type& ot) {
    if (isPermissive(ot)) return Type::number();
    if (isNumberish(ot)) return Type::number();
    if (u.op == UnaryOp::Negate && ot.isUnion()) return Type::number();
    report(diagcode::kInvalidOperandTypes, u.span,
           "invalid operand type '" + pretty(ot) + "' for unary '" +
               unaryText(u.op) + "'");
    return Type::number();
}

std::string Analyzer::unaryText(UnaryOp op) const {
    switch (op) {
        case UnaryOp::Not: return "!";
        case UnaryOp::Negate: return "-";
        case UnaryOp::Plus: return "+";
        case UnaryOp::BitNot: return "~";
        case UnaryOp::Typeof: return "typeof";
        case UnaryOp::Delete: return "delete";
        case UnaryOp::Void: return "void";
    }
    return "?";
}

Type Analyzer::inferUpdate(UpdateExpr& up) {
    if (up.operand) {
        if (up.operand->kind == NodeKind::Identifier) {
            auto& id = static_cast<Identifier&>(*up.operand);
            Binding b;
            if (!lookup(id.name, b)) {
                reportUndefinedName(id.span, id.name);
            } else if (b.isConst) {
                report(diagcode::kConstReassignment, up.span,
                       "cannot reassign to const '" + id.name + "'");
            }
            setType(id, b.type);
        } else {
            inferExpr(*up.operand);
        }
    }
    return withType(up, Type::number());
}

// =============================================================================
// Binary + logical + conditional
// =============================================================================
Type Analyzer::inferBinary(BinaryExpr& be) {
    Type l = be.left ? inferExpr(*be.left) : Type::unknown();
    Type r = be.right ? inferExpr(*be.right) : Type::unknown();
    Type t = evalBinary(be.op, l, r, be.span);
    return withType(be, t);
}

Type Analyzer::evalBinary(BinaryOp op, const Type& l, const Type& r,
                          const SourceSpan& span) {
    const bool permissive = isPermissive(l) || isPermissive(r);
    switch (op) {
        case BinaryOp::Add:
            if (permissive) return ambient();
            if (isNumberish(l) && isNumberish(r)) return Type::number();
            if (isStringish(l) && isStringish(r)) return Type::string();
            report(diagcode::kMixedTypeArithmetic, span,
                   "type mismatch: mixed-type arithmetic with '+' on '" +
                       pretty(l) + "' and '" + pretty(r) +
                       "' (only number+number or string+string is allowed)");
            return Type::error();
        case BinaryOp::Subtract:
        case BinaryOp::Multiply:
        case BinaryOp::Divide:
        case BinaryOp::Modulo:
        case BinaryOp::Power:
        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::ShiftLeft:
        case BinaryOp::ShiftRight:
        case BinaryOp::UnsignedShiftRight:
            if (permissive) return ambient();
            if (isNumberish(l) && isNumberish(r)) return Type::number();
            report(diagcode::kMixedTypeArithmetic, span,
                   "type mismatch: mixed-type arithmetic with '" +
                       binaryText(op) + "' on '" + pretty(l) + "' and '" +
                       pretty(r) + "' (both operands must be number)");
            return Type::error();
        case BinaryOp::LessThan:
        case BinaryOp::GreaterThan:
        case BinaryOp::LessThanEqual:
        case BinaryOp::GreaterThanEqual:
            if (permissive) return Type::boolean();
            if ((isNumberish(l) && isNumberish(r)) ||
                (isStringish(l) && isStringish(r))) {
                return Type::boolean();
            }
            report(diagcode::kInvalidOperandTypes, span,
                   "invalid operand types '" + pretty(l) + "' and '" +
                       pretty(r) + "' for comparison '" + binaryText(op) + "'");
            return Type::boolean();
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
        case BinaryOp::StrictEqual:
        case BinaryOp::StrictNotEqual:
            return Type::boolean();
        case BinaryOp::In:
            return Type::boolean();
    }
    return Type::unknown();
}

std::string Analyzer::binaryText(BinaryOp op) const {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Subtract: return "-";
        case BinaryOp::Multiply: return "*";
        case BinaryOp::Divide: return "/";
        case BinaryOp::Modulo: return "%";
        case BinaryOp::Power: return "**";
        case BinaryOp::Equal: return "==";
        case BinaryOp::NotEqual: return "!=";
        case BinaryOp::StrictEqual: return "===";
        case BinaryOp::StrictNotEqual: return "!==";
        case BinaryOp::LessThan: return "<";
        case BinaryOp::GreaterThan: return ">";
        case BinaryOp::LessThanEqual: return "<=";
        case BinaryOp::GreaterThanEqual: return ">=";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr: return "|";
        case BinaryOp::BitXor: return "^";
        case BinaryOp::ShiftLeft: return "<<";
        case BinaryOp::ShiftRight: return ">>";
        case BinaryOp::UnsignedShiftRight: return ">>>";
        case BinaryOp::In: return "in";
    }
    return "?";
}

Type Analyzer::inferLogical(LogicalExpr& le) {
    Type l = le.left ? inferExpr(*le.left) : Type::unknown();
    Type r = le.right ? inferExpr(*le.right) : Type::unknown();
    Type t = Type::unknown();
    switch (le.op) {
        case LogicalOp::And:
        case LogicalOp::Or:
            if (isPermissive(l) || isPermissive(r)) {
                t = ambient();
            } else {
                t = unify(widen(l), widen(r));
            }
            break;
        case LogicalOp::Nullish: {
            Type left = stripNullish(l);
            // A statically nullish left side (`null ?? x`) never produces a
            // value, so the result is the right operand. `stripNullish` folds
            // nullish literals to `unknown` too, so test the source type.
            if (isStaticallyNullish(l)) {
                t = isPermissive(r) ? ambient() : widen(r);
            } else if (isPermissive(left) || isPermissive(r)) {
                t = ambient();
            } else {
                t = unify(widen(left), widen(r));
            }
            break;
        }
    }
    return withType(le, t);
}

Type Analyzer::inferConditional(ConditionalExpr& ce) {
    if (ce.condition) {
        inferExpr(*ce.condition);
        checkCondition(typeOfExpr(*ce.condition), ce.condition->span);
    }
    Type a = ce.whenTrue ? inferExpr(*ce.whenTrue) : Type::unknown();
    Type b = ce.whenFalse ? inferExpr(*ce.whenFalse) : Type::unknown();
    Type t = unify(widen(a), widen(b));
    return withType(ce, t);
}

// =============================================================================
// Assignment
// =============================================================================
Type Analyzer::inferAssignment(AssignmentExpr& ae) {
    if (!ae.target) {
        if (ae.value) inferExpr(*ae.value);
        return Type::unknown();
    }
    Type valType = ae.value ? inferExpr(*ae.value) : Type::unknown();
    if (ae.op == AssignOp::Assign) {
        checkAssignToTarget(*ae.target, valType, ae.span);
        return withType(ae, valType);
    }
    Type targetType = targetTypeOf(*ae.target);
    Type result = compoundResult(ae.op, targetType, valType, ae.span);
    if (!result.isError()) {
        checkAssignToTarget(*ae.target, result, ae.span, /*constCheck*/ true);
    }
    return withType(ae, result);
}

Type Analyzer::compoundResult(AssignOp op, const Type& targetType,
                              const Type& valType, const SourceSpan& span) {
    switch (op) {
        case AssignOp::Add:
            return evalBinary(BinaryOp::Add, targetType, valType, span);
        case AssignOp::Subtract:
            return evalBinary(BinaryOp::Subtract, targetType, valType, span);
        case AssignOp::Multiply:
            return evalBinary(BinaryOp::Multiply, targetType, valType, span);
        case AssignOp::Divide:
            return evalBinary(BinaryOp::Divide, targetType, valType, span);
        case AssignOp::Modulo:
            return evalBinary(BinaryOp::Modulo, targetType, valType, span);
        case AssignOp::Power:
            return evalBinary(BinaryOp::Power, targetType, valType, span);
        case AssignOp::BitAnd:
            return evalBinary(BinaryOp::BitAnd, targetType, valType, span);
        case AssignOp::BitOr:
            return evalBinary(BinaryOp::BitOr, targetType, valType, span);
        case AssignOp::BitXor:
            return evalBinary(BinaryOp::BitXor, targetType, valType, span);
        case AssignOp::ShiftLeft:
            return evalBinary(BinaryOp::ShiftLeft, targetType, valType, span);
        case AssignOp::ShiftRight:
            return evalBinary(BinaryOp::ShiftRight, targetType, valType, span);
        case AssignOp::UnsignedShiftRight:
            return evalBinary(BinaryOp::UnsignedShiftRight, targetType, valType,
                              span);
        case AssignOp::LogicalAnd:
        case AssignOp::LogicalOr:
            return (isPermissive(targetType) || isPermissive(valType))
                       ? ambient()
                       : unify(widen(targetType), widen(valType));
        case AssignOp::Nullish: {
            Type left = stripNullish(targetType);
            return (isPermissive(left) || isPermissive(valType))
                       ? ambient()
                       : unify(widen(left), widen(valType));
        }
        default:
            return valType;
    }
}

Type Analyzer::targetTypeOf(Expr& target) {
    switch (target.kind) {
        case NodeKind::Identifier: {
            auto& id = static_cast<Identifier&>(target);
            Binding b;
            if (!lookup(id.name, b)) {
                reportUndefinedName(id.span, id.name);
                return Type::error();
            }
            setType(id, b.type);
            return b.type;
        }
        case NodeKind::MemberExpr: {
            auto& m = static_cast<MemberExpr&>(target);
            Type ot = m.object ? inferExpr(*m.object) : Type::unknown();
            Type r = memberOf(ot, m.property, m.span, m.optional);
            setType(m, r);
            return r;
        }
        case NodeKind::IndexExpr: {
            auto& ix = static_cast<IndexExpr&>(target);
            Type ot = ix.object ? inferExpr(*ix.object) : Type::unknown();
            Type r = indexOf(ot, ix);
            setType(ix, r);
            return r;
        }
        default:
            return inferExpr(target);
    }
}

void Analyzer::checkAssignToTarget(Expr& target, const Type& valType,
                                   const SourceSpan& span, bool constCheck) {
    switch (target.kind) {
        case NodeKind::Identifier: {
            auto& id = static_cast<Identifier&>(target);
            Binding b;
            if (!lookup(id.name, b)) {
                reportUndefinedName(id.span, id.name);
                return;
            }
            setType(id, b.type);
            if (constCheck && b.isConst) {
                report(diagcode::kConstReassignment, span,
                       "cannot reassign to const '" + id.name + "'");
                return;
            }
            checkAssign(valType, b.type, span, false);
            return;
        }
        case NodeKind::MemberExpr: {
            auto& m = static_cast<MemberExpr&>(target);
            Type ot = m.object ? inferExpr(*m.object) : Type::unknown();
            Type mt = memberOf(ot, m.property, m.span, m.optional);
            setType(m, mt);
            if (isReadOnlyFamilyProp(ot, m.property)) {
                report(diagcode::kTypeMismatch, span,
                       "property '" + m.property +
                           "' of a typed array is read-only");
                return;
            }
            if (mt.isError()) return;
            checkAssign(valType, mt, span, false);
            return;
        }
        case NodeKind::IndexExpr: {
            auto& ix = static_cast<IndexExpr&>(target);
            Type ot = ix.object ? inferExpr(*ix.object) : Type::unknown();
            if (isTypedArrayClass(ot)) {
                // A typed-array element target is `number`: out-of-range
                // stores are ignored at runtime (JS semantics), and a
                // non-number write is CP3401 (tarrays_check.cpp owns the
                // read refinement).
                if (ix.index) inferExpr(*ix.index);
                setType(ix, Type::number());
                checkAssign(valType, Type::number(), span, false);
                return;
            }
            Type mt = indexOf(ot, ix);
            setType(ix, mt);
            if (mt.isError()) return;
            checkAssign(valType, mt, span, false);
            return;
        }
        default:
            inferExpr(target);
            return;
    }
}

}  // namespace cp3
