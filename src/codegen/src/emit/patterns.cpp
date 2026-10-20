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

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
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

// An object literal for a known record type. Property order and repeated
// spreads follow JS semantics because the value is assembled by assignment in
// source order, and a spread source is evaluated once into a local.
std::string Generator::recordLiteralText(const ObjectLiteral& lit,
                                         const Type& expected) {
    std::string structName;
    std::vector<std::string> fields;
    if (!recordLayout(expected, structName, fields)) return std::string();
    const std::string outName = freshLabel("record_");
    std::string body = structName + " " + outName + "{};";
    for (const auto& propertyPtr : lit.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (p == nullptr) continue;
        if (p->value && p->value->kind == NodeKind::SpreadElement) {
            const auto& sp = static_cast<const SpreadElement&>(*p->value);
            if (!sp.argument) return std::string();
            std::string sourceStruct;
            std::vector<std::string> sourceFields;
            if (!recordLayout(effectiveType(*sp.argument), sourceStruct,
                              sourceFields)) {
                notImplemented(*p->value, "this spread property");
                return std::string();
            }
            const std::string source = freshLabel("spread_");
            body += " { const auto& " + source + " = " + expr(*sp.argument) + ";";
            for (const std::string& field : sourceFields) {
                if (!hasField(fields, field)) continue;
                body += " " + outName + "." + mapIdent(field) + " = " + source +
                        "." + mapIdent(field) + ";";
            }
            body += " }";
            continue;
        }
        const std::string name = propertyKeyName(*p);
        if (name.empty() || !hasField(fields, name)) {
            notImplemented(*p, "this object property");
            return std::string();
        }
        if (const Expr* value = propertyValue(*p)) {
            body += " " + outName + "." + mapIdent(name) + " = " + expr(*value) +
                    ";";
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
        if (p->name->kind != NodeKind::ArrayPattern &&
            p->name->kind != NodeKind::ObjectPattern) {
            continue;
        }
        const std::optional<Type> pt = parameterType(*p, fd, i);
        const std::string source = "cp3_param_" + std::to_string(i);
        const PatternSource src{source, pt.value_or(Type::unknown()), false};
        unpackPattern(*p->name, src, out, indent);
    }
}

std::optional<Type> Generator::parameterType(const Parameter& p,
                                             const FunctionDeclaration& fd,
                                             std::size_t index) const {
    if (p.type) {
        if (auto t = typeOfNode(*p.type)) {
            if (p.optional) return Type::nullable(*t);
            return t;
        }
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end() && index < it->second.paramTypes.size()) {
        return it->second.paramTypes[index];
    }
    return std::nullopt;
}

std::string Generator::parameterText(const Parameter& p, const FunctionDeclaration& fd,
                                     std::size_t index, bool includeDefault) {
    const Identifier* id = parameterIdentifier(p);
    const bool pattern = p.name && (p.name->kind == NodeKind::ArrayPattern ||
                                    p.name->kind == NodeKind::ObjectPattern);
    std::string name;
    if (id) {
        name = mapIdent(id->name);
    } else if (pattern) {
        // A destructured parameter: the C++ parameter is the generated handle
        // whose contents the body prologue unpacks.
        name = "cp3_param_" + std::to_string(index);
    } else {
        if (p.name) notImplemented(*p.name, "destructuring parameter");
        name = "cp3_param";
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
        if (!id || elem.empty()) {
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

    std::string type;
    if (pt) {
        CppInfo ci = classify(*pt);
        if (ci.representable) {
            type = ci.cpp;
        } else {
            // A destructured parameter annotated with a registered record
            // (an interface reified by the JSON record surface) uses that
            // struct's spelling; the prologue reads its fields.
            std::string structName;
            std::vector<std::string> fields;
            if (pattern && recordLayout(*pt, structName, fields)) {
                type = structName;
            }
        }
    }
    if (type.empty()) {
        notImplemented(p, "untyped parameter");
        type = "double";
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
            const std::string at = "::cp3::runtime::arrayAt(" + src.text + ", " +
                                   std::to_string(index) + ".0)";
            bindPatternTarget(*targetPtr, PatternSource{at, elem, src.isConst},
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
    // all-primitive value struct (and the std/json record) keeps `.field`.
    const Type base = stripNullish(src.type);
    const ObjectModel* om = objectModelOf(base);
    const ClassModel* cm = classModelOf(base);
    bool modelUsed = om != nullptr && objectModelUsed(*om);
    if (om != nullptr && !modelUsed) om = nullptr;
    if (cm == nullptr && om == nullptr &&
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
        return src.text + "." + mapIdent(name);
    };
    const auto known = [&](const std::string& name) -> bool {
        if (om != nullptr) return findObjectField(*om, name) != nullptr;
        if (cm != nullptr) {
            const ClassMemberModel* m = findClassMember(*cm, name);
            return m != nullptr && !m->isMethod && m->ctor == nullptr;
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
        if (target && target->kind == NodeKind::AssignmentPattern) {
            // Record fields are always present in this representation, so a
            // default has nothing to fire on; keep the binding target only.
            target = static_cast<const AssignmentPattern&>(*target).left.get();
        }
        if (!target) continue;
        const std::string valueText = access(name);
        bindPatternTarget(*target, PatternSource{valueText, Type::unknown(),
                                                 src.isConst},
                          out, indent);
    }
    if (p.rest) notImplemented(*p.rest, "...rest object pattern");
}

void Generator::bindPatternTarget(const Node& target, const PatternSource& value,
                                  std::string& out, int indent) {
    const Node* bound = &target;
    if (bound->kind == NodeKind::RestElement) {
        bound = static_cast<const RestElement&>(*bound).argument.get();
    } else if (bound->kind == NodeKind::AssignmentPattern) {
        // A pattern default (`[a = 1]`) is out of scope: arrayPattern defaults
        // are not specified through `arrayAt`, and object record fields are
        // always present.
        notImplemented(target, "this pattern default");
        bound = static_cast<const AssignmentPattern&>(*bound).left.get();
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
                                                   !st->isError()) {
        declared = *st;
    }
    if (intOn() && classify(declared).shape == Shape::Number) intFail();
    declare(id.name, declared);
}

}  // namespace cp3
