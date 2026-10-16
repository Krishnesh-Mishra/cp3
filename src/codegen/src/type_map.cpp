// =============================================================================
// src/codegen/src/type_map.cpp — PRIVATE cp3-type -> C++-type mapping and the
// Generator's declaration-type environment / type resolution (STYLE.md §8).
//
// Implements type_map.h. Communication with sema is through the frozen
// `SemaResult` side tables (DECISIONS D1/D6/D14):
//   * expression/declared types        -> sema.nodeTypes (keyed by nodeId)
//   * resolved function signatures     -> sema.functions
//
// Type -> C++ representation (cp3/runtime.hpp):
//   number    -> double
//   string    -> std::string
//   boolean   -> bool
//   void      -> void
//   undefined -> cp3::runtime::Undefined        (value cp3::runtime::undefined)
//   null      -> std::nullptr_t                 (value nullptr)
//   T | null / T | undefined (T primitive) -> std::optional<T>
// =============================================================================
#include "generator.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "type_map.h"

namespace cp3 {

using namespace detail;

// -----------------------------------------------------------------------------
// Free helpers: shapes and the cp3-type -> C++-type mapping
// -----------------------------------------------------------------------------

namespace detail {

bool isPrimitiveShape(Shape s) {
    return s == Shape::Number || s == Shape::String || s == Shape::Bool;
}

NullishKind nullishKindOf(const Type& t) {
    if (t.isNullable()) {
        return NullishKind::Mixed;  // the `T?` shorthand: T | null | undefined
    }
    bool hasNull = false;
    bool hasUndef = false;
    for (const Type& part : t.parts) {
        if (part.isNull()) {
            hasNull = true;
        } else if (part.isUndefined()) {
            hasUndef = true;
        }
    }
    if (hasNull && hasUndef) return NullishKind::Mixed;
    if (hasNull) return NullishKind::Null;
    if (hasUndef) return NullishKind::Undefined;
    return NullishKind::None;
}

bool optionalEmptyMatches(NullishKind kind, Shape nullishShape) {
    const bool isNull = (nullishShape == Shape::Null);
    const bool isUndefined = (nullishShape == Shape::Undefined);
    switch (kind) {
        case NullishKind::Mixed: return isNull || isUndefined;
        case NullishKind::Null: return isNull;
        case NullishKind::Undefined: return isUndefined;
        default: return false;
    }
}

CppInfo classify(const Type& raw) {
    CppInfo info;
    Type t = raw;
    if (t.isLiteral()) {
        t = t.literalBaseType();
    }
    info.type = t;
    switch (t.kind) {
        case TypeKind::Number:
            info.shape = Shape::Number; info.representable = true; info.cpp = "double";
            return info;
        case TypeKind::String:
            info.shape = Shape::String; info.representable = true; info.cpp = "std::string";
            return info;
        case TypeKind::Boolean:
            info.shape = Shape::Bool; info.representable = true; info.cpp = "bool";
            return info;
        case TypeKind::Undefined:
            info.shape = Shape::Undefined; info.representable = true;
            info.cpp = "cp3::runtime::Undefined";
            return info;
        case TypeKind::Null:
            info.shape = Shape::Null; info.representable = true; info.cpp = "std::nullptr_t";
            return info;
        case TypeKind::Void:
            info.shape = Shape::Void; info.representable = true; info.cpp = "void";
            return info;
        case TypeKind::Unknown:
        case TypeKind::Error:
            info.shape = Shape::Unknown; info.representable = false;
            return info;
        case TypeKind::Nullable: {
            if (t.parts.size() == 1) {
                CppInfo inner = classify(t.parts.front());
                if (inner.representable && isPrimitiveShape(inner.shape)) {
                    info.shape = Shape::Optional; info.representable = true;
                    info.cpp = "std::optional<" + inner.cpp + ">";
                    info.emptyKind = NullishKind::Mixed;
                }
            }
            return info;
        }
        case TypeKind::Union: {
            std::vector<Type> nonNullish;
            for (const Type& part : t.parts) {
                if (!part.isNullish()) {
                    nonNullish.push_back(part);
                }
            }
            if (nonNullish.size() == 1) {
                CppInfo inner = classify(nonNullish.front());
                if (inner.representable && isPrimitiveShape(inner.shape)) {
                    info.shape = Shape::Optional; info.representable = true;
                    info.cpp = "std::optional<" + inner.cpp + ">";
                    info.emptyKind = nullishKindOf(t);
                }
            }
            return info;
        }
        default:
            return info;  // Array/Object/Function/Class/Enum/TypeParam: out of M1
    }
}

std::optional<Type> typeFromTypeNode(const TypeNode& tn) {
    switch (tn.kind) {
        case NodeKind::NamedType: {
            const auto& nt = static_cast<const NamedType&>(tn);
            if (nt.name == "number") return Type::number();
            if (nt.name == "string") return Type::string();
            if (nt.name == "boolean" || nt.name == "bool") return Type::boolean();
            if (nt.name == "void") return Type::voidType();
            if (nt.name == "undefined") return Type::undefined();
            if (nt.name == "null") return Type::nullType();
            if (nt.name == "unknown") return Type::unknown();
            return Type::named(TypeKind::Class, nt.name);
        }
        case NodeKind::UnionType: {
            std::vector<Type> members;
            for (const auto& part : static_cast<const UnionType&>(tn).types) {
                if (!part) return std::nullopt;
                auto mapped = typeFromTypeNode(*part);
                if (!mapped) return std::nullopt;
                members.push_back(*mapped);
            }
            return Type::unionOf(std::move(members));
        }
        case NodeKind::ArrayType: {
            const auto& at = static_cast<const ArrayType&>(tn);
            if (!at.element) return std::nullopt;
            auto element = typeFromTypeNode(*at.element);
            if (!element) return std::nullopt;
            return Type::array(*element);
        }
        case NodeKind::ParenthesizedType: {
            const auto& pt = static_cast<const ParenthesizedType&>(tn);
            if (!pt.inner) return std::nullopt;
            return typeFromTypeNode(*pt.inner);
        }
        default:
            return std::nullopt;
    }
}

bool isNullishLiteral(const Expr& e) {
    return e.kind == NodeKind::NullLiteral || e.kind == NodeKind::UndefinedLiteral;
}

Type stripNullish(const Type& t) {
    if (t.isNullish()) return Type::unknown();
    if (t.isUnion() || t.isNullable()) {
        std::vector<Type> kept;
        for (const Type& part : t.parts) {
            if (!part.isNullish()) kept.push_back(part);
        }
        if (kept.empty()) return Type::unknown();
        if (kept.size() == 1) return kept.front();
        return Type::unionOf(std::move(kept));
    }
    return t;
}

}  // namespace detail

// -----------------------------------------------------------------------------
// Generator: declaration-type environment
// -----------------------------------------------------------------------------

void Generator::pushScope() { env_.emplace_back(); }

void Generator::popScope() {
    if (!env_.empty()) {
        env_.pop_back();
    }
}

void Generator::declare(const std::string& name, const std::optional<Type>& type) {
    if (!type) return;
    if (env_.empty()) env_.emplace_back();
    env_.back()[name] = *type;
}

std::optional<Type> Generator::lookup(const std::string& name) const {
    for (auto it = env_.rbegin(); it != env_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// Generator: type resolution
// -----------------------------------------------------------------------------

std::optional<Type> Generator::bindingType(const VariableDeclarator& d) const {
    if (d.type) {
        if (auto t = typeFromTypeNode(*d.type)) {
            return t;
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
    if (e.kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(e);
        if (auto t = lookup(id.name)) return *t;
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
