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
//     (`arrayAt(source, i)`);
//   * an object-literal target reads the named field of the source
//     (`((source)->field)`).
//
// The right side is evaluated exactly once (into a local) and the
// assignment's own value is that right side (JS), so the whole write lowers
// to a lambda that returns it. A target form this write does not cover (a
// compound write to a pattern, a member expression inside the pattern) is
// left to the caller, which reports it.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/mangle.h"
#include "detail/type_map.h"

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
// local, then every target of the pattern is written. The expression's value
// is the right side, so the whole thing lowers to a lambda that returns it.
// An empty result means the target form is not one this write covers (a
// compound write to a pattern, a member expression inside the pattern); the
// caller reports it.
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
        const std::string name = mapIdent(id.name);
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
    return false;
}

// The array-literal half of a destructuring assignment: every element of the
// source is written into the target the pattern names.
bool Generator::assignDestructuringElements(const ArrayLiteral& lit,
                                            const std::string& access,
                                            const Type& source,
                                            std::string& body) {
    if (source.kind != TypeKind::Array || source.parts.empty()) return false;
    needsTypedArray_ = true;
    const Type elem = source.parts.front();
    std::size_t index = 0;
    for (const auto& el : lit.elements) {
        if (!el) { ++index; continue; }
        const std::string at = "::cp3::runtime::arrayAt(" + access + ", " +
                               std::to_string(index) + ".0)";
        if (!assignDestructuringTarget(*el, at, elem, body)) return false;
        ++index;
    }
    return true;
}

// The object-literal half of a destructuring assignment: every named field of
// the source is written into the target the pattern names.
bool Generator::assignDestructuringFields(const ObjectLiteral& lit,
                                          const std::string& access,
                                          const Type& source, std::string& body) {
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(source, structName, fields)) return false;
    for (const auto& propertyPtr : lit.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (!p || !p->value) return false;
        const std::string name = destructuringKeyName(*p);
        const ObjectField* field =
            name.empty() ? nullptr : shapeField(fields, name);
        if (!field) return false;
        const std::string at = "((" + access + ")->" + mapIdent(name) + ")";
        if (!assignDestructuringTarget(*p->value, at, field->type, body)) {
            return false;
        }
    }
    return true;
}

}  // namespace cp3
