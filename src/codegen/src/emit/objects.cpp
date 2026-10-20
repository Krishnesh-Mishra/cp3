// =============================================================================
// src/codegen/src/emit/objects.cpp - object value shapes: member reads and
// literal construction (STYLE.md §8).
//
// The two expression forms of a `$o{...}` value shape (Shape::Object, a
// generated value struct; support/value_shapes.cpp):
//
//   * a member read `o.a` - or the optional read `o?.a` on a `{...} | null`
//     receiver, which yields `std::optional<F>`;
//   * an object literal used as a value, `{ a: 1 }` -> the generated struct
//     built from its fields.
//
// Everything else (a computed/method key, a missing or extra field, a plain
// read through a possibly-null receiver) returns "" so the caller keeps its
// other paths and the unit reports CP3002 (or stays with the family lowering
// that already claimed the shape, like a std/json record).
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The literal name of an object-literal field key ("" when the property is out
// of this wave's scope: computed, a method, or a non-name key).
std::string fieldName(const ObjectProperty& p) {
    if (p.computed || p.isMethod || !p.key) return std::string();
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    return std::string();
}

// The value expression of a property (a shorthand `{ a }` reads its key).
const Expr* propertyValue(const ObjectProperty& p) {
    if (p.value) return p.value.get();
    if (p.shorthand && p.key) return p.key.get();
    return nullptr;
}

// The C++ spelling of the shape field named `property` (false when the shape
// has no such field). Defined in support/value_shapes.cpp.
}  // namespace

std::string Generator::objectLiteralForType(const ObjectLiteral& lit,
                                            const Type& declared) {
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(declared, structName, fields)) return std::string();
    std::vector<std::string> values;
    for (const ObjectField& field : fields) {
        const Expr* value = nullptr;
        for (const auto& propertyPtr : lit.properties) {
            if (!propertyPtr) continue;
            if (fieldName(*propertyPtr) == field.name) {
                value = propertyValue(*propertyPtr);
                break;
            }
        }
        if (value == nullptr) return std::string();
        if (field.type.kind == TypeKind::Object &&
            value->kind == NodeKind::ObjectLiteral) {
            const std::string nested = objectLiteralForType(
                static_cast<const ObjectLiteral&>(*value), field.type);
            if (!nested.empty()) {
                values.push_back(nested);
                continue;
            }
        }
        values.push_back(expr(*value));
    }
    std::string out = structName + "{";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += values[i];
    }
    out += "}";
    return out;
}

std::string Generator::emitObject(const Expr& e) {
    if (e.kind == NodeKind::ObjectLiteral) {
        const auto& literal = static_cast<const ObjectLiteral&>(e);
        Type shape = effectiveType(literal);
        if (shape.kind != TypeKind::Object) {
            // The initializer may have been checked against a nullish option
            // (`{...} | null`): peel to the object the literal denotes.
            Type object;
            bool optional = false;
            if (!objectReceiver(shape, object, optional)) return std::string();
            shape = object;
        }
        std::string structName;
        std::vector<ObjectField> fields;
        if (!objectShapeOf(shape, structName, fields)) return std::string();
        if (literal.properties.size() != fields.size()) return std::string();
        std::vector<std::string> init;
        for (const ObjectField& f : fields) {
            const Expr* value = nullptr;
            for (const auto& propertyPtr : literal.properties) {
                const ObjectProperty* p = propertyPtr.get();
                if (p && fieldName(*p) == f.name) {
                    value = propertyValue(*p);
                    break;
                }
            }
            if (!value) return std::string();  // missing / odd property
            init.push_back(expr(*value));
        }
        return structName + "{" + joinStrings(init, ", ") + "}";
    }
    if (e.kind != NodeKind::MemberExpr) return std::string();
    const auto& m = static_cast<const MemberExpr&>(e);
    if (!m.object || m.property.empty()) return std::string();
    Type object;
    bool optional = false;
    if (!objectReceiver(effectiveType(*m.object), object, optional)) {
        return std::string();
    }
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(object, structName, fields)) return std::string();
    std::string field;
    if (!fieldCppText(fields, m.property, field)) return std::string();
    (void)structName;  // the field spellings are all this read needs
    const std::string receiver = expr(*m.object);
    const std::string member = mapIdent(m.property);
    if (optional) {
        // `o?.a` over `{...} | null`: the read produces only when the receiver
        // holds a value (the empty optional is the `... | undefined`).
        if (!m.optional) return std::string();
        return "([&]{ auto cp3_o = (" + receiver +
               "); return cp3_o.has_value() ? std::optional<" + field +
               ">(cp3_o->" + member + ") : std::nullopt; }())";
    }
    if (m.optional && info(m).shape == Shape::Optional) {
        // `o?.a` where the receiver cannot be nullish: the read itself, sold as
        // the optional its cp3 type names.
        return "std::optional<" + field + ">((" + receiver + ")." + member + ")";
    }
    return "(" + receiver + ")." + member;
}

// The object-literal expression form: a `$o{...}` value shape lowers to the
// generated value struct (emitObject above); a literal with an enum-typed field
// lowers to a std::tuple (emit/enum.cpp); anything else stays CP3002.
std::string Generator::emitObjectLiteral(const Expr& e) {
    if (std::string object = emitObject(e); !object.empty()) {
        return object;
    }
    if (std::string literal =
            emitEnumObjectLiteral(static_cast<const ObjectLiteral&>(e));
        !literal.empty()) {
        return literal;
    }
    notImplemented(e, "this object literal");
    return "0.0";
}

// `obj.field = value` over a `$o{...}` value shape: a plain member write on the
// generated struct. Only the plain assignment is in this wave (a compound write
// keeps the CP3002 path); an optional receiver is left out for the same reason.
std::string Generator::emitObjectMemberWrite(const AssignmentExpr& a) {
    if (!a.target || a.target->kind != NodeKind::MemberExpr ||
        a.op != AssignOp::Assign || !a.value) {
        return std::string();
    }
    const auto& m = static_cast<const MemberExpr&>(*a.target);
    if (!m.object || m.property.empty()) return std::string();
    Type object;
    bool optional = false;
    if (!objectReceiver(effectiveType(*m.object), object, optional)) {
        return std::string();
    }
    if (optional) return std::string();
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(object, structName, fields)) return std::string();
    std::string field;
    if (!fieldCppText(fields, m.property, field)) return std::string();
    return "((" + expr(*m.object) + ")." + mapIdent(m.property) + " = " +
           expr(*a.value) + ")";
}

}  // namespace cp3
