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
// Out-of-scope forms report CP3002 (deduped per nodeId) and return a
// compilable stub; the generator never throws and never aborts.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

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
std::string Generator::templatePrefix(const FunctionDeclaration& fd) const {
    if (fd.typeParams.empty()) return std::string();
    std::string out = "template <";
    bool first = true;
    for (const auto& tp : fd.typeParams) {
        if (!tp) continue;
        if (!first) out += ", ";
        first = false;
        out += "typename " + mapIdent(tp->name);
    }
    if (first) return std::string();
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
// -> `identity<double>`. Types with no spelling here stay CP3002.
std::string Generator::typeArgText(const TypeNode& tn) {
    if (auto t = typeFromTypeNode(tn)) {
        const CppInfo ci = classify(*t);
        if (ci.representable) return ci.cpp;
    }
    notImplemented(tn, "this type argument");
    return "double";
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
                notImplemented(*p->value, "this object field value");
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
        out += (i == 0 ? "" : ", ") + expr(*element);
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
        out += expr(*el);
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
