// =============================================================================
// src/codegen/src/emit/class_expr.cpp - class / interface / object expressions
// for the spec §15 object surface (STYLE.md §8).
//
// The expression side of the object surface, hooked from the generic emitters
// (emit/expr.cpp, emit/call.cpp, emit/stmt.cpp, emit/decl.cpp). Each entry point
// recognizes one model form and returns its C++ spelling; "" means "not a model
// form", so the caller keeps its own path (usually its unsupported-form
// diagnostic). The
// structs these expressions name are emitted by emit/class_decl.cpp (classes)
// and emit/class_shape.cpp (object / interface shapes).
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/class/class_walk.h"
#include "emit/class/auto_weak.h"
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

// A detached interface method value (`let f = shape.area`): binds the wrapper
// and calls the generated `cp3_m_<name>` thunk with its adapted object.
std::string detachedInterfaceMethodLambda(const ObjectFieldModel& f,
                                          const std::string& recv) {
    std::string ret = "void";
    if (!f.type.parts.empty()) {
        const CppInfo rc = classify(f.type.parts.back());
        if (rc.representable) ret = rc.cpp;
    }
    std::vector<std::string> params;
    std::vector<std::string> names;
    for (std::size_t i = 0; i + 1 < f.type.parts.size(); ++i) {
        const CppInfo pc = classify(f.type.parts[i]);
        const std::string pname = "cp3_a" + std::to_string(i);
        params.push_back((pc.representable ? pc.cpp : std::string("auto")) +
                         " " + pname);
        names.push_back(pname);
    }
    const std::string rest =
        names.empty() ? std::string() : ", " + joinStrings(names, ", ");
    return "([cp3_r = (" + recv + ")](" + joinStrings(params, ", ") +
           ") -> " + ret + " { return cp3_r.cp3_m_" + mapIdent(f.name) +
           "(cp3_r.cp3_obj.get()" + rest + "); })";
}

// Pack the trailing call arguments into one array for a rest parameter.
std::vector<std::string> packRestArgs(const std::vector<std::string>& args,
                                      std::size_t restAt,
                                      const std::string& elem) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < restAt && i < args.size(); ++i) {
        out.push_back(args[i]);
    }
    std::vector<std::string> tail;
    for (std::size_t i = restAt; i < args.size(); ++i) tail.push_back(args[i]);
    out.push_back("::cp3::runtime::makeArray<" + elem + ">(std::vector<" +
                  elem + ">{" + joinStrings(tail, ", ") + "})");
    return out;
}

}  // namespace

namespace {

// Stage 2 auto-weak (see emit/class_decl.cpp): storeText(m, ...) has no
// owner class, so its callers push the declaring class name for the call.
std::vector<std::string> g_autoWeakStoreOwnerStack;

bool storeIsAutoWeak(const SemaResult& sema, const ClassMemberModel& m) {
    if (g_autoWeakStoreOwnerStack.empty()) return false;
    const ClassModel* cm =
        ModelRegistry::get().classByName(g_autoWeakStoreOwnerStack.back());
    return cm != nullptr && isAutoWeakField(sema, *cm, m.name);
}

}  // namespace

// One store of `value` into the lvalue (an instance or static field). A weak
// field stores through `WeakRef::operator=` / `.reset()`; an optional field
// folds a nullish store into `std::nullopt`; `%=` reuses the `%` lowering.
std::string Generator::storeText(const ClassMemberModel& m,
                                 const std::string& lvalue,
                                 const AssignmentExpr& a,
                                 const std::string& value) {
    // The model type comes from the structural annotation re-parse, which
    // cannot see aliases; fall back to sema's resolved annotation type when the
    // model type has no spelling (a weak field alias needs the pointee).
    Type memberType = m.type;
    if (m.field && m.field->type && !classify(memberType).representable) {
        if (auto resolved = typeOfNode(*m.field->type)) memberType = *resolved;
    }
    const bool nullish = isNullishLiteral(*a.value);
    // A proven auto-weak field stores through WeakRef exactly like a
    // manual `weak` field (see memberFieldText in class_decl.cpp).
    if ((m.isWeak || storeIsAutoWeak(sema_, m)) &&
        (a.op == AssignOp::Assign)) {
        if (nullish) return "(" + lvalue + ".reset())";
        // A weak store accepts either a strong handle or the optional result
        // of a weak read (`Node | undefined`). An empty optional clears the
        // link; a present one stores its strong handle.
        const CppInfo vci = classify(effectiveType(*a.value));
        const std::string pointee = weakPointee(memberType);
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
            const CppInfo ci = classify(memberType);
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

// `new C(...)` / `Error(...)`: the built-in Error hierarchy has a fixed
// constructor surface. The options object is a compile-time shape, so its
// `cause` property is lowered to the runtime constructor's cause argument.
std::string Generator::emitErrorCtor(const ExprList& args, const ClassModel& cm,
                                     const SourceSpan& span) {
    auto propertyValue = [](const ObjectLiteral& lit,
                            const std::string& wanted) -> const Expr* {
        for (const auto& p : lit.properties) {
            if (!p || p->computed) continue;
            if (keyNameOf(p->key.get()) != wanted) continue;
            return p->value ? p->value.get() : p->key.get();
        }
        return nullptr;
    };
    auto causeText = [&](const Expr* options) -> std::string {
        if (options == nullptr ||
            options->kind == NodeKind::UndefinedLiteral ||
            options->kind == NodeKind::NullLiteral) {
            return "nullptr";
        }
        if (options->kind != NodeKind::ObjectLiteral) {
            sink_.error(diagcode::kTypeMismatch, options->span,
                        "the Error options argument must be an object literal "
                        "with an optional `cause` property");
            return "nullptr";
        }
        const auto& lit = static_cast<const ObjectLiteral&>(*options);
        const Expr* cause = propertyValue(lit, "cause");
        if (cause == nullptr || cause->kind == NodeKind::UndefinedLiteral ||
            cause->kind == NodeKind::NullLiteral) {
            return "nullptr";
        }
        return expr(*cause);
    };

    const bool aggregate = cm.name == "AggregateError";
    std::vector<std::string> texts;
    if (aggregate) {
        if (!args.empty() && args[0]) {
            texts.push_back(expr(*args[0]));
        } else {
            texts.push_back(
                "std::vector<::cp3::runtime::Ref<::cp3::Error>>()");
        }
        if (args.size() > 1 && args[1]) {
            texts.push_back(expr(*args[1]));
        } else {
            texts.push_back("std::string()");
        }
        if (args.size() > 2) texts.push_back(causeText(args[2].get()));
    } else {
        if (!args.empty() && args[0]) {
            texts.push_back(expr(*args[0]));
        } else {
            texts.push_back("std::string()");
        }
        if (args.size() > 1) texts.push_back(causeText(args[1].get()));
    }
    (void)span;
    return "::cp3::runtime::makeRef<" + cm.cpp + ">(" +
           joinStrings(texts, ", ") + ")";
}

std::string Generator::emitErrorCall(const CallExpr& c,
                                     const ClassModel& cm) {
    return emitErrorCtor(c.arguments, cm, c.span);
}

// `new C(...)`: a registered class behind a `Ref<T>` handle. "" when the callee
// is not a class of this unit (the network / family / emitter hooks run first).
std::string Generator::emitModelNew(const NewExpr& n) {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::string();
    const std::string name = static_cast<const Identifier&>(*n.callee).name;
    const ClassModel* cm = ModelRegistry::get().classByName(name);
    if (cm == nullptr) return std::string();
    needsRef_ = true;
    if (cm->builtin) return emitErrorCtor(n.arguments, *cm, n.span);
    std::vector<Type> instArgs;
    if (cm->decl) {
        for (const auto& ta : n.typeArgs) {
            if (ta) instArgs.push_back(modelTypeFromTypeNode(*ta));
        }
        // A bare `new Box(...)` fills from the declared parameter defaults
        // (`<T = U>`), so the struct spelling is stable instead of rejected.
        for (std::size_t i = instArgs.size(); i < cm->decl->typeParams.size();
             ++i) {
            const auto& tp = cm->decl->typeParams[i];
            if (tp && tp->defaultType) {
                instArgs.push_back(modelTypeFromTypeNode(*tp->defaultType));
            }
        }
    } else {
        for (const auto& ta : n.typeArgs) {
            if (ta) instArgs.push_back(modelTypeFromTypeNode(*ta));
        }
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
    // The constructor's parameters (for optional-argument lowering below).
    const ClassModel* ctorOwner = nullptr;
    const ClassMemberModel* ctor = findClassCtor(*cm, &ctorOwner);
    std::vector<std::string> texts;
    for (std::size_t ai = 0; ai < n.arguments.size(); ++ai) {
        const Expr* arg = n.arguments[ai].get();
        if (arg == nullptr) continue;
        // A nullish literal for an optional parameter must lower to
        // `std::nullopt`: a bare `nullptr` would construct an
        // `optional<Ref<T>>` *containing* a null pointer (has_value() true).
        bool nullopt = false;
        if ((arg->kind == NodeKind::NullLiteral ||
             arg->kind == NodeKind::UndefinedLiteral) &&
            ctor != nullptr && ctor->ctor != nullptr &&
            ai < ctor->ctor->params.size()) {
            const Parameter* p = ctor->ctor->params[ai].get();
            Type pt = Type::unknown();
            if (p && p->type) {
                if (auto resolved = typeOfNode(*p->type)) pt = *resolved;
            } else if (p && p->name) {
                if (const Type* st = sema_.typeOf(p->name->nodeId)) pt = *st;
            }
            rewriteTypeParams(pt, cm->typeParams);
            if (ctorOwner != nullptr) rewriteTypeParams(pt, ctorOwner->typeParams);
            const CppInfo pc = classify(pt);
            // `Maybe<T>` (`T | null | undefined`) keeps its flavored
            // spellings instead (see emit/expr/expr.cpp).
            if (pc.shape == Shape::Optional) {
                nullopt = true;
            }
        }
        texts.push_back(nullopt ? "std::nullopt" : expr(*arg));
    }
    // A constructor with a rest parameter takes the trailing arguments as
    // one array (spec 15). Resolve the annotation through sema so an alias of
    // an array type packs the same way a spelled-out `E[]` does. A derived
    // class without its own constructor uses the nearest base's parameters.
    if (ctor != nullptr && ctor->ctor != nullptr) {
        const ParamList& params = ctor->ctor->params;
        std::size_t restAt = params.size();
        for (std::size_t i = 0; i < params.size(); ++i) {
            const Parameter* p = params[i].get();
            if (p && p->isRest) {
                restAt = i;
                break;
            }
        }
        if (restAt < params.size()) {
            const Parameter* rp = params[restAt].get();
            Type t = Type::unknown();
            if (rp && rp->type) {
                if (auto resolved = typeOfNode(*rp->type)) t = *resolved;
            } else if (rp && rp->name) {
                if (const Type* st = sema_.typeOf(rp->name->nodeId)) t = *st;
            }
            rewriteTypeParams(t, cm->typeParams);
            if (ctorOwner != nullptr) {
                rewriteTypeParams(t, ctorOwner->typeParams);
            }
            Type elem = Type::unknown();
            if (t.kind == TypeKind::Array && !t.parts.empty()) {
                elem = t.parts.front();
            }
            const CppInfo ec = classify(elem);
            texts = packRestArgs(
                texts, restAt,
                ec.representable && ec.shape != Shape::Void ? ec.cpp : "double");
        }
    }
    const std::string factory =
        cm->localRef ? "makeLocalTypedRef" : "makeTypedRef";
    std::string out = "::cp3::runtime::" + factory + "<" + self + ">(" +
                      std::to_string(cm->typeId) + "u";
    if (!texts.empty()) out += ", " + joinStrings(texts, ", ");
    out += ")";
    return out;
}

// A member read: `C.staticField`, `(recv)->field` on a class handle, an
// interface field accessor call, or a detached class/interface method.
// Weak-reference reads stay "" (the caller reports that narrowing is required
// first).
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
    Type rt = m.object ? effectiveType(*m.object) : Type::unknown();
    // The Narrowed type from sema (for `n.next` inside a null guard) wins
    // over the re-derived shape: `n.next` is `optional<Ref<Node>>` in C++
    // but the narrowed `Node` type means "read through the handle".
    // `declaredInfo` must reflect the declared storage shape, not the
    // narrowed shape `effectiveType` returns: for a member receiver, look
    // the field up on its base class model.
    CppInfo declaredInfo = classify(rt);
    if (m.object && m.object->kind == NodeKind::MemberExpr) {
        const auto& rm = static_cast<const MemberExpr&>(*m.object);
        if (rm.object) {
            const Type baseT = effectiveType(*rm.object);
            if (const ClassModel* cm2 = classModelOf(stripNullish(baseT))) {
                if (const ClassMemberModel* mem2 =
                        findClassMemberChain(*cm2, rm.property, nullptr)) {
                    declaredInfo = classify(mem2->type);
                }
            }
        }
    }
    bool needUnwrap = false;
    if (const Type* narrowed = sema_.typeOf(m.object->nodeId)) {
        if (!narrowed->isUnknown() && !narrowed->isError() &&
            !narrowed->isNullish() && !narrowed->isNullable() &&
            !narrowed->isUnion()) {
            if (declaredInfo.shape == Shape::Optional ||
                declaredInfo.shape == Shape::Maybe) {
                needUnwrap = true;
            }
            rt = *narrowed;
        }
    }
    CppInfo ri = classify(rt);
    bool unwrap = needUnwrap;
    // Sema narrows a nullish guard (`if (x === undefined) ... else`) for the
    // branch body, but the declaration environment still spells the binding
    // as its optional storage. When the use site's sema type is the non-null
    // member, read through the held handle.
    if (ri.shape == Shape::Optional || ri.shape == Shape::Maybe) {
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
    bool nonOwningReceiver = false;
    if (m.object->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*m.object);
        const auto resolved = sema_.resolvedBindings.find(id.nodeId);
        nonOwningReceiver = resolved != sema_.resolvedBindings.end() &&
            nonOwningParamDecls_.count(resolved->second) != 0;
    } else if (m.object->kind == NodeKind::IndexExpr) {
        nonOwningReceiver = isStackValueArray(*m.object);
    }
    const std::string instanceOp = nonOwningReceiver ? "." : "->";
    if (unwrap) {
        recv = declaredInfo.shape == Shape::Optional ? "(*(" + recv + "))"
                                                     : "(" + recv + ").value()";
    }
    const Type base = stripNullish(rt);
    if (const ClassModel* cm = classModelOf(base)) {
        // D21: a property read through its getter.
        if (const ClassMemberModel* get =
                findClassAccessor(*cm, m.property, true);
            get != nullptr) {
            const std::string target =
                get->isStatic ? cm->cpp + "::" : "(" + recv + ")" + instanceOp;
            return target + "get_" + mapIdent(get->name) + "()";
        }
        const ClassMemberModel* mem =
            findClassMemberChain(*cm, m.property, nullptr);
        if (mem == nullptr) return std::string();
        // A weak (manual or proven auto-weak) read is `T | undefined`:
        // `readWeak` returns the optional strong handle (`MaybeRef<T>`),
        // and narrowing decides the use.
        if (mem->isWeak || isAutoWeakField(sema_, *cm, mem->name)) {
            return "::cp3::runtime::readWeak((" + recv + ")" + instanceOp +
                   mapIdent(mem->name) + ")";
        }
        if (mem->isMethod) return detachedMethodLambda(*cm, *mem, recv);
        // D24: a static field is reachable through an instance too; it lives
        // on the class, not on the handle.
        if (mem->isStatic) return cm->cpp + "::" + mapIdent(mem->name);
        return "(" + recv + ")" + instanceOp + mapIdent(mem->name);
    }
    if (const ObjectModel* om = objectModelOf(base);
        om != nullptr && ModelRegistry::get().objectUsable(om->label) &&
        objectModelUsed(*om)) {
        const ObjectFieldModel* f = findObjectField(*om, m.property);
        if (f == nullptr) return std::string();
        if (om->isInterface) {
            if (f->isMethod) return detachedInterfaceMethodLambda(*f, recv);
            return "(" + recv + ")." + mapIdent(f->name) + "()";
        }
        if (f->isMethod) return std::string();
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
        bool nonOwningReceiver = false;
        if (m.object->kind == NodeKind::Identifier) {
            const auto& id = static_cast<const Identifier&>(*m.object);
            const auto resolved = sema_.resolvedBindings.find(id.nodeId);
            nonOwningReceiver = resolved != sema_.resolvedBindings.end() &&
                nonOwningParamDecls_.count(resolved->second) != 0;
        } else if (m.object->kind == NodeKind::IndexExpr) {
            nonOwningReceiver = isStackValueArray(*m.object);
        }
        const std::string instanceOp = nonOwningReceiver ? "." : "->";
        const ClassMemberModel* mem =
            findClassMemberChain(*cm, m.property, nullptr);
        if (mem == nullptr || mem->isStatic) return std::string();
        if (!mem->isMethod) {
            // A class function-valued field is stored as a `std::function`:
            // call the stored value directly.
            Type memberType = mem->type;
            if (mem->field && mem->field->type &&
                !classify(memberType).representable) {
                if (auto resolved = typeOfNode(*mem->field->type)) {
                    memberType = *resolved;
                }
            }
            if (classify(memberType).shape != Shape::Function) {
                return std::string();
            }
            return "((" + expr(*m.object) + ")" + instanceOp + mapIdent(mem->name) +
                   ")(" + joinStrings(emitPlainArgs(c), ", ") + ")";
        }
        return "(" + expr(*m.object) + ")" + instanceOp + mapIdent(mem->name) + "(" +
               modelCallArgs(c, *mem) + ")";
    }
    if (const ObjectModel* om = objectModelOf(base);
        om != nullptr && ModelRegistry::get().objectUsable(om->label) &&
        objectModelUsed(*om)) {
        const ObjectFieldModel* f = findObjectField(*om, m.property);
        if (f == nullptr) return std::string();
        const std::string fn = mapIdent(f->name);
        const std::string args = joinStrings(emitPlainArgs(c), ", ");
        if (f->isFunction && !f->isMethod) {
            // A function-valued field: call the stored `std::function`
            // directly. On an interface wrapper the value lives behind the
            // generated accessor, so the call goes through `name()`.
            if (om->isInterface) {
                return "((" + expr(*m.object) + ")." + fn + "())(" + args +
                       ")";
            }
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
    g_autoWeakStoreOwnerStack.push_back(base->name);
    const std::string superStore =
        storeText(*mem, base->cpp + "::" + mapIdent(mem->name), a,
                  expr(*a.value));
    g_autoWeakStoreOwnerStack.pop_back();
    return superStore;
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
        g_autoWeakStoreOwnerStack.push_back(cm->name);
        const std::string staticStore =
            storeText(*mem, cm->cpp + "::" + mapIdent(mem->name), a,
                      expr(*a.value));
        g_autoWeakStoreOwnerStack.pop_back();
        return staticStore;
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
        g_autoWeakStoreOwnerStack.push_back(cm->name);
        const std::string fieldStore = storeText(*mem, lvalue, a, expr(*a.value));
        g_autoWeakStoreOwnerStack.pop_back();
        return fieldStore;
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
