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
#include "detail/worker.h"

namespace cp3 {

using namespace detail;

// -----------------------------------------------------------------------------
// Generator: type resolution
// -----------------------------------------------------------------------------

Type Generator::effectiveType(const Expr& e) const {
    if (const auto t = networkMapFieldType(e)) return *t;
    if (e.kind == NodeKind::CallExpr) {
        if (auto t = parallelCallType(static_cast<const CallExpr&>(e))) return *t;
        // The worker handle's calls (D80): `w.request(v)` is a `Promise<Out>`;
        // every other member call is void.
        if (auto t = workerCallType(static_cast<const CallExpr&>(e))) return *t;
    }
    // `await w.request(v)` resolves with the next Out message.
    if (e.kind == NodeKind::AwaitExpr) {
        const auto& await = static_cast<const AwaitExpr&>(e);
        if (await.argument && await.argument->kind == NodeKind::CallExpr) {
            if (auto t = workerCallType(
                    static_cast<const CallExpr&>(*await.argument))) {
                if (isPromiseType(*t)) return promiseValue(*t);
            }
        }
    }
    if (e.kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(e);
        if (auto b = lookup(id.name)) return b->type;
        if (id.name == "undefined") return Type::undefined();
        if (id.name == "null") return Type::nullType();
    }
    if (const Type* t = sema_.typeOf(e.nodeId)) {
        // `$ambient` is sema's permissive/"not statically known" marker: an
        // expression over an unannotated worker listener parameter is ambient
        // there even though the channel fixes the parameter's type. Falling
        // through to codegen's own inference recovers the concrete shape the
        // emitted C++ actually has (an unannotated listener parameter is the
        // main case: spec 13's typed worker surface).
        const bool ambient =
            t->kind == TypeKind::Object && t->name == "$ambient";
        if (!t->isUnknown() && !t->isError() && !ambient) return *t;
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
        case NodeKind::MemberExpr:
            return memberReadType(static_cast<const MemberExpr&>(e));
        case NodeKind::IndexExpr: {
            const auto& index = static_cast<const IndexExpr&>(e);
            if (!index.object) return Type::unknown();
            const Type recv = effectiveType(*index.object);
            if (recv.kind == TypeKind::Array && !recv.parts.empty()) {
                return recv.parts.front();
            }
            if (recv.kind == TypeKind::String) return Type::string();
            return Type::unknown();
        }
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
