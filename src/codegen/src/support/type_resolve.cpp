// =============================================================================
// src/codegen/src/support/type_resolve.cpp - the Generator's type resolution
// (split out of support/type_map.cpp for the file budget).
//
// `type_map.cpp` owns the cp3 Type -> C++ spelling mapping (`classify` and the
// family hooks). This unit owns the other direction used by the emitters: the
// effective type of an expression and of a declarator, which consults the
// declaration environment before sema's side tables (so a lowering that
// declares a type sema cannot see still resolves its reads).
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>

#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

// -----------------------------------------------------------------------------
// Generator: type resolution
// -----------------------------------------------------------------------------

std::optional<Type> Generator::bindingType(const VariableDeclarator& d) const {
    if (d.type) {
        if (auto t = typeOfNode(*d.type)) {
            return t;
        }
    }
    // A `parallel.reduce` result takes the accumulator type even though sema
    // leaves the ambient call unknown (the binding must not be `auto`: its
    // closure was lowered against the init's C++ type).
    if (d.init && d.init->kind == NodeKind::CallExpr) {
        if (auto t = parallelCallType(static_cast<const CallExpr&>(*d.init))) {
            return *t;
        }
    }
    if (const Type* t = sema_.typeOf(d.nodeId)) {
        if (!t->isUnknown() && !t->isError()) return *t;
    }
    if (d.id) {
        if (const Type* t = sema_.typeOf(d.id->nodeId)) {
            if (!t->isUnknown() && !t->isError()) return *t;
        }
    }
    return std::nullopt;
}

// The binding's effective type: declared/annotated first, then inferred from
// its initializer. This is what we both emit and register in the scope env.
std::optional<Type> Generator::resolvedBindingType(const VariableDeclarator& d) const {
    if (auto declared = bindingType(d)) {
        return declared;
    }
    if (d.init) {
        const Type inferred = effectiveType(*d.init);
        if (!inferred.isUnknown() && !inferred.isError()) {
            return inferred;
        }
    }
    return std::nullopt;
}

Type Generator::effectiveType(const Expr& e) const {
    if (const auto t = networkMapFieldType(e)) return *t;
    if (e.kind == NodeKind::CallExpr) {
        if (auto t = parallelCallType(static_cast<const CallExpr&>(e))) return *t;
    }
    if (e.kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(e);
        if (auto b = lookup(id.name)) return b->type;
        if (id.name == "undefined") return Type::undefined();
        if (id.name == "null") return Type::nullType();
    }
    if (const Type* t = sema_.typeOf(e.nodeId)) {
        if (!t->isUnknown() && !t->isError()) return *t;
    }
    switch (e.kind) {
        case NodeKind::NumberLiteral: return Type::number();
        case NodeKind::StringLiteral: return Type::string();
        case NodeKind::BoolLiteral: return Type::boolean();
        case NodeKind::NullLiteral: return Type::nullType();
        case NodeKind::UndefinedLiteral: return Type::undefined();
        case NodeKind::TemplateLiteral: return Type::string();
        case NodeKind::BinaryExpr: return inferBinary(static_cast<const BinaryExpr&>(e));
        case NodeKind::LogicalExpr: return inferLogical(static_cast<const LogicalExpr&>(e));
        case NodeKind::UnaryExpr: return inferUnary(static_cast<const UnaryExpr&>(e));
        case NodeKind::UpdateExpr:
            return effectiveType(*static_cast<const UpdateExpr&>(e).operand);
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return unify(effectiveType(*c.whenTrue), effectiveType(*c.whenFalse));
        }
        case NodeKind::AssignmentExpr:
            return effectiveType(*static_cast<const AssignmentExpr&>(e).target);
        default: return Type::unknown();
    }
}

Type Generator::inferBinary(const BinaryExpr& b) const {
    switch (b.op) {
        case BinaryOp::Add: {
            const Type l = effectiveType(*b.left);
            const Type r = effectiveType(*b.right);
            const CppInfo L = classify(l);
            const CppInfo R = classify(r);
            if (L.shape == Shape::String || R.shape == Shape::String) return Type::string();
            return Type::number();
        }
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
        case BinaryOp::StrictEqual:
        case BinaryOp::StrictNotEqual:
        case BinaryOp::LessThan:
        case BinaryOp::GreaterThan:
        case BinaryOp::LessThanEqual:
        case BinaryOp::GreaterThanEqual:
            return Type::boolean();
        default:
            return Type::number();
    }
}

Type Generator::inferLogical(const LogicalExpr& l) const {
    const Type left = effectiveType(*l.left);
    const Type right = effectiveType(*l.right);
    if (l.op == LogicalOp::Nullish) {
        if (isStaticallyNullish(left)) return right;
        return unify(stripNullish(left), right);
    }
    return unify(left, right);
}

Type Generator::inferUnary(const UnaryExpr& u) const {
    switch (u.op) {
        case UnaryOp::Not: return Type::boolean();
        case UnaryOp::Negate:
        case UnaryOp::Plus:
        case UnaryOp::BitNot: return Type::number();
        case UnaryOp::Typeof: return Type::string();
        case UnaryOp::Void: return Type::undefined();
        case UnaryOp::Delete: return Type::boolean();
    }
    return Type::unknown();
}

CppInfo Generator::info(const Expr& e) const { return classify(effectiveType(e)); }

}  // namespace cp3
