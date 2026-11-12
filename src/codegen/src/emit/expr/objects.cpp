// =============================================================================
// src/codegen/src/emit/objects.cpp - object value shapes: member reads and
// literal construction (STYLE.md §8).
//
// The two expression forms of a `$o{...}` shape (Shape::Object, a generated
// struct `cp3_obj_*` held through the reference model `Ref<struct>`, spec 10):
//
//   * a member read `o.a` - or the optional read `o?.a` on a `{...} | null`
//     receiver, which yields `std::optional<F>`;
//   * an object literal used as a value, `{ a: 1 }` -> a fresh `makeRef` of the
//     generated struct built from its fields (share on copy, spec 10).
//
// Everything else (a computed/method key, a missing or extra field, a plain
// read through a possibly-null receiver) returns "" so the caller keeps its
// other paths, or stays with the family lowering that already claimed the
// shape (like a std/json record).
// =============================================================================
#include "detail/generator/generator.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The literal name of an object-literal field key ("" when the property is out
// of this wave's scope: a method, or a computed name that is not a literal).
std::string fieldName(const ObjectProperty& p) {
    if (p.isMethod) return std::string();
    return classMemberName(p.key.get(), p.computed);
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
    if (!objectShapeOf(declared, structName, fields)) {
        // The declared shape has no value-shape spelling (an optional field,
        // say): the object model owns it, and its struct is the field list.
        const ObjectModel* om = objectModelOf(declared);
        if (om == nullptr || !objectModelUsed(*om)) return std::string();
        // An interface is built through its literal struct (the wrapper takes
        // a class handle instead).
        structName = om->isInterface ? om->literalName : om->structName;
        if (structName.empty()) return std::string();
        for (const ObjectFieldModel& f : om->fields) {
            if (f.isFunction) continue;
            fields.push_back(ObjectField{f.name, objectFieldCpp(f), f.type});
        }
    }
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
        if (value == nullptr) {
            // An optional field may be absent: the empty optional is the
            // cp3 `undefined` a pattern default fills.
            if (field.cpp.rfind("std::optional<", 0) == 0) {
                values.push_back("std::nullopt");
                continue;
            }
            return std::string();
        }
        if (field.type.kind == TypeKind::Object &&
            value->kind == NodeKind::ObjectLiteral) {
            const std::string nested = objectLiteralForType(
                static_cast<const ObjectLiteral&>(*value), field.type);
            if (!nested.empty()) {
                values.push_back(nested);
                continue;
            }
        }
        const std::optional<Type> savedExpected = expected_;
        expected_ = field.type;
        values.push_back(expr(*value));
        expected_ = savedExpected;
    }
    std::string out = "::cp3::runtime::makeRef<" + structName + ">(" +
                      structName + "{";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += values[i];
    }
    out += "})";
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
            const std::optional<Type> savedExpected = expected_;
            expected_ = f.type;
            init.push_back(expr(*value));
            expected_ = savedExpected;
        }
        return "::cp3::runtime::makeRef<" + structName + ">(" + structName +
               "{" + joinStrings(init, ", ") + "})";
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
               ">((*cp3_o)->" + member + ") : std::nullopt; }())";
    }
    if (m.optional && info(m).shape == Shape::Optional) {
        // `o?.a` where the receiver cannot be nullish: the read itself, sold as
        // the optional its cp3 type names.
        return "std::optional<" + field + ">((" + receiver + ")->" + member + ")";
    }
    // The declared storage may be an `optional<Ref<...>>` while the narrowed
    // view is the bare object (an `if (b) ...` guard). Read through the held
    // handle - the model-member paths spell it `(*(storage))->field`.
    if (m.object && m.object->kind == NodeKind::Identifier) {
        if (const auto binding = lookup(static_cast<const Identifier&>(*m.object).name)) {
            const CppInfo di = classify(binding->type);
            if (di.shape == Shape::Optional) {
                return "(*(" + expr(*m.object) + "))->" + member;
            }
            if (di.shape == Shape::Maybe) {
                return "((" + expr(*m.object) + ").value())->" + member;
            }
        }
    }
    return "(" + receiver + ")->" + member;
}

// A field write of a `$o{...}` value shape: `(<object>)->field = value`. Only
// the forms with a plain C++ spelling are lowered; the caller reports any
// other target as a type error.
std::string Generator::emitMemberAssign(const AssignmentExpr& a,
                                        const MemberExpr& m) {
    if (!m.object || !a.value) return std::string();
    Type object;
    bool optional = false;
    if (!objectReceiver(effectiveType(*m.object), object, optional)) {
        return std::string();
    }
    // A possibly-null receiver has no plain field write.
    if (optional) return std::string();
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(object, structName, fields)) return std::string();
    std::string field;
    if (!fieldCppText(fields, m.property, field)) return std::string();
    const std::string target =
        "(" + expr(*m.object) + ")->" + mapIdent(m.property);
    // The write target's type is the field's: a number-backed enum flowing
    // into an ordinary number field (spec 04 / TS parity) converts at the
    // boundary through the expected-type rule.
    Type fieldType = Type::unknown();
    for (const ObjectField& f : fields) {
        if (f.name == m.property) {
            fieldType = f.type;
            break;
        }
    }
    const std::optional<Type> savedExpected = expected_;
    if (!fieldType.isUnknown() && !fieldType.isError()) expected_ = fieldType;
    const std::string value = expr(*a.value);
    expected_ = savedExpected;
    switch (a.op) {
        case AssignOp::Assign:
            if (field.rfind("std::optional<", 0) == 0 &&
                isNullishLiteral(*a.value)) {
                return "(" + target + " = std::nullopt)";
            }
            return "(" + target + " = " + value + ")";
        case AssignOp::Add:
        case AssignOp::Subtract:
        case AssignOp::Multiply:
        case AssignOp::Divide: {
            const char* op = nullptr;
            switch (a.op) {
                case AssignOp::Add: op = "+="; break;
                case AssignOp::Subtract: op = "-="; break;
                case AssignOp::Multiply: op = "*="; break;
                default: op = "/="; break;
            }
            return "(" + target + " " + op + " " + value + ")";
        }
        case AssignOp::Modulo:
            return "(" + target + " = " + modCall(target, *a.value, value) + ")";
        case AssignOp::Power:
            return "(" + target + " = std::pow(" + target + ", " + value + "))";
        default:
            return std::string();
    }
}

// The object-literal expression form: a `$o{...}` value shape lowers to the
// generated value struct (emitObject above); a checked record literal is
// assembled field by field (recordLiteralText); a literal with an enum-typed
// field lowers to a std::tuple (emit/enum.cpp); anything else is a type error.
std::string Generator::emitObjectLiteral(const Expr& e) {
    if (std::string object = emitObject(e); !object.empty()) {
        return object;
    }
    // A literal checked against a known record type (a JSON-interface argument,
    // a spread merge) is assembled field by field by the pattern unit.
    if (std::string record =
            recordLiteralText(static_cast<const ObjectLiteral&>(e),
                              effectiveType(e));
        !record.empty()) {
        return record;
    }
    if (std::string literal =
            emitEnumObjectLiteral(static_cast<const ObjectLiteral&>(e));
        !literal.empty()) {
        return literal;
    }
    reportTypeError(e, "this object literal has no C++ representation");
    return "0.0";
}

}  // namespace cp3
