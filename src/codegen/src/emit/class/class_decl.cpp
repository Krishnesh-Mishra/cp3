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
#include "detail/generator/generator.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/class/class_model.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// Sema's permissive "not statically known" marker (see
// support/type/type_resolve.cpp): an untyped parameter carries `$ambient`, not
// `unknown`, but on the C++ side both mean "let the call site deduce it".
bool isAmbientType(const Type& t) {
    return t.kind == TypeKind::Object && t.name == "$ambient";
}

Type untypedAsUnknown(Type t) {
    if (isAmbientType(t)) return Type::unknown();
    for (Type& part : t.parts) part = untypedAsUnknown(part);
    // `x?: <untyped>` becomes `nullable(unknown)`: it is still an untyped
    // parameter, not an optional of a representable type.
    if (t.kind == TypeKind::Nullable && t.parts.size() == 1 &&
        t.parts.front().isUnknown()) {
        return Type::unknown();
    }
    return t;
}

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
    if (cm == nullptr) {
        // `collectModels` registers every class declaration the emit walk can
        // reach; a missing model means the collection and emit walks disagree.
        reportCodegenError(
            c, diagcode::kInternalError,
            "internal error: no class model for the local class '" + c.name +
                "'");
        return;
    }
    if (!cm->local) {
        // A namespace-scope class is emitted by `emitAllClasses`; reaching it
        // through the statement path must not emit a second struct.
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
    // A derived class with no constructor of its own has the base's
    // constructors: TypeScript's implicit `constructor(...args) {
    // super(...args); }` forwards every call. C++ inherited constructors give
    // the same surface, including the default initialization of this class's
    // fields (`Base(args)` is `makeRef<Derived>(args)`).
    const ClassModel* base =
        cm.baseName.empty() ? nullptr
                            : ModelRegistry::get().classByName(cm.baseName);
    // The first non-builtin class in an inheritance chain owns the runtime type
    // stamp. Every `new` site overwrites it with the concrete class id.
    if (base == nullptr || base->builtin) {
        line(out, indent + 1,
             "unsigned int cp3_type_id = " + std::to_string(cm.typeId) + "u;");
    }
    bool ownCtor = false;
    for (const ClassMemberModel& m : cm.members) {
        if (m.ctor != nullptr) {
            ownCtor = true;
            break;
        }
    }
    if (!ownCtor && base != nullptr && hasClassCtor(*base)) {
        line(out, indent + 1,
             "using " + base->structName + "::" + base->structName + ";");
    }
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
    if (m.staticBlock != nullptr) {
        const StaticBlock& sb = *m.staticBlock;
        const std::string name =
            "cp3_static_block_" + std::to_string(sb.nodeId);
        line(out, indent, "inline static bool " + name + " = []() {");
        const std::string savedReturn = currentReturn_;
        currentReturn_ = "void";
        ++nestingDepth_;
        classStack_.push_back(&cm);
        pushScope();
        std::string body;
        if (sb.body) stmt(*sb.body, body, indent + 1);
        popScope();
        classStack_.pop_back();
        --nestingDepth_;
        currentReturn_ = savedReturn;
        out += body;
        line(out, indent + 1, "return true;");
        line(out, indent, "}();");
        return;
    }
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
        const std::string baseInit = ctorBaseInitText(cd, cm);
        if (defs == nullptr) {
            line(out, indent, declText + baseInit + " {");
            out += ctorBodyText(cd, cm, indent + 1);
            line(out, indent, "}");
            return;
        }
        line(out, indent, declText + ";");
        const std::string qualifier =
            cm.structName + templateUse(cm) + "::" + cm.structName;
        *defs += (cm.typeParams.empty() ? "" : templateHead(cm) + "\n") +
                 qualifier + "(" + joinStrings(params, ", ") + ")" + baseInit +
                 " {\n" + ctorBodyText(cd, cm, 1) + "}\n\n";
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
    // The model type comes from the structural annotation re-parse, which
    // cannot see type aliases or enum kinds. When it has no spelling, sema's
    // resolved type for the annotation is the authoritative one.
    Type fieldType = untypedAsUnknown(m.type);
    const TypeNode* annotation = nullptr;
    if (m.field && m.field->type) {
        annotation = m.field->type.get();
    } else if (m.parameterProperty && m.parameterProperty->type) {
        annotation = m.parameterProperty->type.get();
    }
    if (annotation != nullptr && !classify(fieldType).representable) {
        if (auto resolved = typeOfNode(*annotation)) {
            fieldType = untypedAsUnknown(*resolved);
        }
    }
    const Expr* initializer = nullptr;
    if (m.field) {
        initializer = m.field->initializer.get();
    } else if (m.parameterProperty) {
        initializer = m.parameterProperty->defaultValue.get();
    }
    const Node* fieldNode = m.field ? static_cast<const Node*>(m.field)
                                    : static_cast<const Node*>(m.parameterProperty);
    if (m.isWeak) {
        const std::string pointee = weakPointee(fieldType);
        if (pointee.empty()) {
            if (fieldNode != nullptr) {
                reportUnsupported(
                    *fieldNode, diagcode::kTypeMismatch,
                    "type mismatch: weak field '" + m.name +
                        "' must have a class reference type");
            }
            return stat + "double " + name + "{}";
        }
        return stat + "::cp3::runtime::WeakRef<" + pointee + "> " + name + "{}";
    }
    CppInfo ci = classify(fieldType);
    // An explicit `unknown` field has no C++ spelling, but its initializer
    // fixes the representation (`count: unknown = 5`). Infer from the value
    // exactly as an `unknown` method return type does from its body.
    const bool annotated = annotation != nullptr;
    if (!ci.representable && m.hasInitializer && initializer != nullptr &&
        (!annotated || fieldType.isUnknown())) {
        const CppInfo init = info(*initializer);
        if (init.representable && init.shape != Shape::Void) ci = init;
    }
    if (!ci.representable) {
        if (fieldNode != nullptr) {
            reportUnsupported(
                *fieldNode, diagcode::kTypeMismatch,
                "type mismatch: field '" + m.name +
                    "' has a type with no C++ representation");
        }
        return stat + "double " + name + "{}";
    }
    if (m.hasInitializer && initializer != nullptr) {
        if (ci.shape == Shape::Optional && isNullishLiteral(*initializer)) {
            return stat + ci.cpp + " " + name + " = std::nullopt";
        }
        return stat + ci.cpp + " " + name + " = " + expr(*initializer);
    }
    return stat + ci.cpp + " " + name + "{}";
}

std::string Generator::methodReturnType(const MethodDeclaration& md,
                                        const ClassModel* cm,
                                        const std::vector<std::string>& methodTypeParams) {
    if (md.returnType) {
        // Sema's resolved type carries aliases, enum kinds and generic
        // arguments the structural re-parse cannot see (D1).
        if (auto t = typeOfNode(*md.returnType)) {
            *t = untypedAsUnknown(*t);
            if (cm != nullptr) rewriteTypeParams(*t, cm->typeParams);
            rewriteTypeParams(*t, methodTypeParams);
            CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
            const bool returnsValue =
                md.body && firstReturnedExpr(*md.body) != nullptr;
            if (!t->isUnknown()) {
                reportUnsupported(
                    md, diagcode::kTypeMismatch,
                    "type mismatch: the return type of method '" +
                        keyNameOf(md.key.get()) +
                        "' has no C++ representation");
            }
            // An explicit `unknown` return type has no C++ spelling, but the
            // body can deduce it. A method with no returned value stays void.
            // An unrepresentable annotation already reported above still gets
            // a valid placeholder so the return sites do not cascade.
            return returnsValue ? "auto" : "void";
        }
    }
    if (md.body) {
        if (const Expr* returned = firstReturnedExpr(*md.body)) {
            const CppInfo ci = info(*returned);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
            // An inferred return whose value has no type spelling is deduced
            // by C++ from the returned expression, like an unannotated
            // function's return (emit/decl.cpp).
            if (!ci.representable) return "auto";
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
        // packs the trailing arguments into it (modelCallArgs below, and the
        // constructor path in class_expr.cpp).
        Type t = Type::unknown();
        if (p.type) {
            if (auto resolved = typeOfNode(*p.type)) t = *resolved;
        } else if (p.name) {
            if (const Type* st = sema_.typeOf(p.name->nodeId)) t = *st;
        }
        t = untypedAsUnknown(t);
        if (cm != nullptr) rewriteTypeParams(t, cm->typeParams);
        rewriteTypeParams(t, methodTypeParams);
        Type elem = Type::unknown();
        if (t.kind == TypeKind::Array && !t.parts.empty()) elem = t.parts.front();
        const CppInfo ec = classify(elem);
        if (!ec.representable || ec.shape == Shape::Void) {
            // One C++ parameter can only collect a homogeneous array, so an
            // untyped / unknown-element rest parameter has no lowering: the
            // call site could pass values of unrelated types.
            reportUnsupported(
                p, diagcode::kTypeMismatch,
                "type mismatch: rest parameter '..." + name +
                    "' must have an array type whose element type has a C++ "
                    "representation");
            return "::cp3::runtime::Array<double> " + name;
        }
        return "::cp3::runtime::Array<" + ec.cpp + "> " + name;
    }
    Type t = Type::unknown();
    const bool annotated = p.type != nullptr;
    if (annotated) {
        if (auto resolved = typeOfNode(*p.type)) {
            t = *resolved;
        } else {
            reportUnsupported(
                p, diagcode::kTypeMismatch,
                "type mismatch: parameter '" + name +
                    "' has a type with no C++ representation");
            return "double " + name;
        }
    } else if (p.name) {
        if (const Type* st = sema_.typeOf(p.name->nodeId)) t = *st;
    }
    t = untypedAsUnknown(t);
    if (cm != nullptr) rewriteTypeParams(t, cm->typeParams);
    rewriteTypeParams(t, methodTypeParams);
    if (p.optional && !t.isUnknown()) t = Type::nullable(t);
    const CppInfo ci = classify(t);
    if (ci.representable && ci.shape != Shape::Void) return ci.cpp + " " + name;
    if (t.isUnknown() || t.isError()) {
        if (p.optional) {
            // An optional parameter needs a concrete storage type for the
            // omitted argument; an untyped one has none.
            reportUnsupported(
                p, diagcode::kTypeMismatch,
                "type mismatch: optional parameter '" + name +
                    "' has no C++ type");
            return "double " + name;
        }
        // An untyped parameter is a C++20 abbreviated function template
        // parameter: the call site deduces its type (the same fallback an
        // untyped arrow parameter gets).
        return "auto " + name;
    }
    reportUnsupported(
        p, diagcode::kTypeMismatch,
        "type mismatch: parameter '" + name +
            "' has a type with no C++ representation");
    return "double " + name;
}

std::string Generator::accessorReturnType(const AccessorDeclaration& ad,
                                          const ClassModel* cm) {
    if (ad.returnType) {
        if (auto t = typeOfNode(*ad.returnType)) {
            *t = untypedAsUnknown(*t);
            if (cm != nullptr) rewriteTypeParams(*t, cm->typeParams);
            CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
            const bool returnsValue =
                ad.body && firstReturnedExpr(*ad.body) != nullptr;
            if (!t->isUnknown()) {
                reportUnsupported(
                    ad, diagcode::kTypeMismatch,
                    "type mismatch: the accessor return type has no C++ "
                    "representation");
            }
            // An explicit `unknown` return type has no C++ spelling, but the
            // getter body can deduce it. A getter with no returned value stays
            // void. An unrepresentable annotation already reported above still
            // gets a valid placeholder so the return sites do not cascade.
            return returnsValue ? "auto" : "void";
        }
    }
    if (ad.body) {
        if (const Expr* returned = firstReturnedExpr(*ad.body)) {
            const CppInfo ci = info(*returned);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
            if (!ci.representable) return "auto";
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
            Type t = Type::unknown();
            if (p->type) {
                if (auto resolved = typeOfNode(*p->type)) t = *resolved;
            } else if (const Type* st = sema_.typeOf(p->name->nodeId)) {
                t = *st;
            }
            t = untypedAsUnknown(t);
            rewriteTypeParams(t, cm.typeParams);
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
        Type t = Type::unknown();
        if (p->type) {
            if (auto resolved = typeOfNode(*p->type)) t = *resolved;
        } else if (const Type* st = sema_.typeOf(id->nodeId)) {
            t = *st;
        }
        t = untypedAsUnknown(t);
        // A method of a generic class, or a generic method, names its own
        // type parameters in the signature.
        rewriteTypeParams(t, cm.typeParams);
        rewriteTypeParams(t, methodTypeParams);
        if (p->isRest) {
            // The signature already reported an untyped / unknown-element
            // rest parameter; bind its placeholder array so the body's
            // `length`/index reads do not cascade.
            const bool usable =
                t.kind == TypeKind::Array && !t.parts.empty() &&
                classify(t.parts.front()).representable &&
                classify(t.parts.front()).shape != Shape::Void;
            if (!usable) t = Type::array(Type::number());
        }
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
        Type t = Type::unknown();
        if (p->type) {
            if (auto resolved = typeOfNode(*p->type)) t = *resolved;
        } else if (const Type* st = sema_.typeOf(id->nodeId)) {
            t = *st;
        }
        t = untypedAsUnknown(t);
        rewriteTypeParams(t, cm.typeParams);
        if (p->isRest) {
            const bool usable =
                t.kind == TypeKind::Array && !t.parts.empty() &&
                classify(t.parts.front()).representable &&
                classify(t.parts.front()).shape != Shape::Void;
            if (!usable) t = Type::array(Type::number());
        }
        declare(id->name, t);
    }
    std::string body;
    // Constructor parameter properties are ordinary fields assigned before the
    // constructor body runs.
    for (const auto& p : cd.params) {
        if (!p) continue;
        if (p->access == AccessModifier::None && !p->isReadonly) continue;
        const Identifier* id = parameterIdentifier(*p);
        if (id == nullptr) continue;
        line(body, indent,
             "this->" + mapIdent(id->name) + " = " + mapIdent(id->name) + ";");
    }
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
        Type t = Type::unknown();
        if (p->type) {
            if (auto resolved = typeOfNode(*p->type)) t = *resolved;
        } else if (const Type* st = sema_.typeOf(id->nodeId)) {
            t = *st;
        }
        t = untypedAsUnknown(t);
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
    // The element type of the rest parameter (`E` of its `E[]`). Sema's
    // resolved annotation sees aliases; the model type carries the class and
    // method type parameters already rewritten to `TypeParam`.
    std::string elem = "double";
    if (md != nullptr && restAt < md->params.size()) {
        const Parameter* rp = md->params[restAt].get();
        Type t = Type::unknown();
        if (rp && rp->type) {
            if (auto resolved = typeOfNode(*rp->type)) t = *resolved;
        }
        t = untypedAsUnknown(t);
        const Type model =
            restAt < m.type.parts.size() ? m.type.parts[restAt] : Type::unknown();
        const bool mappedArray =
            t.kind == TypeKind::Array && !t.parts.empty() &&
            classify(t.parts.front()).representable;
        if (!mappedArray && model.kind == TypeKind::Array &&
            !model.parts.empty()) {
            t = model;
        }
        if (t.kind == TypeKind::Array && !t.parts.empty()) {
            const CppInfo ec = classify(t.parts.front());
            if (ec.representable && ec.shape != Shape::Void) elem = ec.cpp;
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
