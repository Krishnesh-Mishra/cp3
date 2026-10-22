// =============================================================================
// src/codegen/src/emit/class_decl.cpp - class struct + method emission for the
// spec §15 object surface (STYLE.md §8).
//
// The declaration side of the object surface: `collectModels` (driven from
// support/class_collect.cpp) builds the unit's ClassModel/ObjectModel registry
// (detail/class_model.h) before the emit walk; `emitAllClasses` then
// materializes every non-generic class as a C++ struct in `classDecls_` plus
// out-of-line method/constructor definitions in `classDefs_`, base-first so a
// derived struct always sees a complete base. `emitLocalClass` emits a
// function-local class inline (all bodies inside the struct). Structural
// object / interface structs live in emit/class_shape.cpp.
// =============================================================================
#include "detail/generator.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "detail/class_model.h"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/pattern_emitter.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The length of a class's base chain: the emission order key (a base must be
// complete before its derived struct).
int classDepth(const ClassModel& cm) {    int depth = 0;
    const ClassModel* k = &cm;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        if (k->baseName.empty()) break;
        k = ModelRegistry::get().classByName(k->baseName);
        if (k == nullptr) break;
        ++depth;
    }
    return depth;
}

// `template <typename T, typename U>` ("" for a non-generic class).
std::string templateHead(const ClassModel& cm) {
    if (cm.typeParams.empty()) return "";
    std::string out = "template <";
    for (std::size_t i = 0; i < cm.typeParams.size(); ++i) {
        if (i != 0) out += ", ";
        out += "typename " + mapIdent(cm.typeParams[i]);
    }
    return out + ">";
}

// `<T, U>` ("" for a non-generic class).
std::string templateUse(const ClassModel& cm) {
    return templateUseOf(cm);
}

// Does the class (or an ancestor) define a `toString` with a body? The
// per-class `cp3_ts` overload is emitted only when it does (an abstract
// `toString` cannot format a value).
bool hasToString(const ClassModel& cm) {
    const ClassModel* k = &cm;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        for (const ClassMemberModel& m : k->members) {
            if (m.name == "toString" && m.isMethod && m.method != nullptr &&
                m.method->body != nullptr) {
                return true;
            }
        }
        k = k->baseName.empty()
                ? nullptr
                : ModelRegistry::get().classByName(k->baseName);
    }
    return false;
}

}  // namespace

// -----------------------------------------------------------------------------
// Generator: top-level class dispatch + whole-unit struct emission
// -----------------------------------------------------------------------------

void Generator::emitTopLevelClass(const ClassDeclaration& c) {
    // Every class (generic included) emits in emitAllClasses, after the walk,
    // so the whole registry is known.
    (void)c;
}

void Generator::emitAllClasses() {
    ModelRegistry& reg = ModelRegistry::get();
    const std::vector<ClassModel*> all = reg.allClasses();
    // Forward-declare every non-local class: a struct may name another's
    // handle (`Ref<T>`) before the definition, and a pair of classes may
    // reference each other. A generic local class is emitted at the unit's
    // namespace scope too (a C++ local class cannot be a template).
    for (ClassModel* cm : all) {
        if (cm->decl == nullptr) continue;
        if (cm->local && !cm->namespaceScope) continue;
        if (!cm->typeParams.empty()) {
            line(classFwdDecls_, 1, templateHead(*cm));
        }
        line(classFwdDecls_, 1, "struct " + cm->structName + ";");
    }
    if (!classFwdDecls_.empty()) line(classFwdDecls_, 0, "");
    // Base-first: a derived struct must see a complete base type.
    std::vector<ClassModel*> ordered;
    for (ClassModel* cm : all) {
        if (cm->decl == nullptr) continue;
        if (cm->local && !cm->namespaceScope) continue;
        ordered.push_back(cm);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const ClassModel* a, const ClassModel* b) {
                         return classDepth(*a) < classDepth(*b);
                     });
    for (ClassModel* cm : ordered) {
        if (!modelEmitted_.insert(cm->decl->nodeId).second) continue;
        emitClassStruct(*cm, classDecls_, 0, &classDefs_);
    }
}

void Generator::emitLocalClass(const ClassDeclaration& c, std::string& out,
                               int indent) {
    // A local class inside an int64 fast-path attempt would be emitted into
    // two bodies (the fast one and the ordinary fallback): fail the attempt
    // and let the ordinary pass emit the struct once.
    if (intMode_) {
        intFail();
        return;
    }
    const ClassModel* cm = ModelRegistry::get().classByName(c.name);
    if (cm == nullptr || !cm->local) {
        notImplemented(c, "this class declaration");
        return;
    }
    if (cm->namespaceScope) {
        // A generic local class is a namespace-scope template (a C++ local
        // class cannot be a template): emitAllClasses emits its struct.
        return;
    }
    if (!modelEmitted_.insert(c.nodeId).second) return;
    // A generic local class is a C++ local class template: the struct is
    // emitted inline in the body that declares it, with its own type
    // parameters.
    emitClassStruct(*cm, out, indent, nullptr);
}

// -----------------------------------------------------------------------------
// Generator: one class struct (+ its method definitions)
// -----------------------------------------------------------------------------

// The struct (+ the per-class `cp3_ts` overload for a top-level class). A
// non-null `defs` collects out-of-line method/constructor definitions; a null
// one emits every body inline (a function-local class cannot have any).
void Generator::emitClassStruct(const ClassModel& cm, std::string& out,
                                int indent, std::string* defs) {
    needsRef_ = true;
    if (!cm.typeParams.empty()) {
        line(out, indent, templateHead(cm));
    }
    std::string head = "struct " + cm.structName;
    if (!cm.baseName.empty()) {
        const ClassModel* base = ModelRegistry::get().classByName(cm.baseName);
        head += " : public " + (base != nullptr ? base->cpp : mapIdent(cm.baseName));
    }
    line(out, indent, head + " {");
    int current = 0;  // 0 public, 1 protected, 2 private
    for (const ClassMemberModel& m : cm.members) {
        const int want = m.isPrivate ? 2 : (m.isProtected ? 1 : 0);
        if (want != current) {
            line(out, indent + 1,
                 want == 2 ? "private:" : (want == 1 ? "protected:" : "public:"));
            current = want;
        }
        emitClassMember(cm, m, out, indent + 1, defs);
    }
    // The `cp3ToText()` pair makes a class instance formattable: its own
    // `toString` when it defines one, the JS default "[object Object]"
    // otherwise.
    const std::string render =
        hasToString(cm)
            ? "std::string cp3ToText() { return toString(); }"
            : "std::string cp3ToText() { return std::string(\"[object Object]\"); }";
    line(out, indent + 1, render);
    line(out, indent, "};");
    line(out, indent, "");
    if (cm.local && !cm.namespaceScope) return;
    // A generic class's formatter is a template over its own type parameters,
    // so a `Ref<Box<T>>` formats through the class's `toString` (spec §15).
    if (hasToString(cm)) {
        if (cm.typeParams.empty()) {
            line(out, indent, "inline std::string cp3_ts(const " +
                                  classRefCpp(cm) +
                                  "& v) { return v->toString(); }");
        } else {
            const std::string ref = "::cp3::runtime::Ref<" + cm.cpp +
                                    templateUse(cm) + ">";
            line(out, indent, templateHead(cm));
            line(out, indent, "inline std::string cp3_ts(const " + ref +
                                  "& v) { return v->toString(); }");
        }
        line(out, indent, "");
    }
}

void Generator::emitClassMember(const ClassModel& cm,
                                const ClassMemberModel& m, std::string& out,
                                int indent, std::string* defs) {
    if (m.ctor != nullptr) {
        const ConstructorDeclaration& cd = *m.ctor;
        std::vector<std::string> params;
        for (const auto& p : cd.params) {
            if (p) params.push_back(methodParamText(*p, &cm));
        }
        const std::string local = cm.structName;
        const std::string declText =
            local + "(" + joinStrings(params, ", ") + ")";
        if (cd.body == nullptr) {
            line(out, indent, declText + ";");
            return;
        }
        if (defs == nullptr) {
            line(out, indent, declText + " {");
            out += ctorBodyText(cd, cm, indent + 1);
            line(out, indent, "}");
            return;
        }
        line(out, indent, declText + ";");
        const std::string qualifier =
            cm.structName + templateUse(cm) + "::" + cm.structName;
        *defs += (cm.typeParams.empty() ? "" : templateHead(cm) + "\n") +
                 qualifier + "(" + joinStrings(params, ", ") + ") {\n" +
                 ctorBodyText(cd, cm, 1) + "}\n\n";
        return;
    }
    if (!m.isMethod || m.method == nullptr) {
        // D21: an accessor is a generated `get_name`/`set_name` method.
        if (m.accessor != nullptr) {
            emitClassAccessorMember(cm, m, out, indent, defs);
            return;
        }
        line(out, indent, memberFieldText(m) + ";");
        return;
    }
    emitClassMethodMember(cm, m, out, indent, defs);
}

// D21: one get/set accessor becomes a generated `get_name` / `set_name`
// method (inline for a local class, out of line for a namespace class).
void Generator::emitClassAccessorMember(const ClassModel& cm,
                                        const ClassMemberModel& m,
                                        std::string& out, int indent,
                                        std::string* defs) {
    const AccessorDeclaration& ad = *m.accessor;
    const bool getter = ad.accessor == AccessorKind::Get;
    const std::string ret =
        getter ? accessorReturnType(ad, &cm) : std::string("void");
    std::vector<std::string> params;
    if (!getter) {
        for (const auto& p : ad.params) {
            if (p) params.push_back(methodParamText(*p, &cm));
        }
    }
    const std::string name = (getter ? "get_" : "set_") + mapIdent(m.name);
    const std::string declText = (ad.isStatic ? "static " : "") + ret + " " +
                                 name + "(" + joinStrings(params, ", ") + ")";
    if (ad.body == nullptr) {
        line(out, indent, declText + ";");
        return;
    }
    if (defs == nullptr) {
        line(out, indent, declText + " {");
        out += accessorBodyText(ad, cm, indent + 1);
        line(out, indent, "}");
        return;
    }
    line(out, indent, declText + ";");
    *defs += (cm.typeParams.empty() ? "" : templateHead(cm) + "\n") + ret +
             " " + cm.structName + templateUse(cm) + "::" + name + "(" +
             joinStrings(params, ", ") + ") {\n" +
             accessorBodyText(ad, cm, 1) + "}\n\n";
}

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
    const CppInfo ci = classify(m.type);
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
                                        const ClassModel* cm,
                                        const std::vector<std::string>& methodTypeParams) {
    if (md.returnType) {
        if (auto t = typeFromTypeNode(*md.returnType)) {
            if (cm != nullptr) rewriteTypeParams(*t, cm->typeParams);
            rewriteTypeParams(*t, methodTypeParams);
            CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
            notImplemented(md, "this return type");
            return "void";
        }
    }
    if (md.body) {
        if (const Expr* returned = firstReturnedExpr(*md.body)) {
            const CppInfo ci = info(*returned);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
        }
    }
    return "void";
}

std::string Generator::methodParamText(const Parameter& p,
                                       const ClassModel* cm,
                                       const std::vector<std::string>& methodTypeParams) {
    const std::string name = paramNameOf(p);
    if (p.isRest) {
        // `...nums: number[]`: the C++ parameter is one array; the call site
        // packs the trailing arguments into it (emit/class_params.cpp).
        Type t = p.type ? modelTypeFromTypeNode(*p.type) : Type::unknown();
        if (cm != nullptr) rewriteTypeParams(t, cm->typeParams);
        Type elem = Type::unknown();
        if (t.kind == TypeKind::Array && !t.parts.empty()) elem = t.parts.front();
        const CppInfo ec = classify(elem);
        if (!ec.representable || ec.shape == Shape::Void) {
            notImplemented(p, "this rest parameter type");
            return "std::vector<double> " + name;
        }
        return "::cp3::runtime::Array<" + ec.cpp + "> " + name;
    }
    Type t = p.type ? modelTypeFromTypeNode(*p.type) : Type::unknown();
    if (!p.type && p.name) {
        if (const Type* st = sema_.typeOf(p.name->nodeId)) t = *st;
    }
    if (cm != nullptr) rewriteTypeParams(t, cm->typeParams);
    rewriteTypeParams(t, methodTypeParams);
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
    if (ad.returnType) {
        if (auto t = typeFromTypeNode(*ad.returnType)) {
            if (cm != nullptr) rewriteTypeParams(*t, cm->typeParams);
            CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
            notImplemented(ad, "this accessor return type");
            return "void";
        }
    }
    if (ad.body) {
        if (const Expr* returned = firstReturnedExpr(*ad.body)) {
            const CppInfo ci = info(*returned);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
        }
    }
    return "void";
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
    for (const auto& p : ad.params) {
        if (!p) continue;
        if (p->name && p->name->kind == NodeKind::Identifier) {
            Type t = p->type ? modelTypeFromTypeNode(*p->type) : Type::unknown();
            if (p->optional && !t.isUnknown()) t = Type::nullable(t);
            declare(static_cast<const Identifier&>(*p->name).name, t);
        }
    }
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
                                      const std::vector<std::string>& methodTypeParams) {
    const std::string savedReturn = currentReturn_;
    currentReturn_ = methodReturnType(md, &cm, methodTypeParams);
    ++nestingDepth_;
    pushScope();
    classStack_.push_back(&cm);
    if (!md.isStatic) {
        declare("this", Type::named(TypeKind::Class, cm.name));
    }
    for (const auto& p : md.params) {
        if (!p) continue;
        // A rest parameter binds its name through the `...` wrapper.
        const Identifier* id = parameterIdentifier(*p);
        if (id == nullptr) continue;
        Type t = p->type ? modelTypeFromTypeNode(*p->type) : Type::unknown();
        // A method of a generic class, or a generic method, names its own
        // type parameters in the signature.
        rewriteTypeParams(t, cm.typeParams);
        rewriteTypeParams(t, methodTypeParams);
        if (p->optional && !t.isUnknown()) t = Type::nullable(t);
        declare(id->name, t);
    }
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
    for (const auto& p : cd.params) {
        if (!p) continue;
        const Identifier* id = parameterIdentifier(*p);
        if (id == nullptr) continue;
        Type t = p->type ? modelTypeFromTypeNode(*p->type) : Type::unknown();
        declare(id->name, t);
    }
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



// The member helpers the class surface needs: the parameter
// declarations a member body reads, and the argument list a
// model call passes (a rest parameter packs the extra
// arguments into an array).

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
