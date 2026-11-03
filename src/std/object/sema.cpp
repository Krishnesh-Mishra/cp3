// =============================================================================
// src/std/object/sema.cpp - the `Object.keys` / `.values` / `.entries` surface.
//
// cp3's object literals have a statically known shape (`$o{...}`), so the
// three enumeration helpers can be typed without runtime reflection. The
// result element types follow the shape:
//
//   Object.keys(o)    -> string[]
//   Object.values(o)  -> V[]  (V is the unified field type)
//   Object.entries(o) -> [string, V][]
//
// Codegen (src/std/object/codegen.cpp) lowers the same shape to array
// construction from the generated value struct. An open/computed shape has no
// static field list; that is a type error, not a silent `unknown[]`.
// =============================================================================
#include "detail/analyzer.h"

#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

bool isObjectBuiltin(const std::string& name) {
    return name == "keys" || name == "values" || name == "entries" ||
           name == "assign" || name == "is" || name == "freeze";
}

}  // namespace

bool Analyzer::tryInferObjectCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Object") {
        return false;
    }
    if (!isObjectBuiltin(m.property)) return false;
    Binding objectBinding;
    if (lookup("Object", objectBinding) && !objectBinding.isAmbient) {
        return false;  // a user binding shadows the global
    }
    inferArgs(call);
    if (m.property == "is") {
        if (call.arguments.size() != 2 || !call.arguments.front() ||
            !call.arguments[1]) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.is expects two arguments");
            out = withType(call, Type::unknown());
            return true;
        }
        out = withType(call, Type::boolean());
        return true;
    }
    if (m.property == "freeze") {
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.freeze expects one argument");
            out = withType(call, Type::unknown());
            return true;
        }
        const Type recv = typeOfExpr(*call.arguments.front());
        if (recv.isUnknown()) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object.freeze requires a value with a known type");
            out = withType(call, Type::unknown());
            return true;
        }
        out = withType(call, recv);
        return true;
    }
    if (m.property == "assign") {
        if (call.arguments.size() < 2 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.assign expects at least two "
                   "arguments");
            out = withType(call, Type::unknown());
            return true;
        }
        // The first argument is the target that is mutated and returned. The
        // remaining sources have to be objects with statically known fields
        // (extra properties are ignored; the target's shape is fixed).
        const Type target = typeOfExpr(*call.arguments.front());
        if (target.isUnknown()) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object.assign target must have a known type");
            out = withType(call, Type::unknown());
            return true;
        }
        if (target.kind != TypeKind::Object) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object.assign target must be an object");
            out = withType(call, target);
            return true;
        }
        for (std::size_t i = 1; i < call.arguments.size(); ++i) {
            const ExprPtr& arg = call.arguments[i];
            if (!arg) continue;
            const Type source = typeOfExpr(*arg);
            const Shape* sourceShape = shapeOf(source);
            if (source.kind != TypeKind::Object || sourceShape == nullptr ||
                sourceShape->open || sourceShape->tuple) {
                report(diagcode::kTypeMismatch, arg->span,
                       "Object.assign sources must be objects with statically "
                       "known fields");
            }
        }
        out = withType(call, target);
        return true;
    }
    if (call.arguments.size() != 1 || !call.arguments.front()) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: Object." + m.property +
                   " expects one object argument");
        out = withType(call, Type::unknown());
        return true;
    }
    const Type object = typeOfExpr(*call.arguments.front());
    const Shape* shape = shapeOf(object);
    if (shape == nullptr || shape->open || shape->tuple ||
        shape->fields.empty()) {
        report(diagcode::kTypeMismatch, call.span,
               "type mismatch: Object." + m.property +
                   " requires an object with statically known fields");
        out = withType(call, Type::unknown());
        return true;
    }
    if (m.property == "keys") {
        out = withType(call, Type::array(Type::string()));
        return true;
    }
    Type value = shape->fields.front().type;
    if (shape->fields.front().optional) {
        value = unify(value, Type::undefined());
    }
    for (std::size_t i = 1; i < shape->fields.size(); ++i) {
        Type next = shape->fields[i].type;
        if (shape->fields[i].optional) next = unify(next, Type::undefined());
        value = unify(value, next);
    }
    if (m.property == "values") {
        out = withType(call, Type::array(value));
        return true;
    }
    Type entry = makeObjectType({}, false, true,
                                {Type::string(), value});
    out = withType(call, Type::array(entry));
    return true;
}

}  // namespace cp3
