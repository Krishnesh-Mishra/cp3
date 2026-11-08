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
// an interface T takes the record path; an inline object annotation
// (`parse<{ x: number }>`) takes the same record path over a generated
// struct for the annotation's shape: a plain object shape of primitives,
// one-level primitive arrays, or nested plain records lowers through the
// value-shape machinery (a `$o{...}` struct, exactly like an object-shape
// literal); a shape with `?:` members is spelled by the object model's field
// struct instead. Optional members (`?:`, `| undefined`, `T?`) that the JSON
// text omits are absent with no error; every other missing member throws.
// Extra keys in the JSON text are ignored, keys match case-sensitively, and
// a duplicate key anywhere throws (as the shared reader enforces). The
// unit's interface declarations are registered up front (collectJsonShapes)
// and a record shape is reified only
// when some `JSON.parse<T>` names it (usage-based reification, STDLIB docs
// §04): the C++ struct materializes into the decls section on first use, and
// the generated builder extracts every declared field at run time, throwing
// `json::ParseError` on a missing or mistyped one.
//
// A registered shape also carries the label sema computed for it
// (makeObjectType/typeKey in src/sema/src/checks/type_check.cpp), so a later
// `u.id` on the parsed value lowers to a plain C++ member read
// (emitJsonMember). A shape sema labels differently (interface methods, deep
// field types) reports an unsupported-shape error at the parse site.
//
// A non-literal object value (`let o = { id: 1 }; JSON.stringify(o)`) lowers
// through the generated `$o{...}` field list into the same dynamic value tree.
// Everything without a JSON spelling (untyped `parse`, class/interface
// receivers, optional/complex fields, cross-module shapes) reports a real
// compile error instead of a skip.
// =============================================================================
#include "detail/generator/generator.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

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

// ---------------------------------------------------------------------------
// Inline object type arguments (`JSON.parse<{ x: number }>`)
//
// sema resolves the annotation to the `$o{...}` object shape (makeObjectType),
// so the plainness of the shape decides the C++ spelling of the struct the
// parse builds:
//   * a plain shape of primitives, one-level primitive arrays or nested
//     plain records: the value-shape machinery spells it (the same struct
//     object literals use, support/value_shapes.cpp);
//   * a shape with `?:` members (no value-shape spelling): the object
//     model's plain field struct spells it (objectStructsText skips it while
//     the value-shape path owns it, so exactly one path claims it).
// Either way the builder is the same `parseRecord` builder the interface
// path emits; nested plain records recurse with the same rules.
//
// Missing vs. extra keys follow the STDLIB spec (see the header comment):
// an extra key is ignored; a declared key whose field is optional
// (`?:`, `| undefined`, `T?` over a single payload) may be absent, in which
// case the field keeps its empty state (undefined); any other missing key
// throws `json::ParseError` (catchable). A mistyped key always throws.
// ---------------------------------------------------------------------------

// One field of an inline record, resolved to its C++ extraction.
struct InlineField {
    std::string name;      // the JSON key (matched case-sensitively)
    Type inner;            // the payload type (a `T | undefined` field keeps T)
    std::string innerCpp;  // the payload's C++ spelling
    std::string memberCpp;  // the member's full spelling (wrapper included)
    bool optional = false;  // an absent key is `undefined`, not an error
    bool maybe = false;     // the member is `Maybe<...>` rather than `std::optional<...>`
    NullishKind nullKind = NullishKind::None;  // flavor of the empty state
};

// One inline record shape, resolved to its generated struct.
struct InlineRecordPlan {
    std::string structName;
    std::vector<InlineField> fields;
};

std::optional<InlineRecordPlan> planInlineRecord(const Type& t, int depth);

// The payload kinds a field may carry: a primitive, a one-level primitive
// array or a nested plain record. Resolving a nested record registers its
// struct (same as the top level), so no second representation is minted.
bool payloadInlineSupported(const Type& t, int depth) {
    switch (t.kind) {
        case TypeKind::Number:
        case TypeKind::String:
        case TypeKind::Boolean:
            return true;
        case TypeKind::Array: {
            if (t.parts.size() != 1) return false;
            const TypeKind e = t.parts.front().kind;
            return e == TypeKind::Number || e == TypeKind::String ||
                   e == TypeKind::Boolean;
        }
        case TypeKind::Object:
            return planInlineRecord(t, depth + 1).has_value();
        default:
            return false;
    }
}

// Plans one field. `memberCpp` is the member's full spelling in the struct
// (the same string classify spells for it), so the generated wrapper around
// a present payload always matches the declaration. A union over one payload
// and a nullish member (`X | undefined`, `X | null`, `T?`) is an absent-
// allowed field; a union with more than one payload is not supported.
// `valueShapeSpelling` selects the member's inline-struct restrictions: the
// generated value-shape `cp3ToText()` can render a single-flavor primitive
// option, but no `Maybe<>` / object-valued optional (no overload exists),
// so those shapes fall to the object model instead.
bool planInlineField(const std::string& name, const std::string& memberCpp,
                     const Type& t, bool modifierOptional,
                     InlineField& out, int depth,
                     bool valueShapeSpelling) {
    Type inner = t;
    bool optional = modifierOptional;
    NullishKind flavor = NullishKind::None;
    if (t.isUnion() || t.isNullable()) {
        std::vector<Type> keep;
        for (const Type& part : t.parts) {
            if (!part.isNullish()) keep.push_back(part);
        }
        if (keep.size() != 1 || keep.front().isNullish()) return false;
        inner = keep.front();
        optional = true;
        flavor = nullishKindOf(t);
        if (valueShapeSpelling) {
            const bool primitive = inner.kind == TypeKind::Number ||
                                   inner.kind == TypeKind::String ||
                                   inner.kind == TypeKind::Boolean;
            if (!primitive) return false;
            if (flavor == NullishKind::Mixed) return false;  // Maybe member
        }
    } else if (t.isNullish()) {
        return false;  // `undefined`/`null` alone carries no JSON value
    }
    // classify spells the two-flavor union as `cp3::runtime::Maybe<...>`
    // (no leading `::`), everything else empty-flavored as `std::optional<`.
    const bool maybe =
        memberCpp.rfind("cp3::runtime::Maybe<", 0) == 0 ||
        memberCpp.rfind("::cp3::runtime::Maybe<", 0) == 0;
    if (optional && !maybe && memberCpp.rfind("std::optional<", 0) != 0) {
        // The struct's member would not carry the wrapper the builder assigns.
        return false;
    }
    if (!payloadInlineSupported(inner, depth)) return false;
    const CppInfo innerCpp = classify(inner);
    if (!innerCpp.representable) return false;
    out.name = name;
    out.inner = inner;
    out.innerCpp = innerCpp.cpp;
    out.memberCpp = memberCpp;
    out.optional = optional;
    out.maybe = maybe;
    out.nullKind = flavor;
    return true;
}

// Plans one inline record shape: the value-shape struct when every member
// carries a value-shape spelling, else the object model's plain field
// struct (which owns the `?:` shapes). False when any member has no JSON
// extraction (a method, a function field, an unspellable type).
std::optional<InlineRecordPlan> planInlineRecord(const Type& t, int depth) {
    if (t.kind != TypeKind::Object || depth > 32) return std::nullopt;
    std::string structName;
    std::vector<ObjectField> valueFields;
    if (objectShapeOf(t, structName, valueFields)) {
        std::vector<InlineField> fields;
        bool ok = true;
        for (const ObjectField& field : valueFields) {
            InlineField planned;
            if (!planInlineField(field.name, field.cpp, field.type,
                                 /*modifierOptional=*/false, planned,
                                 depth, /*valueShapeSpelling=*/true)) {
                ok = false;
                break;
            }
            fields.push_back(std::move(planned));
        }
        if (ok) return InlineRecordPlan{std::move(structName), std::move(fields)};
    }
    // The object model owns shapes the value-shape path cannot spell; its
    // plain field struct (emitted by objectStructsText) is the struct.
    const ObjectModel* om = ModelRegistry::get().objectByLabel(t.name);
    if (om == nullptr || om->isInterface) return std::nullopt;
    if (!ModelRegistry::get().objectUsable(t.name)) return std::nullopt;
    std::vector<InlineField> fields;
    for (const ObjectFieldModel& field : om->fields) {
        if (field.isMethod || field.isFunction) return std::nullopt;
        const std::string memberCpp = objectFieldCpp(field);
        if (memberCpp.empty()) return std::nullopt;
        InlineField planned;
        if (!planInlineField(field.name, memberCpp, field.type,
                             /*modifierOptional=*/field.optional, planned,
                             depth, /*valueShapeSpelling=*/false)) {
            return std::nullopt;
        }
        fields.push_back(std::move(planned));
    }
    return InlineRecordPlan{om->structName, std::move(fields)};
}

std::optional<std::string> emitInlineRecordBody(const InlineRecordPlan& plan,
                                                const std::string& target,
                                                const std::string& fvar,
                                                int depth);

// The wrapper around a present payload's value: the member's optional
// flavor decides between `std::optional` and `Maybe<>`.
std::string wrapPresent(const InlineField& field, const std::string& value) {
    return field.maybe ? "cp3::runtime::Maybe<" + field.innerCpp +
                             ">(" + value + ")"
                       : "std::optional<" + field.innerCpp +
                             ">(" + value + ")";
}

// The empty state a `null` fills for a null-flavored optional member.
std::string emptyPresent(const InlineField& field) {
    return field.maybe
               ? "cp3::runtime::Maybe<" + field.innerCpp + ">(nullptr)"
               : std::string("std::nullopt");
}

// The extraction call for an Object payload: `makeRef<S>` around the nested
// builder, wrapped in the member's optional flavor when one was passed.
// A required field reads through the enclosing Fields vector via recordField
// (`var`); a present optional field holds the value itself via recordValue.
std::string emitNestedRecord(const Type& nestedType, int fdepth,
                             const std::string& fvar, const std::string& name,
                             const InlineField* member) {
    const std::optional<InlineRecordPlan> nested =
        planInlineRecord(nestedType, fdepth + 1);
    if (!nested) return std::string();
    const std::string inner = "cp3_f" + std::to_string(fdepth);
    const std::optional<std::string> body =
        emitInlineRecordBody(*nested, "cp3_in", inner, fdepth + 1);
    if (!body) return std::string();
    std::string read = "::cp3::runtime::json::";
    if (member) {
        read += "recordValue(*" + fvar + ", ";
    } else {
        read += "recordField(" + fvar + ", " + name + ", ";
    }
    read += "[](const ::cp3::runtime::json::Fields& " + inner +
            ") { " + nested->structName + " cp3_in{}; " + *body +
            "return cp3_in; })";
    std::string call =
        "::cp3::runtime::makeRef<" + nested->structName + ">(" + read + ")";
    if (member) call = wrapPresent(*member, call);
    return call;
}

// The extraction for one field. `member` is its full `target.member` path.
std::optional<std::string> emitInlineField(const InlineField& field,
                                           const std::string& member,
                                           const std::string& fvar,
                                           int depth) {
    const std::string name = cppStringLiteral(field.name);
    std::string out;
    if (!field.optional) {
        // Required: the reader throws on a missing or mistyped key.
        switch (field.inner.kind) {
            case TypeKind::Number:
                return member + " = ::cp3::runtime::json::numberField(" +
                       fvar + ", " + name + "); ";
            case TypeKind::String:
                return member + " = ::cp3::runtime::json::stringField(" +
                       fvar + ", " + name + "); ";
            case TypeKind::Boolean:
                return member + " = ::cp3::runtime::json::boolField(" + fvar +
                       ", " + name + "); ";
            case TypeKind::Array: {
                const TypeKind e = field.inner.parts.front().kind;
                const char* reader = e == TypeKind::Number
                                         ? "numberArrayField"
                                     : e == TypeKind::String
                                         ? "stringArrayField"
                                         : "boolArrayField";
                std::string call = "::cp3::runtime::json::" +
                                   std::string(reader) + "(" + fvar + ", " +
                                   name + ")";
                return member + " = " + call + "; ";
            }
            case TypeKind::Object: {
                const std::string call =
                    emitNestedRecord(field.inner, depth + 1, fvar, name,
                                     /*member=*/nullptr);
                if (call.empty()) return std::nullopt;
                return member + " = " + call + "; ";
            }
            default:
                return std::nullopt;
        }
    }
    // Optional: absent means `undefined`; present means the ordinary kind
    // check, and `null` is a mismatch unless the payload's flavor is null.
    const std::string var = "cp3_v" + std::to_string(depth);
    out += "if (const ::cp3::runtime::json::Value* " + var +
           " = ::cp3::runtime::json::findField(" + fvar + ", " + name +
           ")) { ";
    const bool jsonNull = field.nullKind == NullishKind::Null ||
                          field.nullKind == NullishKind::Mixed;
    if (jsonNull) {
        out += "if (" + var +
               "->kind == ::cp3::runtime::json::Value::Kind::Null) { ";
        out += member + " = " + emptyPresent(field) + "; } else { ";
    }
    switch (field.inner.kind) {
        case TypeKind::Number:
            out += "::cp3::runtime::json::requireKind(*" + var +
                   ", ::cp3::runtime::json::Value::Kind::Number, "
                   "\"a number\"); ";
            out += member + " = " + wrapPresent(field, var + "->number") +
                   "; ";
            break;
        case TypeKind::String:
            out += "::cp3::runtime::json::requireKind(*" + var +
                   ", ::cp3::runtime::json::Value::Kind::String, "
                   "\"a string\"); ";
            out += member + " = " + wrapPresent(field, var + "->text") +
                   "; ";
            break;
        case TypeKind::Boolean:
            out += "::cp3::runtime::json::requireKind(*" + var +
                   ", ::cp3::runtime::json::Value::Kind::Bool, "
                   "\"a boolean\"); ";
            out += member + " = " + wrapPresent(field, var + "->boolean") +
                   "; ";
            break;
        case TypeKind::Object: {
            out += "::cp3::runtime::json::requireKind(*" + var +
                   ", ::cp3::runtime::json::Value::Kind::Object, "
                   "\"an object\"); ";
            const std::string call =
                emitNestedRecord(field.inner, depth + 1, var, name, &field);
            if (call.empty()) return std::nullopt;
            out += member + " = " + call + "; ";
            break;
        }
        default:
            return std::nullopt;
    }
    if (jsonNull) out += "} ";
    out += "} ";
    return out;
}

std::optional<std::string> emitInlineRecordBody(const InlineRecordPlan& plan,
                                                const std::string& target,
                                                const std::string& fvar,
                                                int depth) {
    std::string out;
    for (const InlineField& field : plan.fields) {
        const std::string member = target + "." + mapIdent(field.name);
        const std::optional<std::string> stmt =
            emitInlineField(field, member, fvar, depth);
        if (!stmt) return std::nullopt;
        out += *stmt;
    }
    return out;
}

// The whole `JSON.parse<{...}>(text)` expression for a resolved object
// shape. nullopt when the shape is not a plain record this lowering can
// build; the caller then keeps the unsupported-argument diagnostic. The
// struct is the value-shape/object-model struct the unit already spells,
// named here, so the globals/decls emission picks it up from the text.
std::optional<std::string> jsonInlineRecordParse(const Type& t,
                                                 const std::string& text) {
    const std::optional<InlineRecordPlan> plan = planInlineRecord(t, 0);
    if (!plan) return std::nullopt;
    const std::optional<std::string> body =
        emitInlineRecordBody(*plan, "cp3_out", "cp3_f", 0);
    if (!body) return std::nullopt;
    return "::cp3::runtime::makeRef<" + plan->structName +
           ">(::cp3::runtime::json::parseRecord(" + text +
           ", [](const ::cp3::runtime::json::Fields& cp3_f) { " +
           plan->structName + " cp3_out{}; " + *body +
           "return cp3_out; }))";
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

// The dynamic JSON value expression for `text`, whose static type is `t`.
// `depth` uniquifies the temporaries of nested array loops. Returns false when
// the type has no spelling in the value tree (a class/interface handle, an
// `undefined`-flavored optional, a function, ...).
bool jsonValueForType(const Type& t, const std::string& text, std::string& out,
                      int depth) {
    const CppInfo ci = classify(t);
    switch (ci.shape) {
        case Shape::Number:
        case Shape::String:
        case Shape::Bool:
            out = "::cp3::runtime::json::value(" + text + ")";
            return true;
        case Shape::Null:
            out = "::cp3::runtime::json::nullValue()";
            return true;
        case Shape::Object: {
            std::string structName;
            std::vector<ObjectField> fields;
            if (!objectShapeOf(t, structName, fields)) return false;
            std::vector<std::string> members;
            members.reserve(fields.size());
            for (const ObjectField& field : fields) {
                std::string value;
                if (!jsonValueForType(
                        field.type, "(" + text + ")->" + mapIdent(field.name),
                        value, depth)) {
                    return false;
                }
                members.push_back("{" + cppStringLiteral(field.name) + ", " +
                                  value + "}");
            }
            out = "::cp3::runtime::json::objectValue({" +
                  joinStrings(members, ", ") + "})";
            return true;
        }
        case Shape::Array: {
            if (t.kind != TypeKind::Array || t.parts.empty()) return false;
            const Type elem = t.parts.front();
            const CppInfo ec = classify(elem);
            // A primitive (or nested primitive) array already has a direct
            // `value(Array<E>)` conversion.
            if (ec.shape == Shape::Number || ec.shape == Shape::String ||
                ec.shape == Shape::Bool || ec.shape == Shape::Array) {
                out = "::cp3::runtime::json::value(" + text + ")";
                return true;
            }
            const std::string suffix = std::to_string(depth);
            const std::string item = "cp3_e" + suffix;
            const std::string items = "cp3_items" + suffix;
            const std::string src = "cp3_src" + suffix;
            std::string converted;
            if (!jsonValueForType(elem, item, converted, depth + 1)) {
                return false;
            }
            // Bind the array handle to a name: `*(<Ref>)` in a range-for would
            // bind the loop to storage owned by a temporary Ref (dangling).
            out = "([&]{ std::vector<::cp3::runtime::json::Value> " + items +
                  "; const auto " + src + " = (" + text +
                  "); if (" + src + ") { " + items + ".reserve(" + src +
                  "->size()); for (const auto& " + item + " : *" + src +
                  ") " + items + ".push_back(" + converted + "); } return "
                  "::cp3::runtime::json::arrayValue(std::move(" + items +
                  ")); }())";
            return true;
        }
        case Shape::Union:
            // A primitive value union (`number | string | boolean`) is a
            // std::variant; each alternative has a `value` overload.
            out = "std::visit([](const auto& cp3_u) { return "
                  "::cp3::runtime::json::value(cp3_u); }, " + text + ")";
            return true;
        default:
            return false;
    }
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
    // An interface declaration is type-only and always emits nothing here: a
    // plain record shape is registered by collectJsonShapes and materialized on
    // the first `JSON.parse<T>` use (jsonRecordParse), a spec 15 interface is
    // emitted as its wrapper struct (emit/class_shape.cpp), and a generic
    // interface is reified per instantiation (support/type_map.cpp). An unused
    // interface has no C++ spelling at all, exactly like a type alias.
    (void)i;
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
    if (m.property == "stringify") {
        if (std::string out = emitJsonStringify(c); !out.empty()) return out;
        if (c.arguments.empty() || !c.arguments.front() ||
            c.arguments.front()->kind == NodeKind::SpreadElement) {
            reportCodegenError(
                c, diagcode::kArityMismatch,
                "arity mismatch: JSON.stringify expects a value argument");
        } else {
            reportUnsupported(
                c, diagcode::kTypeMismatch,
                "type mismatch: JSON.stringify cannot serialize this value; "
                "only primitives, arrays, and object values with statically "
                "known fields are supported");
        }
        return "0.0";
    }
    if (m.property == "parse") {
        if (std::string out = emitJsonParse(c); !out.empty()) return out;
        if (c.arguments.size() != 1 || !c.arguments.front() ||
            c.arguments.front()->kind == NodeKind::SpreadElement) {
            reportCodegenError(
                c, diagcode::kArityMismatch,
                "arity mismatch: JSON.parse expects one string argument");
        } else if (c.typeArgs.empty() || !c.typeArgs.front()) {
            reportUnsupported(
                c, diagcode::kTypeMismatch,
                "type mismatch: untyped JSON.parse has no runtime "
                "representation; use JSON.parse<T>");
        } else {
            reportUnsupported(
                c, diagcode::kTypeMismatch,
                "type mismatch: JSON.parse<T> does not support this type "
                "argument; only primitives, primitive arrays, and plain "
                "interface records are supported");
        }
        return "0.0";
    }
    return std::string();
}

std::string Generator::emitJsonStringify(const CallExpr& c) {
    if (c.arguments.empty() || !c.arguments.front()) return std::string();
    const Expr* arg = c.arguments.front().get();
    if (arg->kind == NodeKind::SpreadElement) return std::string();
    for (std::size_t i = 1; i < c.arguments.size(); ++i) {
        const Expr* opt = c.arguments[i].get();
        if (!opt || opt->kind == NodeKind::SpreadElement) return std::string();
    }
    if (c.arguments.size() > 3) return std::string();
    // The second-argument classification: the legacy indent is a number; the
    // ECMAScript replacer is a function, an allowlist array, or nullish.
    // A string in the second slot is read as the `space` gap (a string is
    // not a valid replacer anyway; ECMAScript would ignore it).
    enum class Second { None, Indent, Space, Nullish, Replacer, Allowlist };
    Second second = Second::None;
    if (c.arguments.size() >= 2) {
        const Expr* secondArg = c.arguments[1].get();
        const Shape shape = info(*secondArg).shape;
        const Type type = effectiveType(*secondArg);
        if (secondArg->kind == NodeKind::NullLiteral ||
            secondArg->kind == NodeKind::UndefinedLiteral) {
            second = Second::Nullish;  // nullish replacer = no replacer
        } else if (shape == Shape::Number) {
            second = Second::Indent;
        } else if (shape == Shape::String) {
            second = Second::Space;
        } else if (secondArg->kind == NodeKind::ArrayLiteral ||
                   secondArg->kind == NodeKind::ArrowFunction ||
                   type.kind == TypeKind::Function ||
                   (type.kind == TypeKind::Array && !type.parts.empty())) {
            second = (secondArg->kind == NodeKind::ArrayLiteral ||
                      (type.kind == TypeKind::Array && !type.parts.empty() &&
                       type.parts.front().kind == TypeKind::String))
                         ? Second::Allowlist
                         : Second::Replacer;
        } else {
            return std::string();
        }
        if (c.arguments.size() >= 3 && second == Second::Indent) {
            return std::string();  // indent and space are mutually exclusive
        }
        if (c.arguments.size() >= 3 && second == Second::Allowlist &&
            c.arguments[1].get()->kind == NodeKind::ArrayLiteral) {
            // An allowlist never takes a callback; `space` is arg 3 by the
            // same rule as the callback form, so only the nullish/space
            // shapes reach the allowlist path below.
            if (const Expr* third = c.arguments[2].get()) {
                const Shape thirdShape = info(*third).shape;
                if (thirdShape != Shape::Number && thirdShape != Shape::String) {
                    return std::string();
                }
            }
        }
    }
    // No replacer and no space: the legacy fast paths (scalars, primitive
    // arrays, literal composites, indent). A nullish replacer with a third
    // argument still carries a `space`, so it takes the gap path below.
    if (second == Second::None ||
        (second == Second::Indent) ||
        (second == Second::Nullish && c.arguments.size() < 3)) {
        // The optional indent argument applies to every form that lowers
        // through the dynamic value tree (`stringify(Value, int)`).
        std::string indent;
        if (second == Second::Indent) {
            indent = ", static_cast<int>(" + expr(*c.arguments[1]) + ")";
        }
        // A composite literal serializes through the dynamic value tree.
        if (arg->kind == NodeKind::ArrayLiteral ||
            arg->kind == NodeKind::ObjectLiteral) {
            const std::string built = jsonValueText(*arg);
            if (built.empty()) return std::string();
            needsJson_ = true;
            return "::cp3::runtime::json::stringify(" + built + indent + ")";
        }
        const Type argType = effectiveType(*arg);
        const Shape shape = info(*arg).shape;
        if (shape == Shape::Number || shape == Shape::String ||
            shape == Shape::Bool || shape == Shape::Null) {
            needsJson_ = true;
            return "::cp3::runtime::json::stringify(" + expr(*arg) + ")";
        }
        // A primitive (or nested primitive) array keeps the direct Array<E>
        // overload; every other array goes through the value tree.
        if (shape == Shape::Array && argType.kind == TypeKind::Array &&
            !argType.parts.empty()) {
            const CppInfo elem = classify(argType.parts.front());
            if (elem.shape == Shape::Number || elem.shape == Shape::String ||
                elem.shape == Shape::Bool || elem.shape == Shape::Array) {
                needsJson_ = true;
                return "::cp3::runtime::json::stringify(" + expr(*arg) + ")";
            }
        }
        std::string value;
        if (jsonValueForType(argType, "cp3_json_v", value, 0)) {
            needsJson_ = true;
            return "([&]{ const auto& cp3_json_v = (" + expr(*arg) +
                   "); return ::cp3::runtime::json::stringify(" + value +
                   indent + "); }())";
        }
        return std::string();
    }
    // The replacer/space path: always through the dynamic value tree.
    std::string gap = "std::string()";
    if (second == Second::Space) {
        gap = "::cp3::runtime::json::normalizeSpace(" + expr(*c.arguments[1]) + ")";
    } else if (c.arguments.size() >= 3) {
        const Expr* third = c.arguments[2].get();
        const Shape shape = info(*third).shape;
        if (shape != Shape::Number && shape != Shape::String) {
            return std::string();
        }
        gap = "::cp3::runtime::json::normalizeSpace(" + expr(*third) + ")";
    }
    // The `Value` the replacer walks.
    std::string valueExpr;
    if (arg->kind == NodeKind::ArrayLiteral ||
        arg->kind == NodeKind::ObjectLiteral) {
        const std::string built = jsonValueText(*arg);
        if (built.empty()) return std::string();
        valueExpr = built;
    } else {
        std::string converted;
        if (!jsonValueForType(effectiveType(*arg), "cp3_json_v", converted, 0)) {
            return std::string();
        }
        valueExpr = "([&]{ const auto& cp3_json_v = (" + expr(*arg) +
                    "); return " + converted + "; }())";
    }
    // The replacer text (`Replacer` or allowlist vector).
    std::string replacerExpr;
    if (second == Second::Allowlist) {
        if (c.arguments[1].get()->kind == NodeKind::ArrayLiteral) {
            const auto& lit =
                static_cast<const ArrayLiteral&>(*c.arguments[1]);
            std::vector<std::string> items;
            for (const auto& elementPtr : lit.elements) {
                const Expr* element = elementPtr.get();
                if (!element ||
                    element->kind == NodeKind::SpreadElement) {
                    return std::string();
                }
                if (element->kind == NodeKind::StringLiteral) {
                    items.push_back(cppStringLiteral(
                        static_cast<const StringLiteral&>(*element).value));
                } else {
                    return std::string();
                }
            }
            replacerExpr = "std::vector<std::string>{" +
                           joinStrings(items, ", ") + "}";
        } else {
            replacerExpr = "*(" + expr(*c.arguments[1]) + ")";
        }
    } else if (second == Second::Replacer) {
        const Expr* replacerArg = c.arguments[1].get();
        std::string callback;
        if (replacerArg->kind == NodeKind::ArrowFunction) {
            const auto& arrow = static_cast<const ArrowFunction&>(*replacerArg);
            // The C++ spelling of the two replacer parameters: the key is a
            // string, the value is the dynamic JSON tree. An annotation of
            // `unknown` (the placeholder) falls back to the context type.
            callback = emitArrow(arrow, {"std::string",
                                         "::cp3::runtime::json::Value"});
            std::size_t takes = arrow.params.size();
            std::string call = "cp3_fn";
            if (takes >= 2) {
                call += "(cp3_key, cp3_old)";
            } else if (takes == 1) {
                call += "(cp3_key)";
            } else {
                call += "()";
            }
            replacerExpr =
                "([&]{ auto cp3_fn = " + callback +
                "; return ::cp3::runtime::json::Replacer("
                "[cp3_fn](const std::string& cp3_key, "
                "const ::cp3::runtime::json::Value& cp3_old) -> "
                "std::optional<::cp3::runtime::json::Value> { return "
                "::cp3::runtime::json::toOptionalValue("
                "::cp3::runtime::json::value(" +
                call + ")); }); }())";
        } else {
            // A named function / function-typed value: it must accept the two
            // replacer arguments (key, value).
            replacerExpr =
                "([&]{ auto cp3_fn = (" + expr(*replacerArg) +
                "); return ::cp3::runtime::json::Replacer("
                "[cp3_fn](const std::string& cp3_key, "
                "const ::cp3::runtime::json::Value& cp3_old) -> "
                "std::optional<::cp3::runtime::json::Value> { return "
                "::cp3::runtime::json::toOptionalValue("
                "::cp3::runtime::json::value(cp3_fn(cp3_key, cp3_old))); }); "
                "}())";
        }
    }
    needsJson_ = true;
    if (second == Second::Space || second == Second::Nullish) {
        return "::cp3::runtime::json::stringify(" + valueExpr + ", " + gap +
               ")";
    }
    return "::cp3::runtime::json::stringify(" + valueExpr + ", " + replacerExpr +
           ", " + gap + ")";
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
    const std::string text = expr(textArg);
    // An inline object annotation (`JSON.parse<{ x: number }>`): sema resolved
    // it to the `$o{...}` object shape. A plain record of primitives (and
    // nested plain records) lowers through the generated record builder;
    // anything else keeps the unsupported-argument diagnostic below (the
    // interface path above is untouched by this branch).
    std::optional<Type> objType;
    if (const Type* published = sema_.typeOf(tn.nodeId);
        published && published->kind == TypeKind::Object &&
        !published->isError()) {
        objType = *published;
    } else if (tn.kind == NodeKind::ObjectType) {
        if (std::optional<Type> mapped = typeFromTypeNode(tn);
            mapped && mapped->kind == TypeKind::Object) {
            objType = *mapped;
        }
    }
    if (objType) {
        if (std::optional<std::string> out =
                jsonInlineRecordParse(*objType, text)) {
            needsJson_ = true;
            return *out;
        }
        return std::string();  // an unsupported object shape: CP3401
    }
    const std::optional<Type> mapped = typeFromTypeNode(tn);
    if (!mapped) return std::string();
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
    // A registered record is a plain object (spec 10): the parse builds the
    // field struct and the reference model wraps it, so the value converts to
    // the interface wrapper (`cp3_iface_<T>`) and reads through the handle.
    return "::cp3::runtime::makeRef<" + mapIdent(shape) +
           ">(::cp3::runtime::json::parseRecord(" + text +
           ", [](const ::cp3::runtime::json::Fields& cp3_f) { " +
           jsonShapeBuilders_[shape] + " }))";
}

}  // namespace cp3
