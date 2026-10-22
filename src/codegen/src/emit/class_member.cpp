// =============================================================================
// src/codegen/src/emit/class_member.cpp - class member spelling: the field,
// method and accessor types, their bodies, and a member call's arguments
// (STYLE.md section 8).
//
// The declaration side of a class member lives in emit/class_decl.cpp (the
// struct and its members); this unit owns the per-member spellings: the C++
// type of a field / method return / accessor return, the parameter list, the
// body prologue, and the argument vector a method call passes.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/class_model.h"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/pattern_emitter.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

// -----------------------------------------------------------------------------
// Generator: member/method spelling helpers
// -----------------------------------------------------------------------------

std::string Generator::memberFieldText(const ClassMemberModel& m) {
    const std::string name = mapIdent(m.name);
    const std::string stat = m.isStatic ? "static inline " : "";
    if (m.isWeak) {
        const std::string pointee = weakPointee(m.type);
        if (pointee.empty()) {
            if (m.field) notImplemented(*m.field, "this weak field type");
            return stat + "double " + name + "{}";
        }
        return stat + "::cp3::runtime::WeakRef<" + pointee + "> " + name + "{}";
    }
    CppInfo ci = classify(m.type);
    if (!ci.representable && m.hasInitializer && m.field &&
        m.field->initializer) {
        // The declared type has no C++ spelling: the field's own initializer
        // decides, exactly as it does for an unannotated field (the declared
        // type is the permissive one, so the value is the only spelling).
        const CppInfo ic = info(*m.field->initializer);
        if (ic.representable && ic.shape != Shape::Void) ci = ic;
    }
    if (!ci.representable) {
        if (m.field) notImplemented(*m.field, "this field type");
        return stat + "double " + name + "{}";
    }
    if (m.hasInitializer && m.field && m.field->initializer) {
        if (ci.shape == Shape::Optional &&
            isNullishLiteral(*m.field->initializer)) {
            return stat + ci.cpp + " " + name + " = std::nullopt";
        }
        return stat + ci.cpp + " " + name + " = " + expr(*m.field->initializer);
    }
    return stat + ci.cpp + " " + name + "{}";
}

std::string Generator::methodReturnType(const MethodDeclaration& md,
                                        const ClassModel* cm) {
    // The candidate chain the top-level function uses (decl.cpp's
    // returnValueType): the annotation first (sema's resolved type wins, so a
    // nominal annotation sema resolved to a structural label lowers by its
    // shape), then the body's own first returned value. The first candidate
    // with a C++ spelling wins.
    std::optional<Type> annotated;
    if (md.returnType) {
        if (auto t = typeOfNode(*md.returnType)) {
            if (cm != nullptr) rewriteTypeParams(*t, cm->typeParams);
            annotated = *t;
            const CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
        }
    }
    // The body's value decides when the annotation has no spelling of its own
    // (the same rule an unannotated method follows).
    if (md.body) {
        if (const Expr* returned = firstReturnedExpr(*md.body)) {
            const CppInfo ci = info(*returned);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
        }
    }
    if (annotated && !annotated->isUnknown()) {
        notImplemented(md, "this return type");
    }
    return "void";
}

std::string Generator::methodParamText(const Parameter& p,
                                       const ClassModel* cm) {
    const std::string name = paramNameOf(p);
    if (p.isRest) {
        // A rest parameter: the C++ parameter is one array, and the call site
        // packs the trailing arguments into it (the same shape the top-level
        // function's rest parameter uses, emit/patterns.cpp).
        Type t = Type::unknown();
        if (p.type) t = modelTypeFromTypeNode(*p.type);
        if (cm != nullptr) rewriteTypeParams(t, cm->typeParams);
        std::string elem;
        if (t.kind == TypeKind::Array && !t.parts.empty()) {
            const CppInfo ec = classify(t.parts.front());
            if (ec.representable) elem = ec.cpp;
        }
        if (elem.empty()) {
            notImplemented(p, "this rest parameter type");
            return "/* rest */";
        }
        needsTypedArray_ = true;
        return "::cp3::runtime::Array<" + elem + "> " + name;
    }
    Type t = Type::unknown();
    if (p.type) {
        t = modelTypeFromTypeNode(*p.type);
    } else if (p.name) {
        if (const Type* st = sema_.typeOf(p.name->nodeId)) t = *st;
    }
    if (cm != nullptr) rewriteTypeParams(t, cm->typeParams);
    if (p.optional && !t.isUnknown()) t = Type::nullable(t);
    const CppInfo ci = classify(t);
    if (!ci.representable) {
        notImplemented(p, "untyped parameter");
        return "double " + name;
    }
    return ci.cpp + " " + name;
}

std::string Generator::accessorReturnType(const AccessorDeclaration& ad,
                                          const ClassModel* cm) {
    // The same candidate chain the method return type uses: the annotation
    // first, then the getter's own first returned value.
    std::optional<Type> annotated;
    if (ad.returnType) {
        if (auto t = typeOfNode(*ad.returnType)) {
            if (cm != nullptr) rewriteTypeParams(*t, cm->typeParams);
            annotated = *t;
            const CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
        }
    }
    if (ad.body) {
        if (const Expr* returned = firstReturnedExpr(*ad.body)) {
            const CppInfo ci = info(*returned);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
        }
    }
    if (annotated && !annotated->isUnknown()) {
        notImplemented(ad, "this accessor return type");
    }
    return "void";
}

// The bindings a member's parameter list introduces in its body. A rest
// parameter binds the whole array (its `...` wraps the name in a
// RestElement); every other parameter binds its own name.
void Generator::declareMemberParams(const ParamList& params) {
    for (const auto& p : params) {
        if (!p) continue;
        const Identifier* id = parameterIdentifier(*p);
        if (id == nullptr) continue;
        Type t = p->type ? modelTypeFromTypeNode(*p->type) : Type::unknown();
        if (p->optional && !p->isRest && !t.isUnknown()) t = Type::nullable(t);
        declare(id->name, t);
    }
}

std::string Generator::accessorBodyText(const AccessorDeclaration& ad,
                                        const ClassModel& cm, int indent) {
    const std::string savedReturn = currentReturn_;
    currentReturn_ =
        ad.accessor == AccessorKind::Get ? accessorReturnType(ad, &cm) : "void";
    ++nestingDepth_;
    pushScope();
    classStack_.push_back(&cm);
    if (!ad.isStatic) {
        declare("this", Type::named(TypeKind::Class, cm.name));
    }
    declareMemberParams(ad.params);
    std::string body;
    if (ad.body) stmt(*ad.body, body, indent);
    classStack_.pop_back();
    popScope();
    --nestingDepth_;
    currentReturn_ = savedReturn;
    return body;
}

std::string Generator::methodBodyText(const MethodDeclaration& md,
                                      const ClassModel& cm, int indent,
                                      bool isStatic) {
    const std::string savedReturn = currentReturn_;
    currentReturn_ = methodReturnType(md, &cm);
    ++nestingDepth_;
    pushScope();
    classStack_.push_back(&cm);
    if (!isStatic) {
        declare("this", Type::named(TypeKind::Class, cm.name));
    }
    declareMemberParams(md.params);
    std::string body;
    if (md.body) stmt(*md.body, body, indent);
    classStack_.pop_back();
    popScope();
    --nestingDepth_;
    currentReturn_ = savedReturn;
    return body;
}

std::string Generator::ctorBodyText(const ConstructorDeclaration& cd,
                                    const ClassModel& cm, int indent) {
    const std::string savedReturn = currentReturn_;
    currentReturn_ = "void";
    ++nestingDepth_;
    pushScope();
    classStack_.push_back(&cm);
    declare("this", Type::named(TypeKind::Class, cm.name));
    declareMemberParams(cd.params);
    std::string body;
    if (cd.body) stmt(*cd.body, body, indent);
    classStack_.pop_back();
    popScope();
    --nestingDepth_;
    currentReturn_ = savedReturn;
    return body;
}

std::string Generator::weakPointee(const Type& fieldType) {
    const CppInfo ci = classify(stripNullish(fieldType));
    const std::string prefix = "::cp3::runtime::Ref<";
    if (ci.shape != Shape::ClassRef ||
        ci.cpp.compare(0, prefix.size(), prefix) != 0) {
        return std::string();
    }
    return ci.cpp.substr(prefix.size(), ci.cpp.size() - prefix.size() - 1);
}


// The arguments of a class method call. A method with a rest parameter takes
// the trailing arguments packed into the one array it declares.
std::string Generator::modelCallArgs(const CallExpr& c,
                                     const ClassMemberModel& m) {
    const MethodDeclaration* md = m.method;
    std::size_t restAt = 0;
    bool hasRest = false;
    if (md != nullptr) {
        for (std::size_t i = 0; i < md->params.size(); ++i) {
            const Parameter* p = md->params[i].get();
            if (p && p->isRest) {
                restAt = i;
                hasRest = true;
                break;
            }
        }
    }
    const std::vector<std::string> args = emitPlainArgs(c);
    if (!hasRest) return joinStrings(args, ", ");
    // The element type of the rest parameter (`E` of its `E[]`).
    std::string elem = "double";
    if (md != nullptr && restAt < md->params.size()) {
        const Parameter* rp = md->params[restAt].get();
        if (rp && rp->type) {
            const Type t = modelTypeFromTypeNode(*rp->type);
            if (t.kind == TypeKind::Array && !t.parts.empty()) {
                const CppInfo ec = classify(t.parts.front());
                if (ec.representable) elem = ec.cpp;
            }
        }
    }
    needsTypedArray_ = true;
    std::vector<std::string> out;
    for (std::size_t i = 0; i < restAt && i < args.size(); ++i) {
        out.push_back(args[i]);
    }
    std::vector<std::string> tail;
    for (std::size_t i = restAt; i < args.size(); ++i) tail.push_back(args[i]);
    out.push_back("::cp3::runtime::makeArray<" + elem + ">(std::vector<" +
                  elem + ">{" + joinStrings(tail, ", ") + "})");
    return joinStrings(out, ", ");
}

}  // namespace cp3
