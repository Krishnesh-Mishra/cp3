// =============================================================================
// src/std/json/codegen.cpp - `std/json` lowering: stringify + parse + records.
//
// `JSON.stringify(value[, indent])`: scalars lower to the runtime's dedicated
// overloads, an array value to the `Array<E>` template, and a composite
// literal (array, object, any nesting of them) through the dynamic value tree
// (`value`/`arrayValue`/`objectValue`) into `stringify(Value, indent)`.
//
// `JSON.parse<T>(text)`: `number`, `string` and `boolean` lower to the
// primitive readers and `number[]`/`string[]`/`boolean[]` to `parseArray<T>`;
// an interface T takes the record path. The unit's interface declarations are
// registered up front (collectJsonShapes) and a record shape is reified only
// when some `JSON.parse<T>` names it (usage-based reification, STDLIB docs
// §04): the C++ struct materializes into the decls section on first use, and
// the generated builder extracts every declared field at run time, throwing
// `json::ParseError` on a missing or mistyped one.
//
// A registered shape also carries the label sema computed for it
// (makeObjectType/typeKey in src/sema/src/checks/type_check.cpp), so a later
// `u.id` on the parsed value lowers to a plain C++ member read
// (emitJsonMember). A shape sema labels differently (interface methods, deep
// field types) simply stays CP3002.
//
// Everything else (untyped `parse`, `undefined`, optional/complex fields,
// cross-module shapes) stays CP3002.
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// One data field of a registered record shape: the cp3 side (name + type, for
// the sema shape label) and the C++ side (member spelling + the extraction the
// generated builder calls).
struct RecordField {
    std::string name;
    Type type;
    std::string cppType;
    std::string extract;
};

RecordField recordField(const std::string& name, Type type, std::string cppType,
                        std::string extract) {
    RecordField out;
    out.name = name;
    out.type = std::move(type);
    out.cppType = std::move(cppType);
    out.extract = std::move(extract);
    return out;
}

// sema's type key (type_check.cpp: typeKey), replicated so a receiver's Object
// type can be matched back to the registered interface without a sema-side
// handshake.
std::string typeKey(const Type& t) {
    std::string out = typeKindName(t.kind);
    out += '|';
    out += t.name;
    out += '|';
    if (t.isLiteral()) {
        switch (t.literalBase) {
            case TypeKind::Number: out += std::to_string(t.numberValue); break;
            case TypeKind::String: out += t.stringValue; break;
            case TypeKind::Boolean: out += t.boolValue ? "T" : "F"; break;
            default: break;
        }
    }
    for (const Type& part : t.parts) {
        out += '<';
        out += typeKey(part);
        out += '>';
    }
    return out;
}

// The shape label sema's makeObjectType computes for a closed field list.
std::string shapeLabel(const std::vector<RecordField>& fields) {
    std::string out = "$o{";
    for (const RecordField& field : fields) {
        out += field.name;
        out += ':';
        out += typeKey(field.type);
        out += ',';
    }
    out += '}';
    return out;
}

std::string keyName(const Expr* key) {
    if (!key) return std::string();
    if (key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    return std::string();
}

// A primitive field, or a one-level array of one: the only field shapes the
// generated builder reads. `container` is set on the array recursion.
std::optional<RecordField> recordFieldOf(const std::string& name,
                                         const TypeNode* tn, bool container) {
    if (!tn) return std::nullopt;
    if (tn->kind == NodeKind::ArrayType && !container) {
        const auto& at = static_cast<const ArrayType&>(*tn);
        return recordFieldOf(name, at.element.get(), true);
    }
    if (tn->kind != NodeKind::NamedType) return std::nullopt;
    const std::string& typeName = static_cast<const NamedType&>(*tn).name;
    if (typeName == "number") {
        return container
                   ? recordField(name, Type::array(Type::number()),
                                 "::cp3::runtime::Array<double>", "numberArrayField")
                   : recordField(name, Type::number(), "double", "numberField");
    }
    if (typeName == "string") {
        return container ? recordField(name, Type::array(Type::string()),
                                       "::cp3::runtime::Array<std::string>",
                                       "stringArrayField")
                         : recordField(name, Type::string(), "std::string",
                                       "stringField");
    }
    if (typeName == "boolean" || typeName == "bool") {
        return container
                   ? recordField(name, Type::array(Type::boolean()),
                                 "::cp3::runtime::Array<bool>", "boolArrayField")
                   : recordField(name, Type::boolean(), "bool", "boolField");
    }
    return std::nullopt;
}

// Every declared data field, or nothing when the interface is not a plain
// record (a method, a computed key, an optional or a deeper field type).
std::optional<std::vector<RecordField>> recordFields(
    const InterfaceDeclaration& iface) {
    std::vector<RecordField> fields;
    for (const auto& memberPtr : iface.members) {
        if (!memberPtr) continue;
        if (memberPtr->kind != NodeKind::PropertySignature) return std::nullopt;
        const auto& ps = static_cast<const PropertySignature&>(*memberPtr);
        const std::string name = keyName(ps.key.get());
        if (name.empty() || ps.computed || ps.optional) return std::nullopt;
        auto field = recordFieldOf(name, ps.type.get(), false);
        if (!field) return std::nullopt;
        fields.push_back(std::move(*field));
    }
    return fields;
}

std::string structText(const std::string& name,
                       const std::vector<RecordField>& fields) {
    std::string out = "struct " + mapIdent(name) + " {";
    for (const RecordField& field : fields) {
        out += "\n    " + field.cppType + " " + mapIdent(field.name) + "{};";
    }
    out += "\n};";
    return out;
}

std::string builderText(const std::string& name,
                        const std::vector<RecordField>& fields) {
    std::string out = mapIdent(name) + " cp3_out{};";
    for (const RecordField& field : fields) {
        out += " cp3_out." + mapIdent(field.name) + " = ::cp3::runtime::json::" +
               field.extract + "(cp3_f, " + cppStringLiteral(field.name) + ");";
    }
    out += " return cp3_out;";
    return out;
}

}  // namespace

// -----------------------------------------------------------------------------
// Record shape registration (usage-based reification)
// -----------------------------------------------------------------------------

void Generator::collectJsonShapes(const Program& program) {
    std::vector<const InterfaceDeclaration*> interfaces;
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        const Stmt* stmt = stmtPtr.get();
        if (stmt->kind == NodeKind::ExportDeclaration) {
            stmt = static_cast<const ExportDeclaration&>(*stmt).declaration.get();
        }
        if (stmt && stmt->kind == NodeKind::InterfaceDeclaration) {
            interfaces.push_back(static_cast<const InterfaceDeclaration*>(stmt));
        }
    }
    for (const InterfaceDeclaration* iface : interfaces) {
        if (!iface || iface->name.empty()) continue;
        auto fields = recordFields(*iface);
        if (!fields) continue;  // not a plain record: the declaration reports
        // The interface keeps its own record struct (jsonRecordParse): the
        // object value-shape path (support/value_shapes.cpp) must not claim the
        // same `$o{...}` label.
        objectShapeExclude(shapeLabel(*fields));
        jsonShapeByLabel_[shapeLabel(*fields)] = iface->name;
        jsonShapeStructs_[iface->name] = structText(iface->name, *fields);
        jsonShapeBuilders_[iface->name] = builderText(iface->name, *fields);
        std::vector<std::string> names;
        for (const RecordField& field : *fields) names.push_back(field.name);
        jsonShapeFields_[iface->name] = std::move(names);
    }
}

void Generator::emitJsonInterface(const InterfaceDeclaration& i) {
    // A registered shape is erased until a `JSON.parse<T>` names it (usage-based
    // reification): the declaration itself emits nothing.
    if (!i.name.empty() && jsonShapeStructs_.count(i.name) != 0) return;
    notImplemented(i, "top-level declaration");
}

std::string Generator::emitJsonMember(const MemberExpr& m) {
    if (!m.object || m.optional) return std::string();
    const Type receiver = effectiveType(*m.object);
    if (receiver.kind != TypeKind::Object) return std::string();
    const auto shape = jsonShapeByLabel_.find(receiver.name);
    if (shape == jsonShapeByLabel_.end()) return std::string();
    const auto fields = jsonShapeFields_.find(shape->second);
    if (fields == jsonShapeFields_.end()) return std::string();
    bool declared = false;
    for (const std::string& name : fields->second) {
        if (name == m.property) {
            declared = true;
            break;
        }
    }
    if (!declared) return std::string();
    needsJson_ = true;
    return expr(*m.object) + "." + mapIdent(m.property);
}

// -----------------------------------------------------------------------------
// Calls
// -----------------------------------------------------------------------------

std::string Generator::emitJsonCall(const CallExpr& c, const MemberExpr& m) {
    if (m.optional) return std::string();
    if (m.property == "stringify") return emitJsonStringify(c);
    if (m.property == "parse") return emitJsonParse(c);
    return std::string();
}

std::string Generator::emitJsonStringify(const CallExpr& c) {
    if (c.arguments.empty() || !c.arguments.front()) return std::string();
    const Expr* arg = c.arguments.front().get();
    if (arg->kind == NodeKind::SpreadElement) return std::string();
    // A composite literal serializes through the dynamic value tree; the
    // optional indent argument applies to that form (`stringify(Value, int)`).
    if (arg->kind == NodeKind::ArrayLiteral || arg->kind == NodeKind::ObjectLiteral) {
        const std::string built = jsonValueText(*arg);
        if (built.empty()) return std::string();
        std::string indent;
        if (c.arguments.size() >= 2) {
            const Expr* indentArg = c.arguments[1].get();
            if (!indentArg || indentArg->kind == NodeKind::SpreadElement) {
                return std::string();
            }
            indent = ", static_cast<int>(" + expr(*indentArg) + ")";
        }
        needsJson_ = true;
        return "::cp3::runtime::json::stringify(" + built + indent + ")";
    }
    const Shape shape = info(*arg).shape;
    if (shape != Shape::Number && shape != Shape::String && shape != Shape::Bool &&
        shape != Shape::Null && shape != Shape::Array) {
        return std::string();
    }
    needsJson_ = true;
    return "::cp3::runtime::json::stringify(" + expr(*arg) + ")";
}

std::string Generator::jsonValueText(const Expr& e) {
    // A composite literal recurses; the dynamic tree carries any nesting.
    if (e.kind == NodeKind::ArrayLiteral) {
        const auto& lit = static_cast<const ArrayLiteral&>(e);
        std::vector<std::string> items;
        for (const auto& elementPtr : lit.elements) {
            const Expr* element = elementPtr.get();
            if (!element || element->kind == NodeKind::SpreadElement) {
                return std::string();
            }
            std::string text = jsonValueText(*element);
            if (text.empty()) return std::string();
            items.push_back(std::move(text));
        }
        return "::cp3::runtime::json::arrayValue({" + joinStrings(items, ", ") +
               "})";
    }
    if (e.kind == NodeKind::ObjectLiteral) {
        const auto& lit = static_cast<const ObjectLiteral&>(e);
        std::vector<std::string> members;
        for (const auto& propertyPtr : lit.properties) {
            const ObjectProperty* property = propertyPtr.get();
            if (!property || property->computed || property->isMethod) {
                return std::string();
            }
            const std::string key = keyName(property->key.get());
            const Expr* value =
                property->value
                    ? property->value.get()
                    : (property->shorthand ? property->key.get() : nullptr);
            if (key.empty() || !value || value->kind == NodeKind::SpreadElement) {
                return std::string();
            }
            std::string text = jsonValueText(*value);
            if (text.empty()) return std::string();
            members.push_back("{" + cppStringLiteral(key) + ", " + text + "}");
        }
        return "::cp3::runtime::json::objectValue({" + joinStrings(members, ", ") +
               "})";
    }
    // A scalar literal or a non-literal leaf: the runtime conversion overloads
    // (double, string, boolean, null, Array<E>).
    if (e.kind == NodeKind::NumberLiteral || e.kind == NodeKind::StringLiteral ||
        e.kind == NodeKind::BoolLiteral || e.kind == NodeKind::NullLiteral) {
        return "::cp3::runtime::json::value(" + expr(e) + ")";
    }
    const Shape shape = info(e).shape;
    if (shape != Shape::Number && shape != Shape::String && shape != Shape::Bool &&
        shape != Shape::Null && shape != Shape::Array) {
        return std::string();
    }
    return "::cp3::runtime::json::value(" + expr(e) + ")";
}

std::string Generator::emitJsonParse(const CallExpr& c) {
    if (c.arguments.size() != 1 || !c.arguments.front() || c.typeArgs.empty() ||
        !c.typeArgs.front()) {
        return std::string();
    }
    const Expr& textArg = *c.arguments.front();
    if (textArg.kind == NodeKind::SpreadElement) return std::string();
    // An interface T is a reified record shape (collectJsonShapes).
    const TypeNode& tn = *c.typeArgs.front();
    if (tn.kind == NodeKind::NamedType) {
        const std::string& name = static_cast<const NamedType&>(tn).name;
        if (jsonShapeStructs_.count(name) != 0) {
            return jsonRecordParse(name, expr(textArg));
        }
    }
    const std::optional<Type> mapped = typeFromTypeNode(tn);
    if (!mapped) return std::string();
    const std::string text = expr(textArg);
    const auto call = [this, &text](const std::string& reader) {
        needsJson_ = true;
        return "::cp3::runtime::json::" + reader + "(" + text + ")";
    };
    if (mapped->kind == TypeKind::Number) return call("parseNumber");
    if (mapped->kind == TypeKind::String) return call("parseString");
    if (mapped->kind == TypeKind::Boolean) return call("parseBoolean");
    if (mapped->kind != TypeKind::Array || mapped->parts.empty()) {
        return std::string();
    }
    const TypeKind element = mapped->parts.front().kind;
    if (element == TypeKind::Number) return call("parseArray<double>");
    if (element == TypeKind::String) return call("parseArray<std::string>");
    if (element == TypeKind::Boolean) return call("parseArray<bool>");
    return std::string();
}

std::string Generator::jsonRecordParse(const std::string& shape,
                                       const std::string& text) {
    needsJson_ = true;
    if (jsonShapeUsed_.insert(shape).second) {
        jsonShapeDecls_ += jsonShapeStructs_[shape] + "\n";
    }
    return "::cp3::runtime::json::parseRecord(" + text +
           ", [](const ::cp3::runtime::json::Fields& cp3_f) { " +
           jsonShapeBuilders_[shape] + " })";
}

}  // namespace cp3
