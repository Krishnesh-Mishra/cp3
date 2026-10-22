// =============================================================================
// src/codegen/src/emit/class_init.cpp - class construction: the constructor's
// arguments, a class literal and a spread into `new` (STYLE.md §8).
//
// The construction half of the spec §15 object surface: `new C(...)` and
// `let c: C = { ... }` both build an instance through the class's
// constructor, so this unit owns the constructor's argument list, the class
// literal's field stores and the spread dispatch. The member reads and calls
// live in emit/class_expr.cpp.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/class_model.h"
#include "detail/class_walk.h"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The literal property matching model field `name` (null when absent). A
// shorthand property (`{ x }`) carries its value in the key.
const Expr* literalValueOf(const ObjectLiteral& lit, const std::string& name) {
    for (const auto& p : lit.properties) {
        if (!p || p->computed) continue;
        if (keyNameOf(p->key.get()) != name) continue;
        if (p->value) return p->value.get();
        return p->key.get();
    }
    return nullptr;
}

// The constructor a `makeRef<C>(...)` call reaches: the class's own, or the
// nearest base's (a derived class without a constructor inherits the base's).
// `owner` is the class that declares it.
const ClassMemberModel* ctorOf(const ClassModel& cm, const ClassModel** owner) {
    const ClassModel* k = &cm;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        for (const ClassMemberModel& m : k->members) {
            if (m.ctor != nullptr) {
                if (owner != nullptr) *owner = k;
                return &m;
            }
        }
        k = k->baseName.empty() ? nullptr
                                : ModelRegistry::get().classByName(k->baseName);
    }
    if (owner != nullptr) *owner = nullptr;
    return nullptr;
}

// The cp3 type of one constructor parameter: the same computation the
// parameter's own C++ spelling uses (emit/class_decl.cpp's methodParamText).
Type ctorParamType(const SemaResult& sema, const Parameter& p,
                   const ClassModel& cm) {
    Type t = Type::unknown();
    if (p.type) {
        t = modelTypeFromTypeNode(*p.type);
    } else if (p.name) {
        if (const Type* st = sema.typeOf(p.name->nodeId)) t = *st;
    }
    rewriteTypeParams(t, cm.typeParams);
    if (p.optional && !t.isUnknown()) t = Type::nullable(t);
    return t;
}

}  // namespace

// `new C(...args)`: the spread array's elements are the constructor's
// arguments. One array carries one element type, so that type must fit every
// constructor parameter the spread fills; the call is then dispatched on the
// array's length (a length the constructor does not take is a runtime error,
// JS parity). The spread is the call's last argument: the fixed arguments
// before it keep their positions.
std::string Generator::newSpreadText(const ClassModel& cm,
                                     const std::string& self,
                                     const ExprList& arguments,
                                     std::size_t spreadAt) {
    const auto& spread = static_cast<const SpreadElement&>(*arguments[spreadAt]);
    if (!spread.argument) return std::string();
    const Type type = effectiveType(*spread.argument);
    if (type.kind != TypeKind::Array || type.parts.empty()) {
        sink_.error(diagcode::kInvalidArgumentValue, spread.span,
                    "a spread argument must be an array");
        return std::string();
    }
    const CppInfo ec = classify(type.parts.front());
    if (!ec.representable) {
        sink_.error(diagcode::kInvalidArgumentValue, spread.span,
                    "a spread argument's elements must have a C++ spelling");
        return std::string();
    }
    const ClassModel* owner = nullptr;
    const ClassMemberModel* ctor = ctorOf(cm, &owner);
    const ClassModel& decl = owner != nullptr ? *owner : cm;
    std::vector<Type> ptypes;
    std::size_t required = 0;
    if (ctor != nullptr && ctor->ctor != nullptr) {
        for (const auto& p : ctor->ctor->params) {
            if (!p) continue;
            ptypes.push_back(ctorParamType(sema_, *p, decl));
            if (!p->optional && p->defaultValue == nullptr) ++required;
        }
    }
    // The parameters the spread fills (the ones after the fixed arguments).
    const std::size_t fixed = spreadAt;
    for (std::size_t i = fixed; i < ptypes.size(); ++i) {
        const CppInfo pi = classify(ptypes[i]);
        if (!pi.representable || pi.cpp != ec.cpp) {
            sink_.error(diagcode::kTypeMismatch, spread.span,
                        "type mismatch: the spread array's element type does "
                        "not match the constructor's parameter type");
            return std::string();
        }
    }
    const std::size_t fill = ptypes.size() > fixed ? ptypes.size() - fixed : 0;
    const std::size_t needed = required > fixed ? required - fixed : 0;
    // The dispatch: the constructor takes the elements the array holds.
    std::string out = "([&]() -> ::cp3::runtime::Ref<" + self + "> { "
                      "const auto& cp3_a = " + expr(*spread.argument) +
                      "; const std::size_t cp3_n = cp3_a->size();";
    for (std::size_t n = needed; n <= fill; ++n) {
        out += " if (cp3_n == " + std::to_string(n) + ") return "
               "::cp3::runtime::makeRef<" + self + ">(";
        bool first = true;
        for (std::size_t i = 0; i < fixed; ++i) {
            const Expr* arg = arguments[i].get();
            if (!arg) continue;
            if (!first) out += ", ";
            first = false;
            out += expr(*arg);
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (!first) out += ", ";
            first = false;
            out += "cp3_a->at(" + std::to_string(i) + ")";
        }
        // A parameter the (shorter) array did not fill keeps its own default.
        for (std::size_t i = fixed + n; i < ptypes.size(); ++i) {
            if (!first) out += ", ";
            first = false;
            const CppInfo pi = classify(ptypes[i]);
            out += pi.shape == Shape::Optional || pi.shape == Shape::Maybe
                       ? "std::nullopt"
                       : objectFieldDefaultText(ptypes[i]);
        }
        out += ");";
    }
    out += " throw ::cp3::runtime::Thrown::make(std::string(\"wrong number of "
           "arguments to the constructor\")); })()";
    return out;
}


// `{ x: 1 }`: the generated literal/field struct behind a `Ref<T>`, arguments
// in model order (a missing optional field becomes `std::nullopt`).
std::string Generator::objectInitText(const ObjectModel& om,
                                      const ObjectLiteral& lit) {
    needsRef_ = true;
    std::vector<std::string> args;
    args.reserve(om.fields.size());
    for (const ObjectFieldModel& f : om.fields) {
        const Expr* value = literalValueOf(lit, f.name);
        if (value == nullptr) {
            args.push_back(f.optional ? "std::nullopt"
                                      : objectFieldDefaultText(f.type));
            continue;
        }
        if (f.optional && isNullishLiteral(*value)) {
            args.push_back("std::nullopt");
            continue;
        }
        args.push_back(expr(*value));
    }
    const std::string& structName =
        om.literalName.empty() ? om.structName : om.literalName;
    return "::cp3::runtime::makeRef<" + structName + ">(" +
           joinStrings(args, ", ") + ")";
}

std::string Generator::emitModelObjectLiteral(const ObjectLiteral& lit) {
    const std::vector<ObjectFieldModel> fields = literalFields(lit, sema_);
    if (fields.empty()) return std::string();
    const std::string label = objectLabelOf(fields);
    ModelRegistry& reg = ModelRegistry::get();
    const ObjectModel* om = reg.objectByLabel(label);
    if (om == nullptr || !reg.objectUsable(label)) return std::string();
    // A plain all-primitive shape keeps master's value-struct literal path
    // (support/value_shapes.cpp); only a model-owned shape lowers here.
    if (!objectModelUsed(*om)) return std::string();
    return objectInitText(*om, lit);
}


// The constructor's arguments for a class literal: the literal's values,
// matched to the constructor's parameters by name and then - for the
// parameters no name matched - in the literal's property order. A parameter
// the literal does not supply keeps the parameter's own default.
// `let c: C = { x: 1 }`: a class instance built field by field. A class
// **with a constructor** is made with `new`, so an object literal is not one
// of its instances: the sema reports that (CP3401) and the codegen repeats
// it, so a unit that reached codegen without the sema gate still fails.
std::string Generator::classLiteralInit(const ClassModel& cm,
                                        const ObjectLiteral& lit) {
    needsRef_ = true;
    const ClassModel* owner = nullptr;
    const ClassMemberModel* ctor = ctorOf(cm, &owner);
    if (ctor != nullptr && ctor->ctor != nullptr) {
        sink_.error(diagcode::kTypeMismatch, lit.span,
                    "type mismatch: an object literal is not assignable to type '" +
                        cm.name +
                        "': a class with a constructor must be created with `new " +
                        cm.name + "(...)`");
        return std::string();
    }
    std::string args;
    std::string out = "([&]{ auto cp3_v = ::cp3::runtime::makeRef<" + cm.cpp +
                      ">(" + args + ");";
    for (const ClassMemberModel& m : cm.members) {
        if (m.ctor != nullptr || m.isMethod) continue;
        const Expr* value = literalValueOf(lit, m.name);
        if (value == nullptr) continue;
        const std::string field = "cp3_v->" + mapIdent(m.name);
        const bool nullish = isNullishLiteral(*value);
        if (m.isWeak) {
            out += " " + field + (nullish ? ".reset()" : " = " + expr(*value)) +
                   ";";
            continue;
        }
        const CppInfo ci = classify(m.type);
        if (nullish &&
            (ci.shape == Shape::Optional || ci.shape == Shape::Maybe)) {
            out += " " + field + " = std::nullopt;";
            continue;
        }
        out += " " + field + " = " + expr(*value) + ";";
    }
    out += " return cp3_v; }())";
    return out;
}

std::string Generator::objectFieldDefaultText(const Type& t) {
    const CppInfo ci = classify(t);
    if (!ci.representable) return "double{}";
    return ci.cpp + "{}";
}

}  // namespace cp3
