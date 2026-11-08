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
#include <unordered_map>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"

namespace cp3 {

namespace {

bool isNumberLiteralValue(const Type& t) {
    return t.isLiteral() && t.literalBase == TypeKind::Number;
}

// True for an operand a native arithmetic operator can consume: a native value
// or an ordinary cp3 number (a number literal included).
bool isNumericOperand(const Type& t) {
    return isNativeType(t) || isNumberish(t);
}

// An enum with an integral backing (spec 04): its value has a numeric
// reading, so the operator table can treat it like a number. A
// string-backed enum has none, and an enum name the table does not know
// stays rejected.
bool isNumberBackedEnum(const std::unordered_map<std::string, EnumInfo>& table,
                        const Type& t) {
    if (t.kind != TypeKind::Enum) return false;
    const auto found = table.find(t.name);
    return found != table.end() && !found->second.stringBacked;
}

// Both operands carry a numeric reading for one operator: a number / number
// pair, or a number-backed enum against the same enum or a numeric operand
// (spec 04 / TS parity). Distinct enums and string-backed enums have no
// common numeric reading and stay errors.
bool hasEnumNumberPair(const std::unordered_map<std::string, EnumInfo>& table,
                       const Type& l, const Type& r) {
    if (isNumericOperand(l) && isNumericOperand(r)) return true;
    const bool lEnum = isNumberBackedEnum(table, l);
    const bool rEnum = isNumberBackedEnum(table, r);
    if (lEnum && rEnum) return l.name == r.name;
    if (lEnum) return isNumericOperand(r);
    if (rEnum) return isNumericOperand(l);
    return false;
}

}  // namespace

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
    // Negating a numeric literal keeps a literal type, so `let a: r_'i8' =
    // -1;` sees an integral value in range instead of a widened `number`.
    // (Codegen's integralLiteralValue already folds the same shape.)
    if (u.op == UnaryOp::Negate && ot.isLiteral() &&
        ot.literalBase == TypeKind::Number) {
        // Keep the exact spelling for the 64-bit boundaries: `-9223372036854775808`
        // fits i64 even though its positive magnitude does not fit i64.
        const std::string raw =
            ot.numberRaw.empty() ? std::string() : "-" + ot.numberRaw;
        return Type::numberLiteral(-ot.numberValue, raw);
    }
    if (isNativeType(ot)) {
        // C++ allows `-`, `+` and `~` on every integer, and `-`/`+` on floats.
        // The declared type keeps its native width; the C++ compiler applies
        // any integral promotion in the generated expression.
        if (u.op == UnaryOp::BitNot && !isNativeInteger(ot)) {
            report(diagcode::kInvalidOperandTypes, u.span,
                   "invalid operand type '" + pretty(ot) +
                       "' for unary '~' (an integer type is required)");
            return Type::error();
        }
        return ot;
    }
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
    // `++`/`--` keep a native operand's declared width (C++ narrows back to
    // the lvalue type); ordinary numbers stay `number`.
    const Type operandType = up.operand ? typeOfExpr(*up.operand)
                                        : Type::unknown();
    return withType(up, isNativeType(operandType) ? operandType : Type::number());
}

// =============================================================================
// Binary + logical + conditional
// =============================================================================
Type Analyzer::inferInstanceof(BinaryExpr& be) {
    Type l = be.left ? inferExpr(*be.left) : Type::unknown();
    const Identifier* id =
        be.right && be.right->kind == NodeKind::Identifier
            ? static_cast<const Identifier*>(be.right.get())
            : nullptr;
    const bool targetKnown =
        id != nullptr &&
        (findClass(id->name) != nullptr || isErrorClassName(id->name));
    if (!targetKnown) {
        report(diagcode::kTypeMismatch, be.right ? be.right->span : be.span,
               "the right operand of 'instanceof' must be a class name");
    }
    bool classLike = l.kind == TypeKind::Class || l.kind == TypeKind::Unknown ||
                     l.kind == TypeKind::Error || l.kind == TypeKind::Null ||
                     l.kind == TypeKind::Undefined || isPermissive(l);
    if (l.kind == TypeKind::Nullable && !l.parts.empty() &&
        l.parts.front().kind == TypeKind::Class) {
        classLike = true;
    }
    if (l.kind == TypeKind::Union) {
        int values = 0;
        bool oneClass = false;
        for (const Type& part : l.parts) {
            if (part.kind == TypeKind::Null ||
                part.kind == TypeKind::Undefined) {
                continue;
            }
            ++values;
            oneClass = part.kind == TypeKind::Class;
        }
        if (values == 1 && oneClass) classLike = true;
    }
    if (!classLike) {
        report(diagcode::kInvalidOperandTypes, be.span,
               "invalid left operand of type '" + pretty(l) +
                   "' for 'instanceof'");
    }
    return withType(be, Type::boolean());
}

Type Analyzer::inferBinary(BinaryExpr& be) {
    if (be.op == BinaryOp::Instanceof) return inferInstanceof(be);
    Type l = be.left ? inferExpr(*be.left) : Type::unknown();
    Type r = be.right ? inferExpr(*be.right) : Type::unknown();
    Type t = evalBinary(be.op, l, r, be.span);
    return withType(be, t);
}

Type Analyzer::evalBinary(BinaryOp op, const Type& l, const Type& r,
                          const SourceSpan& span) {
    const bool permissive = isPermissive(l) || isPermissive(r);
    // Native numeric operands (spec 03 / D65): C++ usual arithmetic
    // conversions. An integral literal that fits adopts the native type, so
    // `n % 2` and `n + 1` stay native; any other number widens the expression
    // to `number` unless strictNumericCasts refuses the mix.
    Type ln = l;
    Type rn = r;
    if (isNativeType(ln) && isNumberLiteralValue(r) &&
        nativeCanRepresentLiteral(ln, r)) {
        rn = ln;
    } else if (isNativeType(rn) && isNumberLiteralValue(l) &&
               nativeCanRepresentLiteral(rn, l)) {
        ln = rn;
    }
    if (!permissive && (isNativeType(ln) || isNativeType(rn))) {
        if (auto native = evalNativeBinary(op, ln, rn, span)) return *native;
    }
    const bool numericPair = hasEnumNumberPair(enums_, ln, rn);
    switch (op) {
        case BinaryOp::Add:
            if (permissive) return ambient();
            if (hasEnumNumberPair(enums_, ln, rn)) return Type::number();
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
            if (hasEnumNumberPair(enums_, ln, rn)) return Type::number();
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
            if (numericPair || (isStringish(l) && isStringish(r))) {
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
        case BinaryOp::Instanceof:
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
        case BinaryOp::Instanceof: return "instanceof";
    }
    return "?";
}

Type Analyzer::inferLogical(LogicalExpr& le) {
    Type l = le.left ? inferExpr(*le.left) : Type::unknown();
    Type r = Type::unknown();
    if (le.left && le.right &&
        (le.op == LogicalOp::And || le.op == LogicalOp::Or ||
         le.op == LogicalOp::Nullish)) {
        pushScope();
        std::vector<ConditionNarrowing> narrows;
        // Right operand sees the left's true branch for &&, and its false
        // branch for || and ??.
        collectCondNarrowings(*le.left, le.op == LogicalOp::And, narrows);
        applyConditionNarrowings(narrows, false);
        r = inferExpr(*le.right);
        popScope();
    } else if (le.right) {
        r = inferExpr(*le.right);
    }
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
    std::vector<ConditionNarrowing> narrows;
    if (ce.condition) {
        inferExpr(*ce.condition);
        checkCondition(typeOfExpr(*ce.condition), ce.condition->span);
        collectCondNarrowings(*ce.condition, true, narrows);
    }
    pushScope();
    applyConditionNarrowings(narrows, false);
    Type a = ce.whenTrue ? inferExpr(*ce.whenTrue) : Type::unknown();
    popScope();
    std::vector<ConditionNarrowing> elseNarrows;
    if (ce.condition) collectCondNarrowings(*ce.condition, false, elseNarrows);
    pushScope();
    applyConditionNarrowings(elseNarrows, false);
    Type b = ce.whenFalse ? inferExpr(*ce.whenFalse) : Type::unknown();
    popScope();
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
    // Assignment invalidates member narrowing for that target (fail closed).
    // After the RHS has been read: `cur = cur.next` still sees the old
    // narrowing on the RHS, and only later reads are un-narrowed.
    if (ae.target->kind == NodeKind::Identifier) {
        const std::string prefix =
            static_cast<Identifier&>(*ae.target).name + ":";
        for (auto it = narrowedMembersByKey_.begin();
             it != narrowedMembersByKey_.end();) {
            if (it->first.rfind(prefix, 0) == 0) it = narrowedMembersByKey_.erase(it);
            else ++it;
        }
    } else if (ae.target->kind == NodeKind::MemberExpr) {
        narrowedMembersByKey_.erase(memberKeyFor(*ae.target));
    }
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
            // Compound assignment reads the current value but writes the
            // declared slot: use the declared type (see
            // checkAssignToTarget).
            return b.declaredType.isUnknown() ? b.type : b.declaredType;
        }
        case NodeKind::MemberExpr: {
            auto& m = static_cast<MemberExpr&>(target);
            Type ot = m.object ? inferExpr(*m.object) : Type::unknown();
            const bool prevTarget = assignTarget_;
            assignTarget_ = true;
            Type r = memberOf(ot, m.property, m.span, m.optional);
            assignTarget_ = prevTarget;
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
            // Assignment is checked against the declared type: the current
            // `b.type` may be a narrowed view (`cur` is `Node` inside
            // `while (cur !== null)`), which would reject `cur = cur.next`.
            const Type& declared =
                b.declaredType.isUnknown() ? b.type : b.declaredType;
            checkAssign(valType, declared, span, false);
            return;
        }
        case NodeKind::MemberExpr: {
            auto& m = static_cast<MemberExpr&>(target);
            Type ot = m.object ? inferExpr(*m.object) : Type::unknown();
            const bool prevTarget = assignTarget_;
            assignTarget_ = true;
            Type mt = memberOf(ot, m.property, m.span, m.optional);
            assignTarget_ = prevTarget;
            setType(m, mt);
            if (isReadOnlyFamilyProp(ot, m.property)) {
                report(diagcode::kTypeMismatch, span,
                       "property '" + m.property +
                           "' of a typed array is read-only");
                return;
            }
            if (isByteBufferReadOnlyProp(ot, m.property)) {
                report(diagcode::kTypeMismatch, span,
                       "property '" + m.property +
                           "' of a Buffer is read-only");
                return;
            }
            if (mt.isError()) return;
            checkAssign(valType, mt, span, false);
            return;
        }
        case NodeKind::IndexExpr: {
            auto& ix = static_cast<IndexExpr&>(target);
            Type ot = ix.object ? inferExpr(*ix.object) : Type::unknown();
            if (!isTypedArrayClass(ot) && ot.kind == TypeKind::String) {
                // JavaScript does `s[0] = 'x'` silently (strings are
                // immutable there too, writes are ignored). cp3 rejects it,
                // but not with the generic "cannot assign X to number"
                // mismatch that reading an index into a string would
                // otherwise produce.
                if (ix.index) inferExpr(*ix.index);
                report(diagcode::kInvalidAssignmentTarget, span,
                       "cannot assign to the index of a string; strings are "
                       "immutable (use string methods like replace / substring / "
                       "slice)");
                return;
            }
            if (isTypedArrayClass(ot)) {
                // A typed-array element target is `number`: out-of-range
                // stores are ignored at runtime (JS semantics), and a
                // non-number write is CP3401 (src/language/typed-arrays/sema.cpp
                // owns the read refinement).
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
