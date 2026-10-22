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

#include "cp3/native_types.hpp"

#include "detail/class_model.h"
#include "detail/cpp_writer.h"
#include "detail/parallel.h"
#include "detail/type_map.h"
#include "detail/worker.h"

namespace cp3 {

using namespace detail;

// -----------------------------------------------------------------------------
// Free helpers: shapes and the cp3-type -> C++-type mapping
// -----------------------------------------------------------------------------

namespace detail {

bool isPrimitiveShape(Shape s) {
    return s == Shape::Number || s == Shape::String || s == Shape::Bool ||
           s == Shape::Native;
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

// `Promise<T>` -> ... the mapping lives in support/type_classify.cpp.

Type promiseOf(const Type& value) {
    Type p(TypeKind::Class, "Promise");
    p.parts.push_back(value);
    return p;
}

Type promiseValue(const Type& t) {
    if (!isPromiseType(t) || t.parts.empty()) return Type::unknown();
    return t.parts.front();
}

std::optional<Type> instantiateGenericInterface(const TypeNode& tn) {
    if (tn.kind != NodeKind::NamedType) return std::nullopt;
    const auto& nt = static_cast<const NamedType&>(tn);
    ModelRegistry& reg = ModelRegistry::get();
    const GenericInterfaceModel* generic = reg.genericInterface(nt.name);
    if (generic == nullptr || generic->decl == nullptr) return std::nullopt;
    // The type arguments of the use site.
    std::vector<Type> args;
    for (const auto& ta : nt.typeArgs) {
        if (!ta) continue;
        args.push_back(modelTypeFromTypeNode(*ta));
    }
    // The interface's members, with the type arguments substituted in.
    std::vector<ObjectFieldModel> fields;
    for (const auto& mp : generic->decl->members) {
        if (!mp) continue;
        ObjectFieldModel f;
        if (mp->kind == NodeKind::PropertySignature) {
            const auto& ps = static_cast<const PropertySignature&>(*mp);
            f.name = keyNameOf(ps.key.get());
            f.optional = ps.optional;
            if (ps.type) f.type = modelTypeFromTypeNode(*ps.type);
        } else if (mp->kind == NodeKind::MethodSignature) {
            const auto& ms = static_cast<const MethodSignature&>(*mp);
            f.name = keyNameOf(ms.key.get());
            f.optional = ms.optional;
            std::vector<Type> params;
            for (const auto& p : ms.params) {
                if (!p) continue;
                params.push_back(p->type ? modelTypeFromTypeNode(*p->type)
                                         : Type::unknown());
            }
            const Type ret = ms.returnType ? modelTypeFromTypeNode(*ms.returnType)
                                           : Type::unknown();
            f.type = Type::function(std::move(params), ret);
            f.isFunction = true;
            f.isMethod = true;
        } else {
            continue;
        }
        if (f.name.empty()) continue;
        rewriteTypeParams(f.type, generic->typeParams);
        f.type = substituteTypeArgs(f.type, generic->typeParams, args);
        fields.push_back(std::move(f));
    }
    // The shape label: the same spelling sema computes for the instantiated
    // members, so both sides name one shape.
    const std::string label = objectLabelOf(fields);
    // The wrapper name is per instantiation (two instantiations of one
    // interface are two shapes with two wrappers).
    reg.addInterface(nt.name, label, fields, /*generic=*/true);
    return Type(TypeKind::Object, label);
}

std::optional<Type> typeFromTypeNode(const TypeNode& tn, TypeSystem typeSystem) {
    switch (tn.kind) {
        case NodeKind::PrefixedType: {
            // `r_'u32'`, `c_'long long'`, `ts_'number'` (spec 03). An
            // unresolved spelling returns nullopt; sema has already reported
            // the precise diagnostic that stops the pipeline.
            const auto& pt = static_cast<const PrefixedType&>(tn);
            return resolveNativeTypeName(pt.prefix, pt.name).type;
        }
        case NodeKind::NamedType: {
            const auto& nt = static_cast<const NamedType&>(tn);
            if (nt.name == "number") return Type::number();
            if (nt.name == "string") return Type::string();
            if (nt.name == "boolean" || nt.name == "bool") return Type::boolean();
            if (nt.name == "void") return Type::voidType();
            if (nt.name == "undefined") return Type::undefined();
            if (nt.name == "null") return Type::nullType();
            if (nt.name == "unknown") return Type::unknown();
            // The project's own plain vocabulary (`i64`, `long long`, ...).
            // TypeScript names above keep working in every project.
            if (typeSystem != TypeSystem::TypeScript) {
                const NativeNameResult native =
                    resolvePlainNativeTypeName(typeSystem, nt.name);
                if (native.type) return *native.type;
            }
            if (nt.name == "Promise") {
                // `Promise<T>` (spec §14); a bare `Promise` is `Promise<unknown>`.
                Type p(TypeKind::Class, "Promise");
                Type inner = Type::unknown();
                if (!nt.typeArgs.empty() && nt.typeArgs.front()) {
                    auto mapped =
                        typeFromTypeNode(*nt.typeArgs.front(), typeSystem);
                    if (!mapped) return std::nullopt;
                    inner = *mapped;
                }
                p.parts.push_back(inner);
                return p;
            }
            // A generic interface (D26): the type arguments are substituted
            // into the interface's members, and the instantiated shape is
            // registered as an interface model.
            if (std::optional<Type> generic = instantiateGenericInterface(nt)) {
                return generic;
            }
            // An interface name resolves to its structural object type
            // (spec 15); only names registered by the model pass qualify.
            if (const ObjectModel* om =
                    ModelRegistry::get().interfaceByName(nt.name)) {
                return Type(TypeKind::Object, om->label);
            }
            Type named = Type::named(TypeKind::Class, nt.name);
            for (const auto& ta : nt.typeArgs) {
                if (!ta) continue;
                auto mapped = typeFromTypeNode(*ta, typeSystem);
                if (!mapped) return std::nullopt;
                named.parts.push_back(*mapped);
            }
            return named;
        }
        case NodeKind::ObjectType: {
            // `{ x: number }`: register the field struct and name it by the
            // sema object label so the literal emitter can find it.
            const auto& ot = static_cast<const ObjectType&>(tn);
            std::vector<ObjectFieldModel> fields;
            for (const auto& mp : ot.members) {
                if (!mp) continue;
                ObjectFieldModel f;
                if (mp->kind == NodeKind::PropertySignature) {
                    const auto& ps = static_cast<const PropertySignature&>(*mp);
                    f.name = keyNameOf(ps.key.get());
                    if (f.name.empty()) continue;
                    if (ps.type) {
                        f.type = typeFromTypeNode(*ps.type, typeSystem)
                                     .value_or(Type::unknown());
                    }
                    f.optional = ps.optional;
                } else if (mp->kind == NodeKind::MethodSignature) {
                    const auto& ms = static_cast<const MethodSignature&>(*mp);
                    f.name = keyNameOf(ms.key.get());
                    if (f.name.empty()) continue;
                    std::vector<Type> params;
                    for (const auto& p : ms.params) {
                        if (!p) continue;
                        params.push_back(
                            p->type ? typeFromTypeNode(*p->type, typeSystem)
                                          .value_or(Type::unknown())
                                    : Type::unknown());
                    }
                    const Type ret =
                        ms.returnType
                            ? typeFromTypeNode(*ms.returnType, typeSystem)
                                  .value_or(Type::unknown())
                            : Type::unknown();
                    f.type = Type::function(std::move(params), ret);
                    f.isFunction = true;
                } else {
                    continue;
                }
                fields.push_back(std::move(f));
            }
            const std::string label = objectLabelOf(fields);
            ModelRegistry::get().ensureObject(label, fields);
            return Type(TypeKind::Object, label);
        }
        case NodeKind::UnionType: {
            std::vector<Type> members;
            for (const auto& part : static_cast<const UnionType&>(tn).types) {
                if (!part) return std::nullopt;
                auto mapped = typeFromTypeNode(*part, typeSystem);
                if (!mapped) return std::nullopt;
                members.push_back(*mapped);
            }
            return Type::unionOf(std::move(members));
        }
        case NodeKind::ArrayType: {
            const auto& at = static_cast<const ArrayType&>(tn);
            if (!at.element) return std::nullopt;
            auto element = typeFromTypeNode(*at.element, typeSystem);
            if (!element) return std::nullopt;
            return Type::array(*element);
        }
        case NodeKind::TupleType: {
            // `[A, B]` -> a tuple Type (label `$t`, elements in `parts`); the
            // sema-built spelling carries the full label, but codegen only
            // needs the prefix and the elements.
            const auto& tt = static_cast<const TupleType&>(tn);
            Type out(TypeKind::Object, "$t");
            for (const auto& element : tt.elements) {
                if (!element) return std::nullopt;
                auto mapped = typeFromTypeNode(*element, typeSystem);
                if (!mapped) return std::nullopt;
                out.parts.push_back(*mapped);
            }
            return out;
        }
        case NodeKind::ParenthesizedType: {
            const auto& pt = static_cast<const ParenthesizedType&>(tn);
            if (!pt.inner) return std::nullopt;
            return typeFromTypeNode(*pt.inner, typeSystem);
        }
        case NodeKind::FunctionType: {
            // `(a: T, b: U) => R`: a parameter with no annotation is
            // `unknown`; the return type is required for a useful shape.
            const auto& ft = static_cast<const FunctionType&>(tn);
            if (!ft.returnType) return std::nullopt;
            auto ret = typeFromTypeNode(*ft.returnType);
            if (!ret) return std::nullopt;
            std::vector<Type> params;
            for (const auto& part : ft.params) {
                if (!part) return std::nullopt;
                if (part->type) {
                    auto param = typeFromTypeNode(*part->type);
                    if (!param) return std::nullopt;
                    params.push_back(*param);
                } else {
                    params.push_back(Type::unknown());
                }
            }
            return Type::function(std::move(params), *ret);
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

}  // namespace cp3
