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
// An object pattern whose source is neither (for example an interface with an
// optional field, which the value-shape gate does not represent) reports
// CP3002; that representation is the pending object-model decision.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/object_rest.h"
#include "detail/type_map.h"

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

// The literal field name of a binding-pattern key ("" for a computed key or a
// key kind the record surfaces do not name).
std::string patternKeyName(const Expr* key, bool computed) {
    if (computed || key == nullptr) return std::string();
    if (key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    if (key->kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(*key).raw;
    }
    return std::string();
}

// Sema's "not statically known" marker: the binding's own type is the one
// the emitter inferred, not the ambient placeholder.
bool isAmbientType(const Type& t) {
    return t.kind == TypeKind::Object && t.name == "$ambient";
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
        notImplemented(d, "destructuring without an initializer");
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
            if (target && (target->kind == NodeKind::ArrayPattern ||
                           target->kind == NodeKind::ObjectPattern)) {
                pattern = target;
            }
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
        // the body prologue unpacks.
        name = "cp3_param_" + std::to_string(index);
    }

    const std::optional<Type> pt = parameterType(p, fd, index);
    if (p.isRest) {
        // `...rest: T[]`: the C++ parameter is one array (calls pack their
        // trailing arguments, emit/call.cpp). The default covers a call that
        // passes no rest arguments at all.
        std::string elem;
        if (pt && pt->kind == TypeKind::Array && !pt->parts.empty()) {
            const CppInfo ec = classify(pt->parts.front());
            if (ec.representable) elem = ec.cpp;
        }
        if ((!id && restPattern == nullptr) || elem.empty()) {
            notImplemented(p, "this rest parameter type");
            return "/* rest */";
        }
        if (index != 0) {
            // Only a trailing `...rest` after fixed parameters stays out of
            // this wave: the `rest_param_leading` ruling belongs to the
            // closures lane (its expectation is per-JS wrong; see the report).
            notImplemented(p, "a rest parameter after fixed parameters");
            return "/* rest */";
        }
        needsTypedArray_ = true;
        std::string out = "::cp3::runtime::Array<" + elem + "> " + name;
        if (includeDefault) {
            out += " = ::cp3::runtime::makeArray<" + elem + ">(std::vector<" +
                   elem + ">{})";
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
    notImplemented(pattern, "this binding pattern");
}

void Generator::unpackArrayPattern(const ArrayPattern& p, const PatternSource& src,
                                   std::string& out, int indent) {
    if (src.type.kind != TypeKind::Array || src.type.parts.empty()) {
        notImplemented(p, "this destructuring source");
        return;
    }
    const Type elem = src.type.parts.front();
    const CppInfo ec = classify(elem);
    if (!ec.representable) {
        notImplemented(p, "this array element type");
        return;
    }
    needsTypedArray_ = true;
    std::size_t index = 0;
    for (const auto& targetPtr : p.elements) {
        if (targetPtr) {
            const Node* target = targetPtr.get();
            std::string at;
            if (target->kind == NodeKind::AssignmentPattern) {
                // A pattern default (`[a = 1] = xs`) fills the missing element:
                // the read must not panic on an array shorter than the
                // pattern.
                const auto& ap = static_cast<const AssignmentPattern&>(*target);
                at = "::cp3::runtime::arrayAtOrDefault<" + ec.cpp + ">(" +
                     src.text + ", " + std::to_string(index) + ".0, " +
                     expr(*ap.right) + ")";
                target = ap.left.get();
            } else {
                at = "::cp3::runtime::arrayAt(" + src.text + ", " +
                     std::to_string(index) + ".0)";
            }
            bindPatternTarget(*target, PatternSource{at, elem, src.isConst},
                              out, indent);
        }
        ++index;
    }
    if (!p.rest) return;
    const std::string rest =
        "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" + ec.cpp +
        ">(" + src.text + "->begin() + " + std::to_string(index) + ", " +
        src.text + "->end()))";
    bindPatternTarget(*p.rest, PatternSource{rest, Type::array(elem), src.isConst},
                      out, indent);
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
    const Type base = stripNullish(src.type);
    const ObjectModel* om = objectModelOf(base);
    const ClassModel* cm = classModelOf(base);
    bool modelUsed = om != nullptr && objectModelUsed(*om);
    if (om != nullptr && !modelUsed) om = nullptr;
    std::vector<ObjectField> shapeFields;
    const bool plainShape = objectShapeOf(base, structName, shapeFields);
    if (cm == nullptr && om == nullptr && !plainShape &&
        !recordLayout(src.type, structName, fields)) {
        notImplemented(p, "this destructuring source");
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
        if (plainShape) {
            // The all-primitive value shape: its own field list.
            for (const ObjectField& field : shapeFields) {
                if (field.name == name) return true;
            }
            return false;
        }
        return hasField(fields, name);
    };
    for (const auto& propPtr : p.properties) {
        if (!propPtr) continue;
        const ObjectPatternProperty& prop = *propPtr;
        const std::string name = patternKeyName(prop.key.get(), prop.computed);
        if (name.empty() || !known(name)) {
            notImplemented(prop, "this destructuring property");
            continue;
        }
        const Node* target = prop.value.get();
        std::string valueText = access(name);
        if (target && target->kind == NodeKind::AssignmentPattern) {
            // A pattern default (`{ age: a = 18 }`) fills the missing value:
            // a field that can be absent (an optional field) takes the
            // default when it holds no value; a record field that is always
            // present has nothing to fire on.
            const auto& ap = static_cast<const AssignmentPattern&>(*target);
            if (om != nullptr) {
                const ObjectFieldModel* field = findObjectField(*om, name);
                if (field != nullptr && field->optional) {
                    valueText = src.text + "->" + mapIdent(name) + ".value_or(" +
                                expr(*ap.right) + ")";
                }
            }
            target = ap.left.get();
        }
        if (!target) continue;
        bindPatternTarget(*target, PatternSource{valueText, Type::unknown(),
                                                 src.isConst},
                          out, indent);
    }
    if (p.rest) unpackObjectRest(p, src, out, indent);
}

// The object rest (`{ a, ...rest }`): the rest binding of an object pattern.
// The rest object is a fresh value of the source's type that holds the
// properties the pattern does not name.
void Generator::unpackObjectRest(const ObjectPattern& p, const PatternSource& src,
                                 std::string& out, int indent) {
    const std::string text = detail::objectRestText(p, src.type, src.text);
    if (text.empty()) {
        // A class instance, an interface wrapper or a std/json record: cp3
        // has no way to build a rest object for them.
        notImplemented(*p.rest, "...rest object pattern");
        return;
    }
    bindPatternTarget(*p.rest, PatternSource{text, src.type, src.isConst}, out,
                      indent);
    // The rest binding's cp3 type is the source's own type: sema models the
    // rest as an array of the remaining elements, which cp3 cannot spell for
    // an object source.
    const Node* target =
        static_cast<const RestElement&>(*p.rest).argument.get();
    if (target && target->kind == NodeKind::Identifier) {
        declare(static_cast<const Identifier&>(*target).name, src.type);
    }
}

void Generator::bindPatternTarget(const Node& target, const PatternSource& value,
                                  std::string& out, int indent) {
    const Node* bound = &target;
    if (bound->kind == NodeKind::RestElement) {
        bound = static_cast<const RestElement&>(*bound).argument.get();
    }
    if (!bound) return;
    if (bound->kind == NodeKind::ArrayPattern ||
        bound->kind == NodeKind::ObjectPattern) {
        unpackPattern(*bound, value, out, indent);
        return;
    }
    if (bound->kind != NodeKind::Identifier) {
        notImplemented(target, "this binding target");
        return;
    }
    const auto& id = static_cast<const Identifier&>(*bound);
    line(out, indent,
         (value.isConst ? std::string("const auto ") : std::string("auto ")) +
             mapIdent(id.name) + " = " + value.text + ";");
    Type declared = value.type;
    if (const Type* st = sema_.typeOf(id.nodeId); st && !st->isUnknown() &&
                                                   !st->isError() &&
                                                   !isAmbientType(*st)) {
        declared = *st;
    }
    if (intOn() && classify(declared).shape == Shape::Number) intFail();
    declare(id.name, declared);
}

}  // namespace cp3
