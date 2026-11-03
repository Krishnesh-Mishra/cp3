// =============================================================================
// src/std/object/codegen.cpp - `Object.keys` / `.values` / `.entries` lowering.
//
// The argument's static value shape fixes the field list and order. Each call
// evaluates the receiver once, then builds a cp3 array:
//
//   Object.keys(o)    -> makeArray<std::string>({ "a", "b" })
//   Object.values(o)  -> makeArray<V>({ o->a, o->b })
//   Object.entries(o) -> makeArray<tuple<string, V>>({ make_tuple("a", o->a), ... })
//
// Sema (src/std/object/sema.cpp) owns the result types; this unit only emits
// the matching array construction. Shapes with no static field list are
// rejected there, so an empty spelling here is a real lowering gap.
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
