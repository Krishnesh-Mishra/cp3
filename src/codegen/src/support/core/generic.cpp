// =============================================================================
// src/codegen/src/support/generic.cpp - generic function templates + tuples
// (spec §16).
//
// A generic function declared in this unit lowers to a real C++ function
// template: `template <typename T> T identity(T x)`. Explicit call-site type
// arguments become the template argument list (`identity<number>(5)` ->
// `identity<double>(5.0)`); with none, C++ deduction at the call site does the
// work, which is what the inferred cases (`identity("hi")`) rely on. Inside a
// body a type parameter's C++ spelling is its name, so `p.x` (receiver of type
// `T extends Point2`) reads the member off the template parameter directly
// (emit/expr.cpp).
//
// A tuple (`[A, B]`) lowers to `std::tuple<A, B>`: a `return [a, b]` in a
// tuple-returning body becomes `std::make_tuple(a, b)` and a constant index
// read `p[1]` becomes `std::get<1>(p)`.
//
// An object-literal argument to a generic function (`getX({ x: 5 })`) has no
// erased C++ spelling: it is reified into a generated aggregate struct named
// after the literal's node id, materialized into the decls section (the
// `genericDecls_` buffer) ahead of the unit's own declarations.
//
// A generic arrow (`<T,>(x: T) => x`, spec §16) is the arrow-shaped member of
// the same family: it lowers to a C++ generic lambda whose template
// parameters are its own, so the call site either lets C++ deduce them
// (`f(1)`) or names them explicitly (`f<number>(x)`).
//
// Unsupported forms report a codegen diagnostic (deduped per nodeId) and
// return a compilable stub; the generator never throws and never aborts.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The declared name of an object-literal property (`{ x: 5 }` -> "x"). A
// computed key names the same field as its literal value (`{ ["x"]: 5 }`);
// a computed key with a dynamic name - and a method field - has no static
// name at all (the object's shape is fixed at compile time).
std::string objectFieldName(const ObjectProperty& p) {
    if (!p.key) return std::string();
    if (p.computed) {
        if (p.key->kind == NodeKind::StringLiteral) {
            return static_cast<const StringLiteral&>(*p.key).value;
        }
        if (p.key->kind == NodeKind::NumberLiteral) {
            return static_cast<const NumberLiteral&>(*p.key).raw;
        }
        return std::string();
    }
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    return std::string();
}

}  // namespace

// The `template <typename T, ...>` prefix of a generic function, "" for an
// ordinary one (the signature lines of emit/decl.cpp are prefixed with it).
// An untyped parameter adds a template parameter of its own: its cp3 type is
// unknown, so the C++ side carries it as a deduced template parameter (the
// same fallback an untyped arrow parameter gets).
std::string Generator::templatePrefix(const FunctionDeclaration& fd) {
    std::vector<std::string> params;
    for (const auto& tp : fd.typeParams) {
        if (!tp) continue;
        params.push_back(mapIdent(tp->name));
    }
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        if (!fd.params[i]) continue;
        const Parameter& p = *fd.params[i];
        if (p.isRest) {
            // An untyped rest parameter carries its element type as a
            // template parameter too (`...items` -> `Array<cp3_T0>`).
            const std::optional<Type> pt = parameterType(p, fd, i);
            if (pt && pt->kind == TypeKind::Array && !pt->parts.empty() &&
                pt->parts.front().name == untypedParamTemplate(i)) {
                params.push_back(untypedParamTemplate(i));
            }
            continue;
        }
        if (!parameterCppType(p, fd, i).empty()) continue;
        params.push_back(untypedParamTemplate(i));
    }
    if (params.empty()) return std::string();
    std::string out = "template <";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0) out += ", ";
        out += "typename " + params[i];
    }
    return out + "> ";
}

// The C++ callee of a call to a function declared in this unit: the mangled
// name, plus the explicit type-argument list of a generic call
// (`identity<number>(...)` -> `identity<double>(...)`; no `<...>` in the source
// leaves the name alone, so C++ deduction applies).
std::string Generator::calleeText(const std::string& name, const CallExpr& c) {
    if (c.typeArgs.empty()) return mapIdent(name);
    std::vector<std::string> typeArgs;
    for (const auto& ta : c.typeArgs) {
        if (ta) typeArgs.push_back(typeArgText(*ta));
    }
    return mapIdent(name) + "<" + joinStrings(typeArgs, ", ") + ">";
}

// The C++ spelling of an explicit call-site type argument: `identity<number>`
// -> `identity<double>`. A type with no spelling here is an invalid type
// argument and reports a diagnostic.
std::string Generator::typeArgText(const TypeNode& tn) {
    if (auto t = typeFromTypeNode(tn)) {
        const CppInfo ci = classify(*t);
        if (ci.representable) return ci.cpp;
    }
    // A type argument that names a type parameter of the enclosing generic
    // function (`identity<T>(x)`) has no erased spelling: the structural
    // re-parse sees an unknown class name, but sema resolved the node to the
    // `TypeParam` whose C++ spelling is the template parameter itself.
    if (const Type* t = sema_.typeOf(tn.nodeId)) {
        const CppInfo ci = classify(*t);
        if (ci.representable) return ci.cpp;
    }
    reportUnsupported(
        tn, diagcode::kInvalidTypeArgument,
        "invalid type argument: this type has no C++ spelling; a generic type "
        "argument must be a concrete value type");
    return "double";
}

// The explicit type arguments of a generic call, resolved to cp3 types
// (`identity<number>(x)` -> [number]). A type argument sema could not
// resolve stays unknown.
std::vector<Type> Generator::resolvedTypeArgs(const CallExpr& c) const {
    std::vector<Type> args;
    args.reserve(c.typeArgs.size());
    for (const auto& ta : c.typeArgs) {
        if (!ta) {
            args.push_back(Type::unknown());
            continue;
        }
        Type resolved = Type::unknown();
        if (const Type* published = sema_.typeOf(ta->nodeId)) {
            if (!published->isUnknown() && !published->isError()) {
                resolved = *published;
            }
        } else if (auto mapped = typeFromTypeNode(*ta, options_.typeSystem)) {
            resolved = *mapped;
        }
        args.push_back(std::move(resolved));
    }
    return args;
}

// A type with the callee's type parameters replaced by the explicit type
// arguments of the call. The call site spells the substituted types: a rest
// parameter packs its arguments into an array of the substituted element
// type, and C++ then deduces the callee's type parameter from that array.
Type Generator::substituteTypeArgs(const Type& t, const FunctionDeclaration& fd,
                                   const CallExpr& c) const {
    if (fd.typeParams.empty() || c.typeArgs.empty()) return t;
    return substituteTypeParams(t, fd, resolvedTypeArgs(c));
}

// The same substitution over a type tree: a `TypeParam` part named like one
// of the callee's type parameters becomes the matching type argument.
Type Generator::substituteTypeParams(const Type& t, const FunctionDeclaration& fd,
                                     const std::vector<Type>& args) const {
    if (t.kind == TypeKind::TypeParam) {
        for (std::size_t i = 0; i < fd.typeParams.size() && i < args.size(); ++i) {
            const TypeParameter* tp = fd.typeParams[i].get();
            if (tp != nullptr && tp->name == t.name) return args[i];
        }
        return t;
    }
    Type out = t;
    for (Type& part : out.parts) {
        part = substituteTypeParams(part, fd, args);
    }
    return out;
}

// Is `name` a binding whose value is a generic arrow (`let f = <T>(x: T) =>
// x`)? Such a value is a C++ generic lambda: the template parameters are the
// lambda's own, so the call site names them (spec §16).
bool Generator::isGenericArrowBinding(const std::string& name) const {
    const auto it = captures_.arrowOf.find(name);
    if (it == captures_.arrowOf.end()) return false;
    return !it->second->typeParams.empty();
}

// Is this declarator's initializer a generic arrow? A generic lambda's
// `operator()` is not const-qualified (the closure is `mutable`, so a
// captured cell stays writable), so the C++ binding cannot be a `const`
// object - cp3's own const-ness is enforced by sema (a reassignment is
// CP3408).
bool Generator::isGenericArrowInit(const VariableDeclarator& d) const {
    if (!d.init || d.init->kind != NodeKind::ArrowFunction) return false;
    return !static_cast<const ArrowFunction&>(*d.init).typeParams.empty();
}

// The callee text of a call through a generic arrow binding with an explicit
// type-argument list (`f<number>(x)`): the C++ call instantiates the generic
// lambda's template explicitly. Without a type-argument list the ordinary
// call lets C++ deduce the template parameters.
std::string Generator::genericArrowCallee(const std::string& callee,
                                          const CallExpr& c) {
    std::vector<std::string> typeArgs;
    typeArgs.reserve(c.typeArgs.size());
    for (const auto& ta : c.typeArgs) {
        if (ta) typeArgs.push_back(typeArgText(*ta));
    }
    return callee + ".operator()<" + joinStrings(typeArgs, ", ") + ">";
}

// An object-literal argument to a generic function reified as a generated
// aggregate struct (`getX({ x: 5 })` -> `cp3_obj_12{5.0}` plus the struct
// definition in genericDecls_). A nested object value reifies recursively; its
// struct definition lands before the enclosing one.
std::string Generator::reifyObjectArg(const ObjectLiteral& lit) {
    const std::string name = "cp3_obj_" + std::to_string(lit.nodeId);
    std::string fields;
    std::string values;
    for (const auto& propertyPtr : lit.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (p == nullptr || !p->value) continue;
        const std::string key = objectFieldName(*p);
        if (key.empty()) {
            // The object's shape is fixed at compile time: a computed key
            // with a dynamic name (and a method field) names no field the
            // generic body could read.
            reportUnsupported(
                *p, diagcode::kIndexSignatureBanned,
                "a computed object key is not supported: an object's keys are "
                "fixed at compile time (use Map<K, V> for dynamic keys)");
            continue;
        }
        std::string type;
        std::string value;
        if (p->value->kind == NodeKind::ObjectLiteral) {
            const auto& nested = static_cast<const ObjectLiteral&>(*p->value);
            value = reifyObjectArg(nested);
            type = "cp3_obj_" + std::to_string(nested.nodeId);
        } else {
            const CppInfo vi = info(*p->value);
            if (vi.representable && vi.shape != Shape::Void) {
                type = vi.cpp;
            } else {
                reportUnsupported(
                    *p->value, diagcode::kTypeMismatch,
                    "type mismatch: object field '" + key +
                        "' has no C++ value type");
                type = "double";
            }
            value = expr(*p->value);
        }
        fields += "    " + type + " " + mapIdent(key) + ";\n";
        if (!values.empty()) values += ", ";
        values += value;
    }
    if (genericDecls_.find("struct " + name + " ") == std::string::npos) {
        genericDecls_ += "struct " + name + " {\n" + fields + "};\n\n";
    }
    return name + "{" + values + "}";
}

// The tuple declarator's initializer: `let t: [string, number] = ["a", 1]`.
// The tuple annotation fixes the element types, so the literal is
// brace-initialized into the std::tuple directly instead of lowering as an
// Array (spec 03). "" means the declaration is not a tuple literal.
std::string Generator::tupleDeclaratorInitText(const VariableDeclarator& d,
                                                const Type& declared) {
    if (!d.init || !isTupleType(declared) ||
        d.init->kind != NodeKind::ArrayLiteral) {
        return std::string();
    }
    const auto& lit = static_cast<const ArrayLiteral&>(*d.init);
    std::string cpp;
    if (lit.elements.size() != declared.parts.size() ||
        !tupleCpp(declared, cpp)) {
        return std::string();
    }
    needsTuple_ = true;
    std::string out = cpp + "{";
    for (std::size_t i = 0; i < lit.elements.size(); ++i) {
        const Expr* element = lit.elements[i].get();
        if (element == nullptr || element->kind == NodeKind::SpreadElement) {
            return std::string();
        }
        // A `number` slot reads an enum element's underlying value
        // (spec 04 / TS parity).
        const std::optional<Type> savedExpected = expected_;
        if (!declared.parts[i].isUnknown() && !declared.parts[i].isError()) {
            expected_ = declared.parts[i];
        }
        out += (i == 0 ? "" : ", ") + expr(*element);
        expected_ = savedExpected;
    }
    return out + "}";
}

// `return [a, b]` in a tuple-returning body -> `std::make_tuple(a, b)`.
std::string Generator::emitTupleLiteral(const ArrayLiteral& lit) {
    needsTuple_ = true;
    std::string out = "std::make_tuple(";
    for (std::size_t i = 0; i < lit.elements.size(); ++i) {
        if (i != 0) out += ", ";
        const Expr* el = lit.elements[i].get();
        if (el == nullptr || el->kind == NodeKind::SpreadElement) {
            // A tuple's shape is fixed (`std::tuple<A, B>`), so a spread
            // element has no position in it (sema reports it as CP3401).
            reportUnsupported(lit, diagcode::kTypeMismatch,
                              "type mismatch: a spread element has no fixed "
                              "position in a tuple literal");
            continue;
        }
        // A `number` slot reads an enum element's underlying value
        // (spec 04 / TS parity).
        const std::optional<Type> savedExpected = expected_;
        if (currentReturnType_ && i < currentReturnType_->parts.size() &&
            !currentReturnType_->parts[i].isUnknown() &&
            !currentReturnType_->parts[i].isError()) {
            expected_ = currentReturnType_->parts[i];
        }
        out += expr(*el);
        expected_ = savedExpected;
    }
    return out + ")";
}

// A constant index read of a tuple: `p[1]` -> `std::get<1>(p)`. Returns "" when
// the receiver is not a tuple, so the caller keeps its other index paths.
std::string Generator::emitGenericIndexRead(const IndexExpr& ix) {
    if (!ix.object || !ix.index) return std::string();
    const Type ot = effectiveType(*ix.object);
    if (!isTupleType(ot) || ot.parts.empty()) return std::string();
    const std::optional<std::int64_t> index = integralLiteralValue(*ix.index);
    if (!index || *index < 0 ||
        static_cast<std::size_t>(*index) >= ot.parts.size()) {
        return std::string();
    }
    needsTuple_ = true;
    return "std::get<" + std::to_string(*index) + ">(" + expr(*ix.object) + ")";
}

}  // namespace cp3
