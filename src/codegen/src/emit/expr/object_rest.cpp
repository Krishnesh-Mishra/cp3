// =============================================================================
// src/codegen/src/emit/object_rest.cpp - the object-rest lowering
// (STYLE.md section 8).
//
// `const { a, ...rest } = source` binds `rest` to a fresh object that holds
// the properties the pattern does not name. The contract is in
// detail/object_rest.h; this unit spells it.
//
// The rest object is a fresh value of the source's own type: the properties
// the pattern does not name are copied over, and the ones it names keep the
// type's default value (cp3's object model is static - a property is a
// struct member, so there is no absent property to spell).
// =============================================================================
#include "detail/class/object_rest.h"

#include <string>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {
namespace detail {

namespace {

// Is `name` one of the properties the pattern names?
bool excluded(const std::vector<std::string>& names, const std::string& name) {
    for (const std::string& candidate : names) {
        if (candidate == name) return true;
    }
    return false;
}

// The literal name of a pattern property ("" for a computed key).
std::string propertyName(const ObjectPatternProperty& prop) {
    if (prop.computed || !prop.key) return std::string();
    const Expr* key = prop.key.get();
    if (key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    return std::string();
}

}  // namespace

std::string objectRestText(const ObjectPattern& pattern, const Type& sourceType,
                            const std::string& sourceText) {
    const Type base = stripNullish(sourceType);
    // A class instance and an interface wrapper have no layout cp3 can copy
    // a rest object from.
    if (classModelOf(base) != nullptr) return std::string();
    const ObjectModel* om = objectModelOf(base);
    if (om != nullptr && om->isInterface) return std::string();
    std::string structName;
    std::string access;
    std::vector<std::string> fields;
    std::vector<ObjectField> shapeFields;
    if (objectShapeOf(base, structName, shapeFields)) {
        // The all-primitive value shape: a handle to a generated struct.
        access = "->";
        for (const ObjectField& field : shapeFields) fields.push_back(field.name);
    } else if (om != nullptr && objectModelUsed(*om)) {
        // A plain object model: a handle to a generated struct.
        access = "->";
        structName = om->structName;
        for (const ObjectFieldModel& field : om->fields) {
            if (field.isMethod) continue;
            fields.push_back(field.name);
        }
    } else {
        return std::string();
    }
    // The properties the pattern names keep the type's default value.
    std::vector<std::string> named;
    for (const auto& propPtr : pattern.properties) {
        if (!propPtr) continue;
        const std::string name = propertyName(*propPtr);
        if (!name.empty()) named.push_back(name);
    }
    std::string body = "auto cp3_rest = ::cp3::runtime::makeRef<" + structName +
                       ">(" + structName + "{});";
    for (const std::string& field : fields) {
        if (excluded(named, field)) continue;
        const std::string member = mapIdent(field);
        body += " cp3_rest" + access + member + " = " + sourceText + access +
                member + ";";
    }
    body += " return cp3_rest;";
    return "[&]{ " + body + " }()";
}

}  // namespace detail
}  // namespace cp3
