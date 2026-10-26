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
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/class/class_walk.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

// A setter store (`c.value = v`, D21) as an expression: evaluates to the
// assigned value, like an assignment does.
std::string setterStoreText(const std::string& setter,
                            const std::string& value) {
    return "([&]{ auto cp3_v = " + value + "; " + setter +
           "(cp3_v); return cp3_v; }())";
}

// A generic class instantiation spelling (`::ns::Box<double>`); "" when an
// argument has no C++ spelling or the arity is wrong. `error` then names the
// reason.
std::string instanceSpelling(const ClassModel& cm,
                              const std::vector<Type>& args,
                              std::string& error) {
    if (cm.typeParams.empty()) return cm.cpp;
    if (args.size() != cm.typeParams.size()) {
        error = "generic class '" + cm.name + "' expects " +
                std::to_string(cm.typeParams.size()) +
                " type argument(s), got " + std::to_string(args.size());
        return std::string();
    }
    std::string out = cm.cpp + "<";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const CppInfo ci = classify(args[i]);
        if (!ci.representable) {
            error = "the type argument '" + args[i].name + "' of class '" +
                    cm.name + "' has no C++ spelling";
            return std::string();
        }
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
        // A weak store accepts either a strong handle or the optional result
        // of a weak read (`Node | undefined`). An empty optional clears the
        // link; a present one stores its strong handle.
        const CppInfo vci = classify(effectiveType(*a.value));
        const std::string pointee = weakPointee(m.type);
        if ((vci.shape == Shape::Optional || vci.shape == Shape::Maybe) &&
            !pointee.empty()) {
            const bool maybe = vci.shape == Shape::Maybe;
            const std::string has = maybe ? ".hasValue()" : ".has_value()";
            const std::string held = maybe ? "cp3_w.value()" : "(*cp3_w)";
            return "([&]{ const auto& cp3_w = (" + value + "); return (" +
                   lvalue + " = cp3_w" + has +
                   " ? ::cp3::runtime::WeakRef<" + pointee + ">(" + held +
                   ") : ::cp3::runtime::WeakRef<" + pointee + ">()); }())";
        }
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
    std::string error;
    const std::string self = instanceSpelling(*cm, instArgs, error);
    if (self.empty()) {
        // D25: a generic class needs explicit type arguments, and they must
        // match the declaration.
        sink_.error(diagcode::kInvalidTypeArgument, n.span, error);
        return std::string();
    }
    // A spread argument stands for the constructor's arguments, so the call
    // is dispatched on the array's length instead of being spelled out.
    std::size_t spreadAt = n.arguments.size();
    for (std::size_t i = 0; i < n.arguments.size(); ++i) {
        const Expr* arg = n.arguments[i].get();
        if (!arg || arg->kind != NodeKind::SpreadElement) continue;
        spreadAt = i;
        break;
    }
    if (spreadAt < n.arguments.size()) {
        if (spreadAt + 1 != n.arguments.size()) {
            sink_.error(diagcode::kInvalidArgumentValue, n.span,
                        "a spread argument must be the last argument of a "
                        "constructor call");
            return std::string();
        }
        return newSpreadText(*cm, self, n.arguments, spreadAt);
    }
    std::vector<std::string> texts;
    for (const auto& argPtr : n.arguments) {
        const Expr* arg = argPtr.get();
        if (arg == nullptr) continue;
        texts.push_back(expr(*arg));
    }
    // A constructor with a rest parameter takes the trailing arguments as
    // one array (spec 15).
    if (const ClassMemberModel* ctor = findClassMember(*cm, "constructor");
        ctor != nullptr && ctor->ctor != nullptr) {
        texts = classRestArgs(texts, &ctor->ctor->params);
    }
    return "::cp3::runtime::makeRef<" + self + ">(" +
           joinStrings(texts, ", ") + ")";
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
    Type rt = effectiveType(*m.object);
    CppInfo ri = classify(rt);
    const Shape declaredShape = ri.shape;
    bool unwrap = false;
    // Sema narrows a nullish guard (`if (x === undefined) ... else`) for the
    // branch body, but the declaration environment still spells the binding
    // as its optional storage. When the use site's sema type is the non-null
    // member, read through the held handle.
    if ((ri.shape == Shape::Optional || ri.shape == Shape::Maybe) &&
        m.object->kind == NodeKind::Identifier) {
        if (const Type* narrowed = sema_.typeOf(m.object->nodeId);
            narrowed && !narrowed->isUnknown() && !narrowed->isError() &&
            !narrowed->isNullish() && !narrowed->isNullable() &&
            !narrowed->isUnion()) {
            const CppInfo ni = classify(*narrowed);
            if (ni.representable) {
                rt = *narrowed;
                ri = ni;
                unwrap = true;
            }
        }
    }
    if (ri.shape == Shape::Optional || ri.shape == Shape::Maybe) {
        return std::string();  // a narrowed handle is needed first
    }
    std::string recv = expr(*m.object);
    if (unwrap) {
        recv = declaredShape == Shape::Optional ? "(*(" + recv + "))"
                                                : "(" + recv + ").value()";
    }
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
        if (mem == nullptr) return std::string();
        if (mem->isWeak) {
            // A weak read is `T | undefined`: `readWeak` returns the optional
            // strong handle (`MaybeRef<T>`), and narrowing decides the use.
            return "::cp3::runtime::readWeak((" + recv + ")->" +
                   mapIdent(mem->name) + ")";
        }
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
        if (f == nullptr || f->isMethod) return std::string();
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
        if (f == nullptr) return std::string();
        const std::string fn = mapIdent(f->name);
        const std::string args = joinStrings(emitPlainArgs(c), ", ");
        if (f->isFunction && !f->isMethod && !om->isInterface) {
            // A plain object's function-valued field: call the stored
            // `std::function` through its member spelling.
            return "((" + expr(*m.object) + ")->" + fn + ")(" + args + ")";
        }
        if (!f->isMethod) return std::string();
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
    // A plain structural object behind `Ref<struct>`: a field store. An
    // interface wrapper stores through its generated setter thunk, which
    // updates the adapted object's field.
    const ObjectModel* om = objectModelOf(base);
    const bool usable = om != nullptr &&
                        ModelRegistry::get().objectUsable(om->label) &&
                        objectModelUsed(*om);
    const ObjectFieldModel* f =
        usable ? findObjectField(*om, m.property) : nullptr;
    if (f == nullptr || f->isMethod || f->optional ||
        (a.op == AssignOp::Assign && isNullishLiteral(*a.value))) {
        return std::string();
    }
    if (om->isInterface) {
        if (a.op != AssignOp::Assign) return std::string();
        return "(" + expr(*m.object) + ").set_" + mapIdent(f->name) + "(" +
               expr(*a.value) + ")";
    }
    return "((" + expr(*m.object) + ")->" + mapIdent(f->name) + " = " +
           expr(*a.value) + ")";
}

// -----------------------------------------------------------------------------
// Object literals + initializers
// -----------------------------------------------------------------------------

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
