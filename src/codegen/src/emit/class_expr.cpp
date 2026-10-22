// =============================================================================
// src/codegen/src/emit/class_expr.cpp - class / interface / object expressions
// for the spec §15 object surface (STYLE.md §8).
//
// The expression side of the object surface, hooked from the generic emitters
// (emit/expr.cpp, emit/call.cpp, emit/stmt.cpp, emit/decl.cpp). Each entry point
// recognizes one model form and returns its C++ spelling; "" means "not a model
// form", so the caller keeps its own path (usually the CP3002 report). The
// structs these expressions name are emitted by emit/class_decl.cpp (classes)
// and emit/class_shape.cpp (object / interface shapes).
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

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

// A setter store (`c.value = v`, D21) as an expression: evaluates to the
// assigned value, like an assignment does.
std::string setterStoreText(const std::string& setter,
                            const std::string& value) {
    return "([&]{ auto cp3_v = " + value + "; " + setter +
           "(cp3_v); return cp3_v; }())";
}

// A generic class instantiation spelling (`::ns::Box<double>`); "" when an
// argument has no C++ spelling or the arity is wrong.
std::string instanceSpelling(const ClassModel& cm,
                             const std::vector<Type>& args) {
    if (cm.typeParams.empty()) return cm.cpp;
    if (args.size() != cm.typeParams.size()) return std::string();
    std::string out = cm.cpp + "<";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const CppInfo ci = classify(args[i]);
        if (!ci.representable) return std::string();
        if (i != 0) out += ", ";
        out += ci.cpp;
    }
    return out + ">";
}

}  // namespace

// One store of `value` into the lvalue (an instance or static field). A weak
// field stores through `WeakRef::operator=` / `.reset()`; an optional field
// folds a nullish store into `std::nullopt`; `%=` reuses the `%` lowering.
std::string Generator::storeText(const ClassMemberModel& m,
                                 const std::string& lvalue,
                                 const AssignmentExpr& a,
                                 const std::string& value) {
    const bool nullish = isNullishLiteral(*a.value);
    if (m.isWeak && (a.op == AssignOp::Assign)) {
        if (nullish) return "(" + lvalue + ".reset())";
        return "(" + lvalue + " = " + value + ")";
    }
    switch (a.op) {
        case AssignOp::Assign: {
            const CppInfo ci = classify(m.type);
            if (nullish && (ci.shape == Shape::Optional ||
                            ci.shape == Shape::Maybe)) {
                return "(" + lvalue + " = std::nullopt)";
            }
            return "(" + lvalue + " = " + value + ")";
        }
        case AssignOp::Add: return "(" + lvalue + " += " + value + ")";
        case AssignOp::Subtract: return "(" + lvalue + " -= " + value + ")";
        case AssignOp::Multiply: return "(" + lvalue + " *= " + value + ")";
        case AssignOp::Divide: return "(" + lvalue + " /= " + value + ")";
        case AssignOp::Modulo:
            return "(" + lvalue + " = " + modCall(lvalue, *a.value, value) + ")";
        case AssignOp::Power:
            return "(" + lvalue + " = std::pow(" + lvalue + ", " + value + "))";
        default:
            return std::string();
    }
}

// -----------------------------------------------------------------------------
// Construction + member access
// -----------------------------------------------------------------------------

// `new C(...)`: a registered class behind a `Ref<T>` handle. "" when the callee
// is not a class of this unit (the network / family / emitter hooks run first).
std::string Generator::emitModelNew(const NewExpr& n) {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::string();
    const std::string name = static_cast<const Identifier&>(*n.callee).name;
    const ClassModel* cm = ModelRegistry::get().classByName(name);
    if (cm == nullptr) return std::string();
    needsRef_ = true;
    std::vector<Type> instArgs;
    for (const auto& ta : n.typeArgs) {
        if (ta) instArgs.push_back(modelTypeFromTypeNode(*ta));
    }
    const std::string self = instanceSpelling(*cm, instArgs);
    if (self.empty()) {
        notImplemented(n, "this generic class instantiation");
        return std::string();
    }
    std::string args;
    for (const auto& argPtr : n.arguments) {
        const Expr* arg = argPtr.get();
        if (arg == nullptr) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            notImplemented(*arg, "spread argument");
            continue;
        }
        if (!args.empty()) args += ", ";
        args += expr(*arg);
    }
    return "::cp3::runtime::makeRef<" + self + ">(" + args + ")";
}

// A member read: `C.staticField`, `(recv)->field` on a class handle, an
// interface field accessor call, or a detached class method. Weak-reference
// reads stay "" (the caller reports CP3002: narrowing is required first).
std::string Generator::emitModelMember(const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    // D22: `super.field` reads the base subobject.
    if (m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "super" &&
        !classStack_.empty()) {
        const ClassModel* base =
            ModelRegistry::get().classByName(classStack_.back()->baseName);
        if (base == nullptr) return std::string();
        if (const ClassMemberModel* get =
                findClassAccessor(*base, m.property, true);
            get != nullptr) {
            return base->cpp + "::get_" + mapIdent(get->name) + "()";
        }
        const ClassMemberModel* mem =
            findClassMemberChain(*base, m.property, nullptr);
        if (mem == nullptr || mem->isMethod) return std::string();
        return base->cpp + "::" + mapIdent(mem->name);
    }
    const bool viaClassName =
        m.object->kind == NodeKind::Identifier &&
        !hasValueBinding(static_cast<const Identifier&>(*m.object).name);
    if (viaClassName) {
        const std::string cname = static_cast<const Identifier&>(*m.object).name;
        const ClassModel* cm = ModelRegistry::get().classByName(cname);
        if (cm != nullptr) {
            const ClassMemberModel* get =
                findClassAccessor(*cm, m.property, true);
            if (get != nullptr) {
                if (!get->isStatic) return std::string();
                return cm->cpp + "::get_" + mapIdent(get->name) + "()";
            }
            const ClassMemberModel* mem =
                findClassMemberChain(*cm, m.property, nullptr);
            if (mem != nullptr && mem->isStatic && !mem->isMethod) {
                return cm->cpp + "::" + mapIdent(mem->name);
            }
            return std::string();
        }
    }
    const Type rt = effectiveType(*m.object);
    const CppInfo ri = classify(rt);
    if (ri.shape == Shape::Optional || ri.shape == Shape::Maybe) {
        return std::string();  // a narrowed handle is needed first
    }
    const std::string recv = expr(*m.object);
    const Type base = stripNullish(rt);
    if (const ClassModel* cm = classModelOf(base)) {
        // D21: a property read through its getter.
        if (const ClassMemberModel* get =
                findClassAccessor(*cm, m.property, true);
            get != nullptr) {
            const std::string target =
                get->isStatic ? cm->cpp + "::" : "(" + recv + ")->";
            return target + "get_" + mapIdent(get->name) + "()";
        }
        const ClassMemberModel* mem =
            findClassMemberChain(*cm, m.property, nullptr);
        if (mem == nullptr || mem->isWeak) return std::string();
        if (mem->isMethod) return detachedMethodLambda(*cm, *mem, recv);
        // D24: a static field is reachable through an instance too; it lives
        // on the class, not on the handle.
        if (mem->isStatic) return cm->cpp + "::" + mapIdent(mem->name);
        return "(" + recv + ")->" + mapIdent(mem->name);
    }
    if (const ObjectModel* om = objectModelOf(base);
        om != nullptr && ModelRegistry::get().objectUsable(om->label) &&
        objectModelUsed(*om)) {
        const ObjectFieldModel* f = findObjectField(*om, m.property);
        if (f == nullptr || f->isFunction) return std::string();
        if (om->isInterface) return "(" + recv + ")." + mapIdent(f->name) + "()";
        return "(" + recv + ")->" + mapIdent(f->name);
    }
    return std::string();
}


// A method call: `C.staticMethod(...)`, `(recv)->method(...)` on a class
// handle, or an interface method through its wrapper thunk.
std::string Generator::emitModelCall(const CallExpr& c, const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    // D22: `super.m(...)` calls the base implementation, base-qualified so an
    // override in the current class is bypassed (implicit `this`).
    if (m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "super" &&
        !classStack_.empty()) {
        const ClassModel* base =
            ModelRegistry::get().classByName(classStack_.back()->baseName);
        if (base == nullptr) return std::string();
        const ClassMemberModel* mem =
            findClassMemberChain(*base, m.property, nullptr);
        if (mem == nullptr || !mem->isMethod || mem->isStatic) {
            return std::string();
        }
        return base->cpp + "::" + mapIdent(mem->name) + "(" +
               modelCallArgs(c, *mem) + ")";
    }
    const bool viaClassName =
        m.object->kind == NodeKind::Identifier &&
        !hasValueBinding(static_cast<const Identifier&>(*m.object).name);
    if (viaClassName) {
        const std::string cname = static_cast<const Identifier&>(*m.object).name;
        const ClassModel* cm = ModelRegistry::get().classByName(cname);
        if (cm == nullptr) return std::string();
        const ClassMemberModel* mem =
            findClassMemberChain(*cm, m.property, nullptr);
        if (mem == nullptr || !mem->isMethod || !mem->isStatic) {
            return std::string();
        }
        return cm->cpp + "::" + mapIdent(mem->name) + "(" +
               modelCallArgs(c, *mem) + ")";
    }
    const Type rt = effectiveType(*m.object);
    const CppInfo ri = classify(rt);
    if (ri.shape == Shape::Optional || ri.shape == Shape::Maybe) {
        return std::string();
    }
    const Type base = stripNullish(rt);
    if (const ClassModel* cm = classModelOf(base)) {
        const ClassMemberModel* mem =
            findClassMemberChain(*cm, m.property, nullptr);
        if (mem == nullptr || !mem->isMethod || mem->isStatic) {
            return std::string();
        }
        return "(" + expr(*m.object) + ")->" + mapIdent(mem->name) + "(" +
               modelCallArgs(c, *mem) + ")";
    }
    if (const ObjectModel* om = objectModelOf(base);
        om != nullptr && ModelRegistry::get().objectUsable(om->label) &&
        objectModelUsed(*om)) {
        const ObjectFieldModel* f = findObjectField(*om, m.property);
        if (f == nullptr || !f->isMethod) return std::string();
        const std::string fn = mapIdent(f->name);
        const std::string args = joinStrings(emitPlainArgs(c), ", ");
        const std::string rest = args.empty() ? std::string()
                                              : std::string(", ") + args;
        return "([&]{ const auto& cp3_r = (" + expr(*m.object) + "); return cp3_r." +
               "cp3_m_" + fn + "(cp3_r.cp3_obj.get()" + rest + "); }())";
    }
    return std::string();
}

// D22: `super.field = v` writes the base subobject. The caller has already
// established that the receiver is `super` inside a derived body.
std::string Generator::emitSuperMemberAssign(const AssignmentExpr& a,
                                             const MemberExpr& m,
                                             const ClassModel& derived) {
    const ClassModel* base =
        ModelRegistry::get().classByName(derived.baseName);
    if (base == nullptr) return std::string();
    const ClassMemberModel* set = findClassAccessor(*base, m.property, false);
    if (set != nullptr) {
        if (a.op != AssignOp::Assign) return std::string();
        return setterStoreText(base->cpp + "::set_" + mapIdent(set->name),
                               expr(*a.value));
    }
    const ClassMemberModel* mem =
        findClassMemberChain(*base, m.property, nullptr);
    if (mem == nullptr || mem->isMethod || mem->isStatic) {
        return std::string();
    }
    return storeText(*mem, base->cpp + "::" + mapIdent(mem->name), a,
                     expr(*a.value));
}

// A write through a member: instance / static field stores (including the
// compound forms) and weak-reference stores. Interface fields are read-only.
std::string Generator::emitModelAssign(const AssignmentExpr& a,
                                       const MemberExpr& m) {
    if (m.object == nullptr || a.value == nullptr) return std::string();
    if (m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "super") {
        if (classStack_.empty()) return std::string();
        return emitSuperMemberAssign(a, m, *classStack_.back());
    }
    const bool viaClassName =
        m.object->kind == NodeKind::Identifier &&
        !hasValueBinding(static_cast<const Identifier&>(*m.object).name);
    if (viaClassName) {
        const std::string cname = static_cast<const Identifier&>(*m.object).name;
        const ClassModel* cm = ModelRegistry::get().classByName(cname);
        if (cm == nullptr) return std::string();
        const ClassMemberModel* set =
            findClassAccessor(*cm, m.property, false);
        if (set != nullptr) {
            if (!set->isStatic || a.op != AssignOp::Assign) {
                return std::string();
            }
            return setterStoreText(cm->cpp + "::set_" + mapIdent(set->name),
                                   expr(*a.value));
        }
        const ClassMemberModel* mem =
            findClassMemberChain(*cm, m.property, nullptr);
        if (mem == nullptr || !mem->isStatic || mem->isMethod) {
            return std::string();
        }
        return storeText(*mem, cm->cpp + "::" + mapIdent(mem->name), a,
                         expr(*a.value));
    }
    const Type base = stripNullish(effectiveType(*m.object));
    if (const ClassModel* cm = classModelOf(base)) {
        const ClassMemberModel* set =
            findClassAccessor(*cm, m.property, false);
        if (set != nullptr) {
            if (a.op != AssignOp::Assign) return std::string();
            const std::string target =
                set->isStatic ? cm->cpp + "::" : "(" + expr(*m.object) + ")->";
            return setterStoreText(target + "set_" + mapIdent(set->name),
                                   expr(*a.value));
        }
        const ClassMemberModel* mem =
            findClassMemberChain(*cm, m.property, nullptr);
        if (mem == nullptr || mem->isMethod) return std::string();
        // D24: a static field write through an instance targets the class.
        const std::string lvalue =
            mem->isStatic ? cm->cpp + "::" + mapIdent(mem->name)
                          : "(" + expr(*m.object) + ")->" + mapIdent(mem->name);
        return storeText(*mem, lvalue, a, expr(*a.value));
    }
    // A plain structural object behind `Ref<struct>`: a field store.
    const ObjectModel* om = objectModelOf(base);
    const bool usable = om != nullptr && !om->isInterface &&
                        ModelRegistry::get().objectUsable(om->label) &&
                        objectModelUsed(*om);
    const ObjectFieldModel* f =
        usable ? findObjectField(*om, m.property) : nullptr;
    if (f == nullptr || f->isFunction || f->optional ||
        (a.op == AssignOp::Assign && isNullishLiteral(*a.value))) {
        return std::string();
    }
    return "((" + expr(*m.object) + ")->" + mapIdent(f->name) + " = " +
           expr(*a.value) + ")";
}

// -----------------------------------------------------------------------------
// Object literals + initializers
// -----------------------------------------------------------------------------

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

// `let c: C = { x: 1 }`: a class instance built field by field. A class with a
// constructor cannot be built from a literal (construction is the ctor).
std::string Generator::classLiteralInit(const ClassModel& cm,
                                        const ObjectLiteral& lit) {
    if (hasClassCtor(cm)) {
        notImplemented(lit, "an object literal for a class with a constructor");
        return std::string();
    }
    needsRef_ = true;
    std::string out = "([&]{ auto cp3_v = ::cp3::runtime::makeRef<" + cm.cpp +
                      ">();";
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

// The special initializer of a declaration whose declared type is a class and
// whose initializer is an object literal ("" otherwise).
std::string Generator::modelInitText(const VariableDeclarator& d,
                                     const Type& declared) {
    if (!d.init || d.init->kind != NodeKind::ObjectLiteral) {
        return std::string();
    }
    const ClassModel* cm = classModelOf(stripNullish(declared));
    if (cm == nullptr) return std::string();
    return classLiteralInit(*cm, static_cast<const ObjectLiteral&>(*d.init));
}

// A detached method value (`let f = c.getX`): binds the receiver, so a later
// `f(...)` call keeps `this`.
std::string Generator::detachedMethodLambda(const ClassModel& cm,
                                            const ClassMemberModel& m,
                                            const std::string& recv) {
    const std::string ret =
        m.method != nullptr ? methodReturnType(*m.method, &cm) : "void";
    std::vector<std::string> params;
    std::vector<std::string> names;
    if (m.method != nullptr) {
        for (const auto& p : m.method->params) {
            if (!p) continue;
            params.push_back(methodParamText(*p, &cm));
            names.push_back(paramNameOf(*p));
        }
    }
    return "([cp3_r = (" + recv + ")](" + joinStrings(params, ", ") + ") -> " +
           ret + " { return cp3_r->" + mapIdent(m.name) + "(" +
           joinStrings(names, ", ") + "); })";
}

}  // namespace cp3
