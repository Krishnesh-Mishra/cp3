// =============================================================================
// src/codegen/src/emit/destructure_assign.cpp - the destructuring assignment
// (STYLE.md section 8).
//
// A destructuring *assignment* (`[a, b] = expr`, `{ x } = expr`) writes the
// bindings the pattern names. The parser reads the target as an array/object
// literal whose elements are the assignment targets, so the write walks that
// literal:
//
//   * an identifier target is written (`x = value`), through the shared cell
//     (`x.set(value)`) when the binding is one;
//   * an array-literal target reads the element of the source
//     (`arrayAt(source, i)`, or `std::get<i>` for a tuple source);
//   * an object-literal target reads the named field of the source
//     (`((source)->field)` for a class / structural object, `.field` for a
//     record, or the interface accessor);
//   * a member or index target writes that lvalue (`o.x = value`,
//     `t[0] = value`).
//
// The right side is evaluated exactly once (into a local) and the
// assignment's own value is that right side (JS), so the whole write lowers
// to a lambda that returns it. A target form this write does not cover (a
// compound write to a pattern) is left to the caller, which reports it.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The literal field name of an object-literal property ("" for a computed key
// or a key the record surfaces do not name).
std::string destructuringKeyName(const ObjectProperty& p) {
    if (p.computed || p.isMethod || !p.key) return std::string();
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    return std::string();
}

// The field of a value shape named `name` (nullptr when it has none).
const ObjectField* shapeField(const std::vector<ObjectField>& fields,
                              const std::string& name) {
    for (const ObjectField& candidate : fields) {
        if (candidate.name == name) return &candidate;
    }
    return nullptr;
}

}  // namespace

// A destructuring *assignment*: the right side is evaluated once into a
// local, then every target of the pattern is written. The webserverion's value
// is the right side, so the whole thing lowers to a lambda that returns it.
// An empty result means the target form is not one this write covers (a
// compound write to a pattern); the caller reports it.
std::string Generator::emitDestructuringAssign(const AssignmentExpr& a) {
    // A compound write to a pattern has no JS meaning.
    if (a.op != AssignOp::Assign || !a.value) return std::string();
    // The int64 fast path owns the plain bindings it rewrote; a pattern write
    // is not one of them, so the attempt gives up here.
    if (intOn()) intFail();
    const std::string temp = freshLabel("assign_");
    std::string body = "auto " + temp + " = " + expr(*a.value) + "; ";
    if (!assignDestructuringTarget(*a.target, temp, effectiveType(*a.value),
                                   body)) {
        return std::string();
    }
    body += "return " + temp + ";";
    return "([&]{ " + body + " }())";
}

// One target of a destructuring assignment. An identifier is written (through
// the shared cell when the binding is one); a nested array or object literal
// is unpacked in turn, reading the value out of the source.
bool Generator::assignDestructuringTarget(const Node& target,
                                          const std::string& access,
                                          const Type& source, std::string& body) {
    if (target.kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(target);
        const auto binding = lookup(id.name);
        const std::string name = (binding && binding->global)
                                     ? moduleRef(id.name)
                                     : mapIdent(id.name);
        body += isCell(id.name) ? name + ".set(" + access + "); "
                                : name + " = " + access + "; ";
        return true;
    }
    if (target.kind == NodeKind::ArrayLiteral) {
        return assignDestructuringElements(
            static_cast<const ArrayLiteral&>(target), access, source, body);
    }
    if (target.kind == NodeKind::ObjectLiteral) {
        return assignDestructuringFields(
            static_cast<const ObjectLiteral&>(target), access, source, body);
    }
    if (target.kind == NodeKind::IndexExpr) {
        // `[t[0]] = ...` / `[a[1]] = ...`: the element is the lvalue.
        const auto& ix = static_cast<const IndexExpr&>(target);
        if (!ix.object || !ix.index) return false;
        const Type object = effectiveType(*ix.object);
        if (isTupleType(object)) {
            const auto index = integralLiteralValue(*ix.index);
            if (!index || *index < 0 ||
                static_cast<std::size_t>(*index) >= object.parts.size()) {
                return false;
            }
            needsTuple_ = true;
            body += "std::get<" + std::to_string(*index) + ">(" +
                    expr(*ix.object) + ") = " + access + "; ";
            return true;
        }
        if (object.kind == TypeKind::Array && !object.parts.empty()) {
            CppInfo ec;
            if (!arrayElementInfo(arrayElemOf(object), ec)) return false;
            needsTypedArray_ = true;
            body += "::cp3::runtime::arrayWriteAt<" + ec.cpp + ">(" +
                    expr(*ix.object) + ", " + indexOperandText(*ix.index) +
                    ", " + access + "); ";
            return true;
        }
        return false;
    }
    if (target.kind == NodeKind::MemberExpr) {
        // `[o.x] = ...`: a class / structural-object field is the lvalue.
        const auto& m = static_cast<const MemberExpr&>(target);
        if (!m.object) return false;
        const Type base = stripNullish(effectiveType(*m.object));
        if (const ClassModel* cm = classModelOf(base)) {
            const ClassMemberModel* member =
                findClassMemberChain(*cm, m.property, nullptr);
            if (member == nullptr || member->isMethod) return false;
            const std::string lvalue =
                member->isStatic
                    ? cm->cpp + "::" + mapIdent(member->name)
                    : "(" + expr(*m.object) + ")->" + mapIdent(member->name);
            body += lvalue + " = " + access + "; ";
            return true;
        }
        const ObjectModel* om = objectModelOf(base);
        const bool usable =
            om != nullptr && !om->isInterface &&
            ModelRegistry::get().objectUsable(om->label) &&
            objectModelUsed(*om);
        if (usable) {
            const ObjectFieldModel* field = findObjectField(*om, m.property);
            if (field == nullptr || field->isFunction || field->optional) {
                return false;
            }
            body += "((" + expr(*m.object) + ")->" + mapIdent(field->name) +
                    " = " + access + "); ";
            return true;
        }
        std::string structName;
        std::vector<ObjectField> fields;
        if (objectShapeOf(base, structName, fields)) {
            const ObjectField* field = shapeField(fields, m.property);
            if (field == nullptr) return false;
            body += "((" + expr(*m.object) + ")->" + mapIdent(field->name) +
                    " = " + access + "); ";
            return true;
        }
        return false;
    }
    return false;
}

// The array-literal half of a destructuring assignment: every element of the
// source is written into the target the pattern names.
bool Generator::assignDestructuringElements(const ArrayLiteral& lit,
                                            const std::string& access,
                                            const Type& source,
                                            std::string& body) {
    // A tuple has fixed slots read by position; an array is read by index.
    const bool tuple = isTupleType(source);
    if (!tuple && (source.kind != TypeKind::Array || source.parts.empty())) {
        return false;
    }
    if (tuple && lit.elements.size() > source.parts.size()) return false;
    if (!tuple) needsTypedArray_ = true;
    const Type elem = tuple ? Type::unknown() : source.parts.front();
    std::size_t index = 0;
    for (const auto& el : lit.elements) {
        if (!el) { ++index; continue; }
        const Type atType = tuple ? source.parts[index] : elem;
        std::string at;
        if (tuple) {
            needsTuple_ = true;
            at = "std::get<" + std::to_string(index) + ">(" + access + ")";
        } else {
            at = "::cp3::runtime::arrayAt(" + access + ", " +
                 std::to_string(index) + ".0)";
        }
        if (!assignDestructuringTarget(*el, at, atType, body)) return false;
        ++index;
    }
    return true;
}

// The object-literal half of a destructuring assignment: every named field of
// the source is written into the target the pattern names.
bool Generator::assignDestructuringFields(const ObjectLiteral& lit,
                                          const std::string& access,
                                          const Type& source, std::string& body) {
    // The source's value spelling: a class / interface / plain-object model,
    // or the std/json record (the same forms the declaration pattern reads).
    const Type base = stripNullish(source);
    const ObjectModel* om = objectModelOf(base);
    if (om != nullptr && !objectModelUsed(*om)) om = nullptr;
    const ClassModel* cm = classModelOf(base);
    std::string structName;
    std::vector<ObjectField> shapeFields;
    const bool plainShape =
        om == nullptr && cm == nullptr &&
        objectShapeOf(base, structName, shapeFields);
    std::vector<std::string> recordFields;
    const bool record =
        om == nullptr && cm == nullptr && !plainShape &&
        recordLayout(source, structName, recordFields);
    if (om == nullptr && cm == nullptr && !plainShape && !record) return false;
    const auto fieldType = [&](const std::string& name) -> std::optional<Type> {
        if (om != nullptr) {
            const ObjectFieldModel* f = findObjectField(*om, name);
            if (f == nullptr) return std::nullopt;
            return f->optional ? Type::nullable(f->type) : f->type;
        }
        if (cm != nullptr) {
            const ClassMemberModel* m = findClassMember(*cm, name);
            if (m == nullptr || m->isMethod || m->ctor != nullptr) {
                return std::nullopt;
            }
            return m->type;
        }
        if (plainShape) {
            const ObjectField* f = shapeField(shapeFields, name);
            return f == nullptr ? std::nullopt : std::optional<Type>(f->type);
        }
        for (const std::string& f : recordFields) {
            if (f == name) return Type::unknown();
        }
        return std::nullopt;
    };
    for (const auto& propertyPtr : lit.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (!p || !p->value) return false;
        const std::string name = destructuringKeyName(*p);
        if (name.empty()) return false;
        const std::optional<Type> ft = fieldType(name);
        if (!ft) return false;
        std::string at;
        if (om != nullptr && om->isInterface) {
            at = "((" + access + ")." + mapIdent(name) + "())";
        } else if (om != nullptr || cm != nullptr || plainShape) {
            at = "((" + access + ")->" + mapIdent(name) + ")";
        } else {
            at = "((" + access + ")." + mapIdent(name) + ")";
        }
        if (!assignDestructuringTarget(*p->value, at, *ft, body)) {
            return false;
        }
    }
    return true;
}

}  // namespace cp3
