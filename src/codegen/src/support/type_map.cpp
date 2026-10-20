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

#include "detail/class_model.h"
#include "detail/parallel.h"
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

// Is `inner` a value kind an optional (`T | null` / `T | undefined`) can
// carry? Primitives, class instances and structural objects have a spelling.
static bool optionalInnerShape(const CppInfo& inner) {
    return inner.representable &&
           (isPrimitiveShape(inner.shape) || inner.shape == Shape::ClassRef ||
            inner.shape == Shape::Object);
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
CppInfo classifyPromise(const Type& t) {
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

// `T | null` / `T | undefined` / the `T?` shorthand / `T | null | undefined`
// over a primitive `T` -> `std::optional<T>`, or the two-flavor `Maybe<T>`
// when both nullish kinds are present. Also the single-member option over an
// object value shape (`{...} | null` -> `std::optional<Struct>`). The
// primitive-only union spellings live in support/value_shapes.cpp.

Type promiseOf(const Type& value) {
    Type p(TypeKind::Class, "Promise");
    p.parts.push_back(value);
    return p;
}

Type promiseValue(const Type& t) {
    if (!isPromiseType(t) || t.parts.empty()) return Type::unknown();
    return t.parts.front();
}

// The five scalar arms of `classify` (Number, String, Boolean, Undefined,
// Null); false leaves the type to the structural switch.
static bool classifyScalar(TypeKind kind, CppInfo& info) {
    switch (kind) {
        case TypeKind::Number:
            info.shape = Shape::Number; info.representable = true; info.cpp = "double";
            return true;
        case TypeKind::String:
            info.shape = Shape::String; info.representable = true; info.cpp = "std::string";
            return true;
        case TypeKind::Boolean:
            info.shape = Shape::Bool; info.representable = true; info.cpp = "bool";
            return true;
        case TypeKind::Undefined:
            info.shape = Shape::Undefined; info.representable = true;
            info.cpp = "cp3::runtime::Undefined";
            return true;
        case TypeKind::Null:
            info.shape = Shape::Null; info.representable = true;
            info.cpp = "std::nullptr_t";
            return true;
        default:
            return false;
    }
}

CppInfo classify(const Type& raw) {
    CppInfo info;
    Type t = raw;
    if (t.isLiteral()) {
        t = t.literalBaseType();
    }
    info.type = t;
    // The parallel.run result marker must be caught before the Array arm:
    // classifyFamily keys on the element type alone and would spell it
    // `Array<double>`. Keeping it non-representable leaves the binding `auto`.
    if (isParallelRunType(t)) {
        info.shape = Shape::Unknown;
        return info;
    }
    if (classifyScalar(t.kind, info)) return info;
    switch (t.kind) {
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
                if (optionalInnerShape(inner)) {
                    info.shape = Shape::Optional; info.representable = true;
                    info.cpp = "std::optional<" + inner.cpp + ">";
                    info.emptyKind = NullishKind::Mixed;
                }
            }
            return info;
        }
        case TypeKind::Union:
            classifyValueUnion(t, info);
            if (!info.representable) classifyNullishUnion(t, info);
            return info;
        case TypeKind::Object: {
            // A registered interface/structural model first; the all-primitive
            // `$o{...}` value shape (support/value_shapes.cpp) otherwise.
            CppInfo model = classifyObjectModel(t);
            if (model.representable) return model;
            return classifyObjectValue(t);
        }
        case TypeKind::Class:
            return classifyClassValue(t);
        case TypeKind::Enum: return classifyEnum(t);  // spec 04: enum class
        case TypeKind::TypeParam:
            // A generic field/method type: the C++ spelling is the template
            // parameter itself (`T`), usable anywhere a value type is.
            info.type = t;
            info.shape = Shape::Unknown;
            info.representable = true;
            info.cpp = mapIdent(t.name);
            return info;
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
