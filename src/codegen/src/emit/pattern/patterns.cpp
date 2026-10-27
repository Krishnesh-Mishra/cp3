// =============================================================================
// src/codegen/src/emit/patterns.cpp - binding-pattern lowering (STYLE.md 8).
//
// Array/object destructuring declarations, destructuring parameters and the
// record literals they consume. The unit reuses the two existing record
// machineries and adds no object representation of its own:
//
//   * an all-primitive `$o{...}` value shape is the generated value struct of
//     support/value_shapes.cpp (`objectShapeOf`);
//   * a registered interface is the std/json record struct
//     (collectJsonShapes), materialized on first use exactly like a
//     `JSON.parse<T>` use.
//
// An object pattern whose source has no layout cp3 can read (a primitive, an
// unknown value, ...) is a type error at the pattern: there is no value to
// unpack.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/class/object_rest.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace detail {

// The identifier a parameter binds directly, unwrapping a trailing `...rest`
// (parser/src/declarations.cpp); nullptr for a destructuring pattern.
const Identifier* parameterIdentifier(const Parameter& p) {
    const Node* target = p.name.get();
    if (target && target->kind == NodeKind::RestElement) {
        target = static_cast<const RestElement&>(*target).argument.get();
    }
    if (target && target->kind == NodeKind::Identifier) {
        return static_cast<const Identifier*>(target);
    }
    return nullptr;
}

}  // namespace detail

namespace {

// The literal field name of a binding-pattern key. A string or number literal
// is a name even when the source spelled it as a computed key (`{ ["x"]: v }`);
// an identifier is a name only in the shorthand form.
std::string patternKeyName(const Expr* key, bool computed) {
    if (key == nullptr) return std::string();
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    if (key->kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(*key).raw;
    }
    if (!computed && key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    return std::string();
}

// Sema's "not statically known" marker: the binding's own type is the one
// the emitter inferred, not the ambient placeholder.
bool isAmbientType(const Type& t) {
    return t.kind == TypeKind::Object && t.name == "$ambient";
}

// Sema types a tuple rest binding as `unknown[]` (the tail slot types live on
// the source tuple, not on the binding). The site type is the precise array the
// walkers build from the tail.
bool isPlaceholderArray(const Type& t) {
    return t.kind == TypeKind::Array &&
           (t.parts.empty() || t.parts.front().isUnknown());
}

// A value that may be null/undefined cannot be destructured: there is no
// object/array to read.
bool hasNullishMember(const Type& t) {
    if (t.isNullable() || t.isNullish()) return true;
    if (!t.isUnion()) return false;
    for (const Type& part : t.parts) {
        if (part.isNullable() || part.isNullish()) return true;
    }
    return false;
}

// The literal field name of an object-literal property ("" when computed or a
// method).
std::string propertyKeyName(const ObjectProperty& p) {
    if (p.computed || p.isMethod) return std::string();
    return patternKeyName(p.key.get(), false);
}

const Expr* propertyValue(const ObjectProperty& p) {
    if (p.value) return p.value.get();
    if (p.shorthand && p.key) return p.key.get();
    return nullptr;
}

bool hasField(const std::vector<std::string>& fields, const std::string& name) {
    for (const std::string& field : fields) {
        if (field == name) return true;
    }
    return false;
}

// One field of a spread source: its name, how the source is read (`->` for a
// reference, `.` for a by-value record), whether it can be absent, and whether
// it is read through an accessor (an interface member).
struct SpreadField {
    std::string name;
    std::string access;
    bool optional;
    bool accessor;
};

// The field named `name` in a plain object shape's field list.
bool hasShapeField(const std::vector<ObjectField>& fields,
                    const std::string& name) {
    for (const ObjectField& field : fields) {
        if (field.name == name) return true;
    }
    return false;
}

// Is the field named `name` an optional one (an `std::optional<T>` member, so
// the field may be absent)?
bool optionalShapeField(const std::vector<ObjectField>& fields,
                         const std::string& name) {
    for (const ObjectField& field : fields) {
        if (field.name == name) {
            return field.cpp.rfind("std::optional<", 0) == 0;
        }
    }
    return false;
}

}  // namespace

// The existing record spelling of a type, or false. A JSON interface record is
// materialized here the first time a pattern or literal names it.
bool Generator::recordLayout(const Type& t, std::string& structName,
                             std::vector<std::string>& fieldNames) {
    std::vector<ObjectField> shapeFields;
    if (objectShapeOf(t, structName, shapeFields)) {
        fieldNames.clear();
        for (const ObjectField& field : shapeFields) fieldNames.push_back(field.name);
        return true;
    }
    std::string interfaceName;
    if (t.kind == TypeKind::Class) {
        if (jsonShapeStructs_.count(t.name) != 0) interfaceName = t.name;
    } else if (t.kind == TypeKind::Object) {
        const auto label = jsonShapeByLabel_.find(t.name);
        if (label != jsonShapeByLabel_.end()) interfaceName = label->second;
    }
    if (interfaceName.empty()) return false;
    const auto fields = jsonShapeFields_.find(interfaceName);
    if (fields == jsonShapeFields_.end()) return false;
    if (jsonShapeUsed_.insert(interfaceName).second) {
        jsonShapeDecls_ += jsonShapeStructs_[interfaceName] + "\n";
    }
    structName = mapIdent(interfaceName);
    fieldNames = fields->second;
    return true;
}

// The excess-property diagnostic: a property of an object literal that the
// target record does not have. Sema reports the same rule (CP3409) for a
// fresh literal checked against a known target type; the codegen path is the
// backstop for the forms sema does not check.
void Generator::reportExcessProperty(const Node& site, const std::string& name) {
    if (name.empty()) {
        sink_.error(diagcode::kTypeMismatch, site.span,
                    "type mismatch: a computed property is not part of the "
                    "target type");
        return;
    }
    sink_.error(diagcode::kExcessProperty, site.span,
                "excess property '" + name +
                    "' is not part of the target type");
}

// The statements that copy a spread source's fields into the target record.
// The source's layout is a plain value shape, a structural object model (a
// field that can be absent among them) or a registered record; every one of
// them names the fields the spread copies and how the source is read. False
// when the source has no layout cp3 can read.
bool Generator::spreadCopyText(const Expr& argument,
                                const std::vector<std::string>& targetFields,
                                const std::string& outName,
                                const std::string& outAccess, std::string& out) {
    const Type sourceType = effectiveType(argument);
    std::vector<SpreadField> sourceFields;
    std::string sourceStruct;
    std::vector<std::string> recordFields;
    std::vector<ObjectField> sourceShapeFields;
    const bool sourcePlain =
        objectShapeOf(sourceType, sourceStruct, sourceShapeFields);
    if (sourcePlain) {
        for (const ObjectField& field : sourceShapeFields) {
            sourceFields.push_back(SpreadField{field.name, "->", false, false});
        }
    } else {
        const ObjectModel* om = objectModelOf(sourceType);
        if (om != nullptr && objectModelUsed(*om)) {
            for (const ObjectFieldModel& field : om->fields) {
                if (field.isMethod) continue;
                sourceFields.push_back(SpreadField{
                    field.name, om->isInterface ? "." : "->", field.optional,
                    om->isInterface});
            }
        } else if (recordLayout(sourceType, sourceStruct, recordFields)) {
            for (const std::string& field : recordFields) {
                sourceFields.push_back(SpreadField{field, ".", false, false});
            }
        } else {
            // A value cp3 cannot spread into a record (a collection, a
            // promise, ...).
            sink_.error(diagcode::kTypeMismatch, argument.span,
                        "type mismatch: cannot spread '" + describe(sourceType) +
                            "' into an object literal");
            return false;
        }
    }
    const std::string source = freshLabel("spread_");
    out += " { const auto& " + source + " = " + expr(argument) + ";";
    for (const SpreadField& field : sourceFields) {
        if (!hasField(targetFields, field.name)) continue;
        const std::string read = source + field.access + mapIdent(field.name) +
                                 (field.accessor ? "()" : "");
        if (field.optional) {
            // A field that can be absent is copied only when it holds a value
            // (JS spread semantics).
            out += " if (" + read + ".has_value()) " + outName + outAccess +
                   mapIdent(field.name) + " = *" + read + ";";
            continue;
        }
        out += " " + outName + outAccess + mapIdent(field.name) + " = " + read +
               ";";
    }
    out += " }";
    return true;
}

// An object literal filling a structural object model: the model owns a
// struct the plain value shape cannot spell (an optional field, a class
// field, ...), so the literal fills that struct field by field. A field the
// literal does not provide keeps the model's default (an absent optional
// field stays empty).
std::string Generator::recordModelText(const ObjectLiteral& lit,
                                       const ObjectModel& om) {
    const std::string outName = freshLabel("record_");
    std::string body = "auto " + outName + " = ::cp3::runtime::makeRef<" +
                       om.structName + ">(" + om.structName + "{});";
    std::vector<std::string> fields;
    for (const ObjectFieldModel& field : om.fields) {
        if (field.isMethod) continue;
        fields.push_back(field.name);
    }
    for (const auto& propertyPtr : lit.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (p == nullptr) continue;
        if (p->value && p->value->kind == NodeKind::SpreadElement) {
            const auto& sp = static_cast<const SpreadElement&>(*p->value);
            if (!sp.argument) continue;
            if (!spreadCopyText(*sp.argument, fields, outName, "->", body)) {
                return std::string();
            }
            continue;
        }
        const std::string name = propertyKeyName(*p);
        if (name.empty() || !hasField(fields, name)) {
            // A property the model does not have: the excess-property rule
            // sema reports for a fresh literal against a known target type.
            // The property is dropped; the diagnostic is the error.
            reportExcessProperty(*p, name);
            continue;
        }
        if (const Expr* value = propertyValue(*p)) {
            body += " " + outName + "->" + mapIdent(name) + " = " +
                    expr(*value) + ";";
        }
    }
    body += " return " + outName + ";";
    return "[&]{ " + body + " }()";
}

// An object literal for a known record type. Property order and repeated
// spreads follow JS semantics because the value is assembled by assignment in
// source order, and a spread source is evaluated once into a local.
std::string Generator::recordLiteralText(const ObjectLiteral& lit,
                                         const Type& expected) {
    // The context's expected type may name a structural object model: the
    // model owns the struct the literal fills (the plain value shape cannot
    // spell a field the model has).
    if (expected_) {
        const ObjectModel* om = objectModelOf(*expected_);
        if (om != nullptr && !om->isInterface && objectModelUsed(*om)) {
            return recordModelText(lit, *om);
        }
    }
    std::string structName;
    std::vector<std::string> fields;
    std::vector<ObjectField> shapeFields;
    // A plain object shape is a reference type (spec 10): assembled through a
    // `Ref<Struct>` handle. A JSON-interface record stays a by-value struct.
    const bool plainShape = objectShapeOf(expected, structName, shapeFields);
    if (plainShape) {
        for (const ObjectField& field : shapeFields) fields.push_back(field.name);
    } else if (!recordLayout(expected, structName, fields)) {
        return std::string();
    }
    const std::string outName = freshLabel("record_");
    const std::string outAccess = plainShape ? "->" : ".";
    std::string body =
        plainShape
            ? "auto " + outName + " = ::cp3::runtime::makeRef<" + structName +
                  ">(" + structName + "{});"
            : structName + " " + outName + "{};";
    for (const auto& propertyPtr : lit.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (p == nullptr) continue;
        if (p->value && p->value->kind == NodeKind::SpreadElement) {
            const auto& sp = static_cast<const SpreadElement&>(*p->value);
            if (!sp.argument) return std::string();
            if (!spreadCopyText(*sp.argument, fields, outName, outAccess, body)) {
                return std::string();
            }
            continue;
        }
        const std::string name = propertyKeyName(*p);
        if (name.empty() || !hasField(fields, name)) {
            // A property the record does not have: the excess-property rule
            // sema reports for a fresh literal against a known target type.
            reportExcessProperty(*p, name);
            return std::string();
        }
        if (const Expr* value = propertyValue(*p)) {
            body += " " + outName + outAccess + mapIdent(name) + " = " +
                    expr(*value) + ";";
        }
    }
    body += " return " + outName + ";";
    return "[&]{ " + body + " }()";
}

// `let [a, b] = ...` / `let { a } = ...`: evaluate the initializer once into a
// local, then unpack. The generated locals carry the kind's const-ness.
bool Generator::emitDestructureDecl(const VariableDeclarator& d, VarKind kind,
                                    std::string& out, int indent) {
    if (!d.id) return false;
    const bool isArray = d.id->kind == NodeKind::ArrayPattern;
    const bool isObject = d.id->kind == NodeKind::ObjectPattern;
    if (!isArray && !isObject) return false;
    if (intOn()) intFail();
    if (!d.init) {
        // A destructuring declaration without an initializer has no value to
        // destructure (sema reports it as CP3507; the codegen repeats it so a
        // codegen-only pass stays honest).
        reportUnsupported(d, diagcode::kMissingDestructuringInit,
                          "a destructuring declaration needs an initializer");
        return true;
    }
    const std::string temp = freshLabel("src_");
    line(out, indent, "auto " + temp + " = " + expr(*d.init) + ";");
    const PatternSource src{temp, effectiveType(*d.init), kind == VarKind::Const};
    unpackPattern(*d.id, src, out, indent);
    return true;
}

void Generator::emitPatternParams(const FunctionDeclaration& fd, std::string& out,
                                  int indent) {
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        const Parameter* p = fd.params[i].get();
        if (!p || !p->name) continue;
        const Node* pattern = nullptr;
        if (p->name->kind == NodeKind::ArrayPattern ||
            p->name->kind == NodeKind::ObjectPattern) {
            pattern = p->name.get();
        } else if (p->isRest && p->name->kind == NodeKind::RestElement) {
            // A rest parameter with a destructuring target (`f(...[a, b])`):
            // the C++ parameter is the array the rest collects, and the
            // prologue unpacks it into the pattern.
            const Node* target =
                static_cast<const RestElement&>(*p->name).argument.get();
            // An object pattern cannot be the target of a rest parameter
            // (the rest value is an array); parameterText reports it, so the
            // prologue has nothing to unpack.
            if (target && target->kind == NodeKind::ObjectPattern) continue;
            if (target && target->kind == NodeKind::ArrayPattern) {
                pattern = target;
            }
        }
        if (p->defaultValue) {
            // A defaulted destructuring parameter: the C++ parameter carries
            // the default (a C++ default argument), so the bindings read the
            // parameter itself (emit/destructure.cpp).
            emitDestructuringParam(*p, fd, i, out);
            continue;
        }
        if (pattern == nullptr) continue;
        const std::optional<Type> pt = parameterType(*p, fd, i);
        const std::string source = "cp3_param_" + std::to_string(i);
        const PatternSource src{source, pt.value_or(Type::unknown()), false};
        unpackPattern(*pattern, src, out, indent);
    }
}

std::optional<Type> Generator::parameterType(const Parameter& p,
                                             const FunctionDeclaration& fd,
                                             std::size_t index) const {
    // A rest parameter's type is the array of its element type. An untyped
    // rest parameter has no element type at all (sema leaves it `unknown`),
    // so the element type is a template parameter of the function's own
    // template (templatePrefix), which the call site deduces from the
    // arguments it packs.
    const auto restType = [&](const Type& elem) -> Type {
        if (elem.isUnknown()) {
            return Type::array(Type::named(TypeKind::TypeParam,
                                           untypedParamTemplate(index)));
        }
        return Type::array(elem);
    };
    // A generic function's annotation names its own type parameters (`x: T`),
    // which the raw annotation cannot map (sema resolved them): prefer the
    // resolved signature (spec 16).
    if (!fd.typeParams.empty()) {
        auto it = sema_.functions.find(fd.nodeId);
        if (it != sema_.functions.end() &&
            index < it->second.paramTypes.size()) {
            const Type t = it->second.paramTypes[index];
            // Sema records a rest parameter's ELEMENT type; the parameter
            // itself is the array of it (`...items: T[]` -> `T[]`).
            if (p.isRest) return restType(t);
            if (p.optional) return Type::nullable(t);
            return t;
        }
    }
    if (p.type) {
        if (auto t = typeOfNode(*p.type)) {
            if (p.optional) return Type::nullable(*t);
            return t;
        }
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end() && index < it->second.paramTypes.size()) {
        const Type t = it->second.paramTypes[index];
        if (p.isRest) return restType(t);
        return t;
    }
    return std::nullopt;
}

// The C++ spelling of a parameter's own type: its annotation, the resolved
// signature, or "" when it has none. An untyped parameter has no cp3 type at
// all (sema leaves it `unknown`), so the C++ side spells it as a template
// parameter of the function's own template instead.
std::string Generator::parameterCppType(const Parameter& p,
                                        const FunctionDeclaration& fd,
                                        std::size_t index) {
    const std::optional<Type> pt = parameterType(p, fd, index);
    if (!pt) return std::string();
    const CppInfo ci = classify(*pt);
    if (ci.representable) return ci.cpp;
    // A destructured parameter annotated with a registered record (an
    // interface reified by the JSON record surface) uses that struct's
    // spelling; the prologue reads its fields.
    if (p.name && (p.name->kind == NodeKind::ArrayPattern ||
                   p.name->kind == NodeKind::ObjectPattern)) {
        std::string structName;
        std::vector<std::string> fields;
        if (recordLayout(*pt, structName, fields)) return structName;
    }
    return std::string();
}

// The template parameter that carries an untyped parameter's type
// (`function f(x)` -> `template <typename cp3_T0> ... f(cp3_T0 x)`). The
// call site deduces it, exactly like an untyped arrow parameter.
std::string Generator::untypedParamTemplate(std::size_t index) const {
    return "cp3_T" + std::to_string(index);
}

std::string Generator::parameterText(const Parameter& p, const FunctionDeclaration& fd,
                                     std::size_t index, bool includeDefault) {
    const Identifier* id = parameterIdentifier(p);
    // A rest parameter with a destructuring target (`f(...[a, b])`): the C++
    // parameter is the array the rest collects, and the body prologue unpacks
    // it into the pattern.
    const Node* restPattern = nullptr;
    if (p.isRest && p.name && p.name->kind == NodeKind::RestElement) {
        const Node* target =
            static_cast<const RestElement&>(*p.name).argument.get();
        if (target && (target->kind == NodeKind::ArrayPattern ||
                       target->kind == NodeKind::ObjectPattern)) {
            restPattern = target;
        }
    }
    std::string name;
    if (id) {
        name = mapIdent(id->name);
    } else {
        // A destructured parameter (a binding pattern, or a rest parameter
        // with one): the C++ parameter is the generated handle whose contents
        // the body prologue unpacks, spelled per index (emitPatternParams
        // reads the same name).
        name = "cp3_param_" + std::to_string(index);
    }

    const std::optional<Type> pt = parameterType(p, fd, index);
    if (p.isRest) {
        // `...rest: T[]`: the C++ parameter is one array (calls pack their
        // trailing arguments, emit/call.cpp). The default covers a call that
        // passes no rest arguments at all.
        if (!id && restPattern == nullptr) {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: a rest parameter must bind a "
                              "name or an array pattern");
            return "/* rest */";
        }
        if (restPattern != nullptr &&
            restPattern->kind == NodeKind::ObjectPattern) {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: a rest parameter collects an "
                              "array; an object pattern cannot be its target");
            return "/* rest */";
        }
        if (!pt || pt->kind != TypeKind::Array || pt->parts.empty()) {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: a rest parameter must have an "
                              "array type");
            return "/* rest */";
        }
        const Type elemType = pt->parts.front();
        CppInfo ec;
        if (!arrayElementInfo(elemType, ec)) {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: rest parameter element type '" +
                                  describe(elemType) +
                                  "' has no C++ representation");
            return "/* rest */";
        }
        needsTypedArray_ = true;
        std::string out = "::cp3::runtime::Array<" + ec.cpp + "> " + name;
        if (includeDefault) {
            out += " = ::cp3::runtime::makeArray<" + ec.cpp +
                   ">(std::vector<" + ec.cpp + ">{})";
        }
        return out;
    }

    std::string type = parameterCppType(p, fd, index);
    if (type.empty()) {
        // An untyped parameter: its C++ spelling is a template parameter of
        // the function's own template (templatePrefix), which the call site
        // deduces - the same fallback an untyped arrow parameter gets.
        type = untypedParamTemplate(index);
    }

    std::string out = type + (p.isRef ? "& " : " ") + name;
    if (includeDefault) {
        if (p.defaultValue) {
            out += " = " + expr(*p.defaultValue);
        } else if (p.optional && type.rfind("std::optional<", 0) == 0) {
            // `name?: T` lowers to `std::optional<T> name = std::nullopt` so
            // the argument may be omitted at the call site.
            out += " = std::nullopt";
        }
    }
    return out;
}

void Generator::unpackPattern(const Node& pattern, const PatternSource& src,
                              std::string& out, int indent) {
    if (pattern.kind == NodeKind::ArrayPattern) {
        unpackArrayPattern(static_cast<const ArrayPattern&>(pattern), src, out,
                           indent);
        return;
    }
    if (pattern.kind == NodeKind::ObjectPattern) {
        unpackObjectPattern(static_cast<const ObjectPattern&>(pattern), src, out,
                            indent);
        return;
    }
    // The callers only route array and object patterns here; any other AST
    // shape is a broken invariant, not a source-language form.
    reportUnsupported(pattern, diagcode::kInternalError,
                      "internal error: the binding-pattern lowering received a "
                      "non-pattern node");
}

// The runtime value of a rest object binding: a fresh object shape that holds
// the source's fields except the ones the pattern named (the TypeScript Omit).
//
// The Omit type is the one sema computed for the rest binding (the type model
// grows the operation, so the binding's declared type and the value built
// here are the same shape). The value is a new object holding the source's
// remaining fields, in the order the source declared them.
//
// Empty when the source has no enumerable shape (an open shape, a class, a
// primitive): the caller reports the reason.
std::string Generator::restObjectText(const ObjectPattern& p,
                                      const std::string& source,
                                      const Type& sourceType, Type& restType) {
    restType = Type::unknown();
    if (!p.rest) return std::string();
    const auto& re = static_cast<const RestElement&>(*p.rest);
    if (!re.argument || re.argument->kind != NodeKind::Identifier) {
        return std::string();
    }
    const auto& id = static_cast<const Identifier&>(*re.argument);
    const Type* published = sema_.typeOf(id.nodeId);
    if (published == nullptr || published->isUnknown() ||
        published->isError()) {
        return std::string();
    }
    restType = *published;
    std::string structName;
    std::vector<ObjectField> restFields;
    if (!objectShapeOf(restType, structName, restFields)) {
        return std::string();
    }
    // The rest reads the source's own fields, so the source must have a
    // layout cp3 can read. The source families are the same ones object
    // patterns consume: a plain value shape (`->`), a structural object model
    // (an accessor for an interface, `->` for a plain object), or a JSON
    // record (`.`). A class has no static field shape (its members are
    // nominal), so it stays unsupported.
    const Type base = stripNullish(sourceType);
    std::vector<SpreadField> sourceFields;
    std::string sourceStruct;
    std::vector<ObjectField> sourceShapeFields;
    if (objectShapeOf(base, sourceStruct, sourceShapeFields)) {
        for (const ObjectField& field : sourceShapeFields) {
            sourceFields.push_back(SpreadField{field.name, "->", false, false});
        }
    } else if (const ObjectModel* om = objectModelOf(base);
               om != nullptr && objectModelUsed(*om)) {
        for (const ObjectFieldModel& field : om->fields) {
            if (field.isMethod) continue;
            sourceFields.push_back(SpreadField{
                field.name, om->isInterface ? "." : "->", field.optional,
                om->isInterface});
        }
    } else {
        std::vector<std::string> recordFields;
        if (!recordLayout(base, sourceStruct, recordFields)) {
            return std::string();
        }
        for (const std::string& field : recordFields) {
            sourceFields.push_back(SpreadField{field, ".", false, false});
        }
    }
    std::vector<std::string> values;
    for (const ObjectField& field : restFields) {
        const SpreadField* found = nullptr;
        for (const SpreadField& candidate : sourceFields) {
            if (candidate.name == field.name) {
                found = &candidate;
                break;
            }
        }
        if (found == nullptr || found->optional) return std::string();
        std::string read = source + found->access + mapIdent(field.name);
        if (found->accessor) read += "()";
        values.push_back("(" + read + ")");
    }
    return "::cp3::runtime::makeRef<" + structName + ">(" + structName + "{" +
           joinStrings(values, ", ") + "})";
}

void Generator::unpackObjectPattern(const ObjectPattern& p,
                                    const PatternSource& src, std::string& out,
                                    int indent) {
    std::string structName;
    std::vector<std::string> fields;
    // The class / interface / plain-object models own the value spelling when
    // they materialized (spec 15): an interface is a by-value wrapper with
    // accessor methods, a class or plain object is a `Ref<T>` handle. The
    // std/json record keeps `.field`.
    if (hasNullishMember(src.type)) {
        reportUnsupported(p, diagcode::kTypeMismatch,
                          "type mismatch: cannot destructure a possibly null "
                          "or undefined value");
        return;
    }
    const Type base = stripNullish(src.type);
    const ObjectModel* om = objectModelOf(base);
    const ClassModel* cm = classModelOf(base);
    if (om != nullptr && !objectModelUsed(*om)) om = nullptr;
    std::vector<ObjectField> shapeFields;
    const bool plainShape = objectShapeOf(base, structName, shapeFields);
    const bool record =
        om == nullptr && cm == nullptr && !plainShape &&
        recordLayout(src.type, structName, fields);
    if (cm == nullptr && om == nullptr && !plainShape && !record) {
        if (src.type.isUnknown() || isAmbientType(src.type)) {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: cannot destructure a value of "
                              "unknown type; annotate the source");
        } else {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: cannot destructure a value of "
                              "type '" +
                                  describe(src.type) + "'");
        }
        return;
    }
    const auto access = [&](const std::string& name) -> std::string {
        if (om != nullptr) {
            return om->isInterface ? src.text + "." + mapIdent(name) + "()"
                                   : src.text + "->" + mapIdent(name);
        }
        if (cm != nullptr) return src.text + "->" + mapIdent(name);
        if (plainShape) return src.text + "->" + mapIdent(name);
        return src.text + "." + mapIdent(name);
    };
    const auto known = [&](const std::string& name) -> bool {
        if (om != nullptr) return findObjectField(*om, name) != nullptr;
        if (cm != nullptr) {
            const ClassMemberModel* m = findClassMember(*cm, name);
            return m != nullptr && !m->isMethod && m->ctor == nullptr;
        }
        // A plain object shape carries its own field list (the record layout
        // is the only other field table).
        if (plainShape) return hasShapeField(shapeFields, name);
        return hasField(fields, name);
    };
    // An optional field may be empty: a pattern default fills that case.
    const auto optional = [&](const std::string& name) -> bool {
        if (om != nullptr) {
            const ObjectFieldModel* f = findObjectField(*om, name);
            return f != nullptr && f->optional;
        }
        return optionalShapeField(shapeFields, name);
    };
    // The field's own cp3 type: a nested pattern reads the field with the
    // same model/class/shape access the outer pattern uses.
    const auto fieldType = [&](const std::string& name) -> Type {
        if (om != nullptr) {
            const ObjectFieldModel* f = findObjectField(*om, name);
            if (f == nullptr) return Type::unknown();
            return f->optional ? Type::nullable(f->type) : f->type;
        }
        if (cm != nullptr) {
            const ClassMemberModel* m = findClassMember(*cm, name);
            if (m == nullptr || m->isMethod || m->ctor != nullptr) {
                return Type::unknown();
            }
            return m->type;
        }
        if (plainShape) {
            for (const ObjectField& f : shapeFields) {
                if (f.name == name) return f.type;
            }
        }
        return Type::unknown();
    };
    for (const auto& propPtr : p.properties) {
        if (!propPtr) continue;
        const ObjectPatternProperty& prop = *propPtr;
        const std::string name = patternKeyName(prop.key.get(), prop.computed);
        if (name.empty()) {
            reportUnsupported(
                prop, diagcode::kTypeMismatch,
                prop.computed
                    ? "type mismatch: a computed destructuring key must be a "
                      "string or number literal"
                    : "type mismatch: this destructuring property has no name");
            continue;
        }
        if (!known(name)) {
            reportUnsupported(prop, diagcode::kMemberNotFound,
                              "property '" + name +
                                  "' does not exist on type '" + describe(base) +
                                  "'");
            continue;
        }
        const Node* target = prop.value.get();
        const AssignmentPattern* fallback = nullptr;
        if (target && target->kind == NodeKind::AssignmentPattern) {
            fallback = &static_cast<const AssignmentPattern&>(*target);
        }
        std::string valueText = access(name);
        if (fallback != nullptr) {
            if (optional(name)) {
                // `{ age = 18 }`: the default fills the empty optional.
                valueText = "((" + valueText + ").has_value() ? *(" + valueText +
                            ") : (" + expr(*fallback->right) + "))";
            }
            // A field that is always present never misses: the default has
            // nothing to fire on, and the field value is the binding.
            target = fallback->left.get();
        }
        if (!target) continue;
        Type valueType = fieldType(name);
        if (fallback != nullptr && optional(name) && valueType.isNullable()) {
            // The default already filled the empty case, so the binding sees
            // the field's value type.
            valueType = stripNullish(valueType);
        }
        bindPatternTarget(*target,
                          PatternSource{valueText, valueType, src.isConst}, out,
                          indent);
    }
    if (p.rest) {
        // The rest object: a fresh shape holding the source's fields except
        // the ones the pattern named (the TypeScript Omit).
        const auto& re = static_cast<const RestElement&>(*p.rest);
        if (!re.argument || re.argument->kind != NodeKind::Identifier) {
            reportUnsupported(*p.rest, diagcode::kTypeMismatch,
                              "type mismatch: a rest object pattern must bind "
                              "a name");
            return;
        }
        Type restType;
        const std::string value =
            restObjectText(p, src.text, src.type, restType);
        if (value.empty()) {
            reportUnsupported(
                *p.rest, diagcode::kTypeMismatch,
                "type mismatch: a rest object pattern needs a source with a "
                "static value shape");
            return;
        }
        bindPatternTarget(*p.rest, PatternSource{value, restType, src.isConst},
                          out, indent);
    }
}

void Generator::bindPatternTarget(const Node& target, const PatternSource& value,
                                  std::string& out, int indent) {
    const Node* bound = &target;
    if (bound->kind == NodeKind::RestElement) {
        bound = static_cast<const RestElement&>(*bound).argument.get();
    } else if (bound->kind == NodeKind::AssignmentPattern) {
        // A pattern default (`[a = 1]`) is resolved by the pattern that owns
        // the target: the array pattern reads the element or the default, the
        // object pattern reads the field or the default. What is left here is
        // the bound name.
        bound = static_cast<const AssignmentPattern&>(*bound).left.get();
    }
    if (!bound) return;
    if (bound->kind == NodeKind::ArrayPattern ||
        bound->kind == NodeKind::ObjectPattern) {
        unpackPattern(*bound, value, out, indent);
        return;
    }
    if (bound->kind != NodeKind::Identifier) {
        // Parser binding targets are identifiers, array patterns or object
        // patterns; anything else is a broken invariant.
        reportUnsupported(target, diagcode::kInternalError,
                          "internal error: a binding target must be a name or "
                          "a nested pattern");
        return;
    }
    const auto& id = static_cast<const Identifier&>(*bound);
    line(out, indent,
         (value.isConst ? std::string("const auto ") : std::string("auto ")) +
             mapIdent(id.name) + " = " + value.text + ";");
    Type declared = value.type;
    // A tuple rest binding is typed `unknown[]` by sema (the tail types live
    // on the source tuple); the walker passes the precise tail array as the
    // site type, so it wins over that placeholder.
    const bool sitePrecise =
        !value.type.isUnknown() && !value.type.isError() &&
        !isPlaceholderArray(value.type);
    if (const Type* st = sema_.typeOf(id.nodeId);
        st && !st->isUnknown() && !st->isError() && !isAmbientType(*st) &&
        !(isPlaceholderArray(*st) && sitePrecise)) {
        declared = *st;
    }
    if (intOn() && classify(declared).shape == Shape::Number) intFail();
    declare(id.name, declared);
}

}  // namespace cp3
