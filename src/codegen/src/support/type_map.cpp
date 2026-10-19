// =============================================================================
// src/codegen/src/support/type_map.cpp - PRIVATE cp3-type -> C++-type mapping and the
// Generator's declaration-type environment / type resolution (STYLE.md §8).
//
// Implements type_map.h. Communication with sema is through the frozen
// `SemaResult` side tables (DECISIONS D1/D6/D14):
//   * expression/declared types        -> sema.nodeTypes (keyed by nodeId)
//   * resolved function signatures     -> sema.functions
//
// Type -> C++ representation (cp3/core/runtime.hpp):
//   number    -> double
//   string    -> std::string
//   boolean   -> bool
//   void      -> void
//   undefined -> cp3::runtime::Undefined        (value cp3::runtime::undefined)
//   null      -> std::nullptr_t                 (value nullptr)
//   T | null / T | undefined (T primitive) -> std::optional<T>
//   T | null | undefined (T primitive)     -> cp3::runtime::Maybe<T>
//   Enum (spec 04)                         -> enum class <name> : long long
//   EventEmitter (D74)                     -> cp3::runtime::Ref<cp3::runtime::EventEmitter>
//   (...args: unknown[]) => void (D74)     -> cp3::runtime::Listener
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail/type_map.h"

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

bool isPromiseType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Promise";
}

bool isEventEmitterType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "EventEmitter";
}

// `Promise<T>` -> `::cp3::runtime::Promise<T>` (cp3/async/promise.hpp). Only
// M1-representable value types (primitives and `void`) have a spelling;
// anything else stays not-implemented. Implementation detail of `classify`.
static CppInfo classifyPromise(const Type& t) {
    CppInfo info;
    const Type inner = promiseValue(t);
    if (inner.isVoid()) {
        info.shape = Shape::Promise; info.representable = true;
        info.cpp = "::cp3::runtime::Promise<void>";
        return info;
    }
    const CppInfo ic = classify(inner);
    if (ic.representable && isPrimitiveShape(ic.shape)) {
        info.shape = Shape::Promise; info.representable = true;
        info.cpp = "::cp3::runtime::Promise<" + ic.cpp + ">";
    }
    return info;
}

// The Class-kind tail of `classify`: the family spellings (Buffer, typed
// views, Map/Set, Date), then the builtin EventEmitter (D74), then `Promise<T>`
// (the only other class with a spelling). Anything else stays out of M1.
static CppInfo classifyClassValue(const Type& t) {
    CppInfo info;
    info.type = t;
    // The nominal std/buffer `Buffer` (src/std/buffer/codegen.cpp).
    if (classifyByteBuffer(t, info)) return info;
    // The JS-compat family (typed views, buffers, DataView).
    if (classifyFamily(t, info)) return info;
    // The std/collections family (`Map<K, V>` / `Set<T>`).
    if (classifyCollections(t, info)) return info;
    // The `std/date` value type (src/std/date/codegen.cpp).
    if (classifyDate(t, info)) return info;
    // The builtin EventEmitter (D74): a reference type, so its C++
    // spelling is `Ref<EventEmitter>` (cp3/async/emitter.hpp).
    if (t.name == "EventEmitter") {
        info.shape = Shape::EventEmitter;
        info.representable = true;
        info.cpp = "::cp3::runtime::Ref<::cp3::runtime::EventEmitter>";
        return info;
    }
    // `Promise<T>` is the only other class type with a spelling (M1).
    if (isPromiseType(t)) return classifyPromise(t);
    return info;
}

// `T | null` / `T | undefined` / the `T?` shorthand / `T | null | undefined`
// over a primitive `T` -> `std::optional<T>`, or the two-flavor `Maybe<T>`
// when both nullish kinds are present (an implementation detail of
// `classify`). Any other union has no M1 spelling.
static void classifyNullishUnion(const Type& t, CppInfo& info) {
    std::vector<Type> nonNullish;
    for (const Type& part : t.parts) {
        if (!part.isNullish()) {
            nonNullish.push_back(part);
        }
    }
    if (nonNullish.size() != 1) return;
    const CppInfo inner = classify(nonNullish.front());
    if (!inner.representable || !isPrimitiveShape(inner.shape)) return;
    info.representable = true;
    info.emptyKind = nullishKindOf(t);
    if (info.emptyKind == NullishKind::Mixed) {
        // `T | null | undefined` must keep the two nullish flavors apart
        // (JS `null !== undefined`), which no `std::optional` can: use the
        // flavored Maybe<T>.
        info.shape = Shape::Maybe;
        info.cpp = "cp3::runtime::Maybe<" + inner.cpp + ">";
    } else {
        info.shape = Shape::Optional;
        info.cpp = "std::optional<" + inner.cpp + ">";
    }
}

Type promiseOf(const Type& value) {
    Type p(TypeKind::Class, "Promise");
    p.parts.push_back(value);
    return p;
}

Type promiseValue(const Type& t) {
    if (!isPromiseType(t) || t.parts.empty()) return Type::unknown();
    return t.parts.front();
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
        case TypeKind::Union:
            classifyNullishUnion(t, info);
            return info;
        case TypeKind::Class:
            return classifyClassValue(t);
        case TypeKind::Enum: return classifyEnum(t);  // spec 04: enum class
        case TypeKind::Function:
            // A function-typed value (D74): boxed as `Listener` so every copy
            // shares one identity; only void-returning functions have one.
            if (t.parts.empty() || !t.parts.back().isVoid()) return info;
            info.shape = Shape::Listener;
            info.representable = true;
            info.cpp = "::cp3::runtime::Listener";
            return info;
        case TypeKind::Array:
            // Regular arrays have a spelling when their element type does
            // (src/std/typed-arrays/codegen.cpp); otherwise they stay out of
            // M1.
            classifyFamily(t, info);
            return info;
        default:
            return info;  // Object/TypeParam: out of M1
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
            if (nt.name == "Promise") {
                // `Promise<T>` (spec §14); a bare `Promise` is `Promise<unknown>`.
                Type p(TypeKind::Class, "Promise");
                Type inner = Type::unknown();
                if (!nt.typeArgs.empty() && nt.typeArgs.front()) {
                    auto mapped = typeFromTypeNode(*nt.typeArgs.front());
                    if (!mapped) return std::nullopt;
                    inner = *mapped;
                }
                p.parts.push_back(inner);
                return p;
            }
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

namespace {

// 2^53: the largest magnitude at which every integer is exact in both the
// int64 and the double domain (the int64 fast path's invariant, D64).
constexpr double kIntegralLimit = 9007199254740992.0;

// The value of a `NumberLiteral`, possibly negated by a unary `-` (the parser
// keeps `-5` as `-(5)`), or nullopt when `e` is not a number literal.
std::optional<double> literalNumber(const Expr& e) {
    if (e.kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(e).value;
    }
    if (e.kind == NodeKind::UnaryExpr) {
        const auto& u = static_cast<const UnaryExpr&>(e);
        if (u.op == UnaryOp::Negate && u.operand) {
            if (auto v = literalNumber(*u.operand)) return -*v;
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::int64_t> integralLiteralValue(const Expr& e) {
    const auto v = literalNumber(e);
    if (!v) return std::nullopt;
    // The range tests are false for NaN, so a NaN literal is rejected here.
    if (!(*v >= -kIntegralLimit && *v <= kIntegralLimit)) return std::nullopt;
    const std::int64_t i = static_cast<std::int64_t>(*v);
    // The conversion truncates, so this also proves the literal is integral.
    if (static_cast<double>(i) != *v) return std::nullopt;
    return i;
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

// `null`, `undefined`, or a union of only nullish members: a value that is
// always nullish, so `??` can drop it outright.
bool isStaticallyNullish(const Type& t) {
    if (t.isNullish()) return true;
    if (t.isUnion() && !t.parts.empty()) {
        for (const Type& part : t.parts) {
            if (!part.isNullish()) return false;
        }
        return true;
    }
    return false;
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
        if (auto t = typeOfNode(*d.type)) {
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
    if (const auto t = networkMapFieldType(e)) return *t;
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
