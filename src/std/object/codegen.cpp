// =============================================================================
// src/std/object/codegen.cpp - `Object.keys` / `.values` / `.entries` lowering,
// plus `Object.fromEntries` / `Object.groupBy` (ROADMAP L).
//
// The argument's static value shape fixes the field list and order. Each call
// evaluates the receiver once, then builds a cp3 array:
//
//   Object.keys(o)    -> makeArray<std::string>({ "a", "b" })
//   Object.values(o)  -> makeArray<V>({ o->a, o->b })
//   Object.entries(o) -> makeArray<tuple<string, V>>({ make_tuple("a", o->a), ... })
//
// `Record<string, V>` canonicalizes to `Map<string, V>`, so the two
// dynamic-key helpers lower onto the collections runtime (the runtime logic
// for both lives here, in the emitted code; no new include flag is needed
// beyond the existing `needsCollections_`):
//
//   Object.fromEntries(entries) -> makeMap<std::string, V>() + a set loop
//                                  over std::get<0>/<1> (pair and tuple
//                                  entries alike, as `new Map(entries)` does)
//   Object.groupBy(list, keyFn) -> collections::groupBy<std::string, T>(list, cb)
//
// Sema (src/std/object/sema.cpp) owns the result types; this unit only emits
// the matching construction. Shapes with no static field list are rejected
// there, so an empty spelling here is a real lowering gap.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

Type elementTypeOf(const Type& array) {
    if (array.kind != TypeKind::Array || array.parts.empty()) {
        return Type::unknown();
    }
    return array.parts.front();
}

// True for the value kinds that `Object.is` compares with the numeric /
// string / bool runtime helper (JS semantics: NaN equals NaN, +0 != -0).
bool isIsPrimitive(const Type& t) {
    const Type base = t.literalBaseType();
    return base.kind == TypeKind::Number || base.kind == TypeKind::String ||
           base.kind == TypeKind::Boolean;
}

// True for kinds with no reference identity (primitives plus nullish).
bool isPrimitiveValue(const Type& t) {
    const Type base = t.literalBaseType();
    return base.kind == TypeKind::Number || base.kind == TypeKind::String ||
           base.kind == TypeKind::Boolean || base.kind == TypeKind::Null ||
           base.kind == TypeKind::Undefined || base.kind == TypeKind::Native;
}

// If `e` is `Object.freeze` / `.seal` / `.preventExtensions` applied to
// anything, return that method name; otherwise "".
std::string freezeFamilyCall(const Expr& e) {
    if (e.kind != NodeKind::CallExpr) return "";
    const auto& call = static_cast<const CallExpr&>(e);
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr) return "";
    const auto& m = static_cast<const MemberExpr&>(*call.callee);
    if (m.property != "freeze" && m.property != "seal" &&
        m.property != "preventExtensions") {
        return "";
    }
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<const Identifier&>(*m.object).name != "Object") {
        return "";
    }
    return m.property;
}

// True when `e` is a member/index chain rooted in a freeze-family call, e.g.
// `Object.freeze(o).nested` or `Object.freeze(o).list[0]`. This is the
// deep-freeze query surface: the nested value of a frozen object is itself
// frozen under the deep-freeze reading of `freeze` (see emitObjectCall).
bool freezeRooted(const Expr& e) {
    const Expr* cur = &e;
    while (cur != nullptr) {
        if (!freezeFamilyCall(*cur).empty()) return true;
        if (cur->kind == NodeKind::MemberExpr) {
            cur = static_cast<const MemberExpr&>(*cur).object.get();
        } else if (cur->kind == NodeKind::IndexExpr) {
            cur = static_cast<const IndexExpr&>(*cur).object.get();
        } else {
            return false;
        }
    }
    return false;
}

// The freeze-family method a freeze-rooted chain is rooted in ("" if none).
std::string freezeRootMethod(const Expr& e) {
    const Expr* cur = &e;
    while (cur != nullptr) {
        const std::string name = freezeFamilyCall(*cur);
        if (!name.empty()) return name;
        if (cur->kind == NodeKind::MemberExpr) {
            cur = static_cast<const MemberExpr&>(*cur).object.get();
        } else if (cur->kind == NodeKind::IndexExpr) {
            cur = static_cast<const IndexExpr&>(*cur).object.get();
        } else {
            return "";
        }
    }
    return "";
}

}  // namespace

std::string Generator::emitObjectCall(const CallExpr& c, const MemberExpr& m) {
    if (m.property == "assign" || m.property == "freeze" ||
        m.property == "is") {
        if (m.property == "assign") {
            if (c.arguments.size() < 2 || !c.arguments.front()) {
                reportCodegenError(c, diagcode::kArityMismatch,
                                   "arity mismatch: Object.assign expects at "
                                   "least two arguments");
                return "nullptr";
            }
            // JS semantics: the first argument is the (shared) target, mutated
            // in place and returned; aliases of the target observe the writes.
            const Type target = effectiveType(*c.arguments.front());
            std::string targetName;
            std::vector<ObjectField> targetFields;
            if (!objectShapeOf(target, targetName, targetFields)) {
                reportCodegenError(c, diagcode::kTypeMismatch,
                                   "Object.assign requires objects with "
                                   "statically known fields");
                return "nullptr";
            }
            const std::string targetExpr = expr(*c.arguments.front());
            std::string out =
                "([&](){ const auto cp3_t = (" + targetExpr +
                ");";
            for (std::size_t i = 1; i < c.arguments.size(); ++i) {
                const Expr* arg = c.arguments[i].get();
                if (!arg) continue;
                const Type source = effectiveType(*arg);
                std::string sourceName;
                std::vector<ObjectField> sourceFields;
                if (!objectShapeOf(source, sourceName, sourceFields)) {
                    reportCodegenError(*arg, diagcode::kTypeMismatch,
                                       "Object.assign sources must be objects "
                                       "with statically known fields");
                    continue;
                }
                out += " { const auto& cp3_s = (" + expr(*arg) + "); ";
                for (const ObjectField& tf : targetFields) {
                    for (const ObjectField& sf : sourceFields) {
                        if (sf.name == tf.name &&
                            sf.type.kind != TypeKind::Function) {
                            out += "cp3_t->" + mapIdent(tf.name) + " = "
                                   "cp3_s->" + mapIdent(sf.name) + "; ";
                        }
                    }
                }
                out += "} ";
            }
            out += "return cp3_t; })()";
            return out;
        }
        if (m.property == "freeze") {
            const std::string recv = expr(*c.arguments.front());
            // cp3 objects are shared by reference handle; mutating one handle
            // is visible through all aliases, so a no-op is the correct v0.2
            // approximation of Object.freeze.
            return recv;
        }
        // Object.is: NaN is considered equal to itself, and +0 is not equal to
        // -0, matching JS Object.is exactly.
        const std::string lhs = expr(*c.arguments.front());
        const std::string rhs = expr(*c.arguments[1]);
        return "::cp3::runtime::cp3_object_is(" + lhs + ", " + rhs + ")";
    }
    if (m.property == "fromEntries") {
        if (c.arguments.size() != 1 || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: Object.fromEntries expects "
                               "exactly one argument (the entries array)");
            return "0.0";
        }
        const Type ret = effectiveType(c);
        if (ret.kind != TypeKind::Class || ret.name != "Map" ||
            ret.parts.size() < 2) {
            reportTypeError(c, "type mismatch: Object.fromEntries needs an "
                               "array of [key, value] entries");
            return "0.0";
        }
        const CppInfo vc = classify(ret.parts[1]);
        if (!vc.representable) {
            reportTypeError(c, "type mismatch: Object.fromEntries needs a "
                               "known value type (Record<string, V>)");
            return "0.0";
        }
        needsCollections_ = true;
        needsTuple_ = true;
        // Same runtime as `new Map<K, V>(entries)`: std::get reads a pair
        // (Map.entries) and a tuple (Object.entries / a [k, v] literal)
        // alike, so one loop covers both sources. Later duplicate keys
        // overwrite earlier ones, in place (Map.set semantics).
        return "([&]{ auto cp3_m = ::cp3::runtime::collections::makeMap<std::"
               "string, " +
               vc.cpp + ">(); for (const auto& cp3_e : *(" +
               expr(*c.arguments.front()) + ")) " +
               "cp3_m->set(std::get<0>(cp3_e), std::get<1>(cp3_e)); "
               "return cp3_m; }())";
    }
    if (m.property == "groupBy") {
        if (c.arguments.size() != 2 || !c.arguments.front() ||
            !c.arguments[1]) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: Object.groupBy expects "
                               "exactly two arguments (the list and the key "
                               "callback)");
            return "0.0";
        }
        const Type ret = effectiveType(c);
        if (ret.kind != TypeKind::Class || ret.name != "Map" ||
            ret.parts.size() < 2 ||
            ret.parts[1].kind != TypeKind::Array ||
            ret.parts[1].parts.empty()) {
            reportTypeError(c, "type mismatch: Object.groupBy needs an array "
                               "and a key callback returning string");
            return "0.0";
        }
        const Type elem = ret.parts[1].parts.front();
        const CppInfo ec = classify(elem);
        if (!ec.representable) {
            reportTypeError(c, "type mismatch: Object.groupBy needs a known "
                               "element type");
            return "0.0";
        }
        needsCollections_ = true;
        const Expr& items = *c.arguments.front();
        const Expr& cb = *c.arguments[1];
        // Callback context (item, index, array), matching sema - the same
        // adaptation as Map.groupBy (src/std/collections/codegen.cpp): an
        // arrow takes the context types, any other callable is adapted to
        // the runtime's 3-argument call with the prefix it declares.
        const std::string arrayCpp = "::cp3::runtime::Array<" + ec.cpp + ">";
        std::string callback;
        if (cb.kind == NodeKind::ArrowFunction) {
            callback = emitArrow(static_cast<const ArrowFunction&>(cb),
                                 {ec.cpp, "double", arrayCpp});
        } else {
            const Type fnType = effectiveType(cb);
            if (fnType.kind == TypeKind::Function && !fnType.parts.empty()) {
                const std::size_t arity =
                    fnType.parts.size() > 1 ? fnType.parts.size() - 1 : 0;
                if (arity > 3) {
                    reportTypeError(
                        cb, "type mismatch: the Object.groupBy callback takes "
                            "at most 3 argument(s), got " +
                                std::to_string(arity));
                    return "0.0";
                }
                const std::string fnText = expr(cb);
                std::string call;
                for (std::size_t i = 0; i < arity; ++i) {
                    if (i != 0) call += ", ";
                    call += "cp3_value" + std::to_string(i);
                }
                callback = "[cp3_cb = (" + fnText + ")](" + ec.cpp +
                           " cp3_value0, double cp3_value1, " + arrayCpp +
                           " cp3_value2) -> std::string { return cp3_cb(" +
                           call + "); }";
            } else {
                callback = expr(cb);
            }
        }
        return "::cp3::runtime::collections::groupBy<std::string, " +
               ec.cpp + ">(" + expr(items) + ", " + callback + ")";
    }
    if (m.property != "keys" && m.property != "values" &&
        m.property != "entries") {
        return std::string();
    }
    if (c.arguments.size() != 1 || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: Object." + m.property +
                               " expects one object argument");
        return "0.0";
    }
    const Type object = effectiveType(*c.arguments.front());
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(object, structName, fields)) {
        return std::string();
    }
    const std::string recv = expr(*c.arguments.front());
    const std::string bind = "const auto& cp3_o = (" + recv + "); ";
    if (m.property == "keys") {
        std::string list;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i != 0) list += ", ";
            list += "\"" + fields[i].name + "\"";
        }
        return "([&]{ " + bind + "(void)cp3_o; return "
               "::cp3::runtime::makeArray<std::string>("
               "std::vector<std::string>{" + list + "}); }())";
    }

    Type element = elementTypeOf(effectiveType(c));
    CppInfo elem = classify(element);
    if (!elem.representable && !fields.empty()) {
        elem = classify(fields.front().type);
    }
    if (!elem.representable) return std::string();
    if (m.property == "values") {
        std::string list;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i != 0) list += ", ";
            list += "cp3_o->" + mapIdent(fields[i].name);
        }
        return "([&]{ " + bind + "return "
               "::cp3::runtime::makeArray<" + elem.cpp + ">("
               "std::vector<" + elem.cpp + ">{" + list + "}); }())";
    }
    const CppInfo tuple = classify(element);
    if (tuple.representable) {
        elem = tuple;
    } else if (!fields.empty()) {
        const CppInfo value = classify(fields.front().type);
        if (!value.representable) return std::string();
        elem.cpp = "std::tuple<std::string, " + value.cpp + ">";
        elem.representable = true;
    } else {
        return std::string();
    }
    needsTuple_ = true;
    std::string list;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) list += ", ";
        list += "std::make_tuple(std::string(\"" + fields[i].name +
                "\"), cp3_o->" + mapIdent(fields[i].name) + ")";
    }
    return "([&]{ " + bind + "return "
           "::cp3::runtime::makeArray<" + elem.cpp + ">("
           "std::vector<" + elem.cpp + ">{" + list + "}); }())";
}

}  // namespace cp3
