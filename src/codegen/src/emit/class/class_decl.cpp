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
#include "emit/class/auto_weak.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// Local twin of decl.cpp's `tupleSpelling` (which is file-local there): the
// C++ `std::tuple<...>` spelling for a tuple value, so an async method whose
// promise carries a tuple can spell `Promise<std::tuple<...>>(`.
std::optional<std::string> tupleSpelling(const Type& t) {
    std::string cpp;
    if (!isTupleType(t) || !tupleCpp(t, cpp)) return std::nullopt;
    return cpp;
}

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

// One member of a cycle-capable class is a strong edge exactly when its type
// is a direct class handle or an optional of one. Everything else (a number,
// an array, a record) holds no cycle edge.
bool isCycleClassType(const Type& t) {
    if (t.kind == TypeKind::Class) return true;
    if (t.kind == TypeKind::Nullable) {
        return t.parts.size() == 1 && t.parts.front().kind == TypeKind::Class;
    }
    if (t.kind == TypeKind::Union) {
        const Type* cls = nullptr;
        for (const Type& part : t.parts) {
            if (part.isNullish()) continue;
            if (part.kind != TypeKind::Class) return false;
            cls = &part;
        }
        return cls != nullptr;
    }
    return false;
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

// Stage 2 auto-weak: memberFieldText(m) has no owner class, so
// emitClassMember (which does) pushes the declaring class name here for the
// duration of the memberFieldText call and memberFieldText checks the top
// of the stack. Emission is single-threaded per Generator and the push/pop
// pair is lexical, so a nested emission (a field initializer) sees its own
// entry. Empty in report-only mode (no `proven` lines), hence no output
// change until sema's gate passes.
std::vector<std::string> g_autoWeakOwnerStack;

bool memberIsAutoWeak(const SemaResult& sema, const ClassMemberModel& m) {
    if (g_autoWeakOwnerStack.empty()) return false;
    const ClassModel* cm =
        ModelRegistry::get().classByName(g_autoWeakOwnerStack.back());
    return cm != nullptr && isAutoWeakField(sema, *cm, m.name);
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

// Whether this unit owns the declaration behind `cm`. In whole-program mode
// the registry is shared; a class declared in another unit emits there, not
// here (its constructor body was compiled against that unit's sema).
bool Generator::classOwnedByThisUnit(const detail::ClassModel& cm) const {
    if (unitSourceFile_ == nullptr || cm.decl == nullptr) return cm.decl != nullptr;
    return cm.decl->span.file == unitSourceFile_;
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
        if (!classOwnedByThisUnit(*cm)) continue;
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
        if (!classOwnedByThisUnit(*cm)) continue;
        if (cm->local && !cm->namespaceScope) continue;
        ordered.push_back(cm);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const ClassModel* a, const ClassModel* b) {
                         return classDepth(*a) < classDepth(*b);
                     });
    for (ClassModel* cm : ordered) {
        if (!modelEmitted_.insert(cm->decl->nodeId).second) continue;
        if (cm->typeParams.empty()) {
            emitClassStruct(*cm, classDecls_, 0, &classDefs_);
        } else {
            // A generic class's members must be visible in every TU that
            // instantiates it. Emit every body inline inside the shared
            // header's struct; the unit TU emits no duplicate (the header's
            // copy is identical).
            emitClassStruct(*cm, classDecls_, 0, nullptr);
        }
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
    // A class that runs on the reference ABI derives `SharedBase` (which is the
    // common `enable_shared_from_this` base every such class can chain off),
    // so a method returning `this` hands back a shared handle the caller
    // already owns. Ref<T> IS std::shared_ptr<T>.
    //
    // Classes on the local (confinement-optimized) ABI do NOT derive
    // `SharedBase`; they have no weak-ptr control block and so do not support
    // `shared_from_this`.
    const std::string selfBase = cm.structName + templateUseOf(cm);
    std::string bases;
    const ClassModel* baseClass = nullptr;
    if (!cm.baseName.empty()) {
        baseClass = ModelRegistry::get().classByName(cm.baseName);
        bases += "public " + (baseClass != nullptr ? baseClass->cpp : mapIdent(cm.baseName));
    }
    // A cycle-capable class derives `CycleObject`: it carries the runtime type
    // stamp and the visitor the collector walks. It cannot derive
    // `SharedBase` as well: `shared_from_this` needs the std::shared_ptr
    // control block, and a cycle-capable class uses the cycle-aware one.
    const bool cycleCollector = kCycleCollectorCodegen && cm.cycleCollector;
    if (cycleCollector) {
        if (!bases.empty()) bases += ", ";
        bases += "public ::cp3::runtime::CycleObject";
    } else if (!cm.localRef) {
        // Only add a direct `SharedBase` base for classes whose parent does
        // not provide it transitively. The base provides `SharedBase`
        // transitively only when it is a non-built-in Ref-ABI class defined
        // in this unit. A builtin root (e.g. `Error`) does not derive
        // SharedBase, so add it here. An imported class that is on the Ref
        // ABI derives SharedBase itself; skip in that case.
        const bool baseHasSharedBase =
            baseClass != nullptr && !baseClass->builtin && !baseClass->localRef;
        if (!baseHasSharedBase) {
            if (!bases.empty()) bases += ", ";
            bases += "public ::cp3::runtime::SharedBase";
        }
    }
    if (!bases.empty()) head += " : " + bases;
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
    // stamp. Every `new` site overwrites it with the concrete class id. A
    // cycle-capable class inherits the stamp from `CycleObject`.
    if (!cycleCollector && (base == nullptr || base->builtin)) {
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
    // The collector's visitor: the strong edges of this object, enumerated
    // for the trial-deletion collector (docs/design/cycle-collector.md). A
    // cycle-capable class overrides it; every other class keeps the
    // inherited no-op, so its codegen is byte-identical to today's.
    if (cycleCollector) {
        line(out, indent + 1,
             "void cp3_cycle_visit(::cp3::runtime::CycleCollector& c) override {");
        for (const ClassMemberModel& m : cm.members) {
            if (m.ctor != nullptr || m.isMethod) continue;
            if (m.isWeak) continue;  // a weak link is not a strong edge
            if (m.isStatic) continue;  // a static root keeps the island alive
            if (!isCycleClassType(m.type)) continue;  // no class edge
            const std::string field = "this->" + mapIdent(m.name);
            const CppInfo ci = classify(m.type);
            if (ci.shape == Shape::ClassRef) {
                line(out, indent + 2, "c.visitEdge(" + field + ");");
            } else if (ci.shape == Shape::Optional || ci.shape == Shape::Maybe) {
                line(out, indent + 2, "if (" + field + ") c.visitEdge(*" + field + ");");
            }
        }
        line(out, indent + 1, "}");
    }
    // A static block of a non-generic class is run from a namespace-scope
    // anchor emitted after the class closes (private access needs a member
    // function). Emitted here, public regardless of the current section.
    const bool blockAnchored = cm.typeParams.empty() && !cm.local &&
                              std::any_of(cm.members.begin(), cm.members.end(),
                                          [](const ClassMemberModel& m) {
                                              return m.staticBlock != nullptr;
                                          });
    if (blockAnchored && current != 0) {
        line(out, indent + 1, "public:");
        current = 0;
    }
    if (blockAnchored) {
        for (const ClassMemberModel& m : cm.members) {
            if (m.staticBlock == nullptr) continue;
            const StaticBlock& sb = *m.staticBlock;
            const std::string name =
                "cp3_static_block_" + std::to_string(sb.nodeId);
            line(out, indent + 1, "static void " + name + "() {");
            out += staticBlockBodyText(cm, sb, indent + 2);
            line(out, indent + 1, "}");
        }
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
    // Namespace-scope dynamic initialization is merged into the module
    // initializer, which the linker keeps: the anchor runs the block before
    // main, in class declaration order. (`--gc-sections` dropped the
    // class-scope inline-static anchor's initializer entry on the MinGW/lld
    // target.)
    if (blockAnchored) {
        for (const ClassMemberModel& m : cm.members) {
            if (m.staticBlock == nullptr) continue;
            const StaticBlock& sb = *m.staticBlock;
            const std::string name =
                "cp3_static_block_" + std::to_string(sb.nodeId);
            line(out, indent, "namespace {");
            line(out, indent + 1, "[[maybe_unused]] static bool " + name +
                                      " = (::" + namesp_ + "::" + cm.structName +
                                      "::" + name + "(), true);");
            line(out, indent, "}  // namespace");
        }
        line(out, indent, "");
    }
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

std::string Generator::staticBlockBodyText(const ClassModel& cm,
                                           const StaticBlock& sb,
                                           int bodyIndent) {
    const std::string savedReturn = currentReturn_;
    currentReturn_ = "void";
    ++nestingDepth_;
    classStack_.push_back(&cm);
    pushScope();
    std::string body;
    if (sb.body) stmt(*sb.body, body, bodyIndent);
    popScope();
    classStack_.pop_back();
    --nestingDepth_;
    currentReturn_ = savedReturn;
    return body;
}

void Generator::emitClassMember(const ClassModel& cm,
                                const ClassMemberModel& m, std::string& out,
                                int indent, std::string* defs) {
    if (m.staticBlock != nullptr) {
        const StaticBlock& sb = *m.staticBlock;
        const std::string name =
            "cp3_static_block_" + std::to_string(sb.nodeId);
        // A non-generic class's block is emitted by emitClassStruct as a
        // generated public static member function and run from a
        // namespace-scope anchor: an inline class-scope static's
        // initializer entry is dropped by the MinGW/lld GC pass, and the
        // module initializer is not. Local classes keep the class-member
        // anchor (they are emitted mid-function, where a namespace-scope
        // anchor cannot live).
        if (cm.typeParams.empty() && !cm.local) return;
        // `inline static` initializers are never odr-used by user code, so an
        // empty-body DCE build would drop the lambda and its side effects.
        // The attribute tells the linker to keep the section.
        line(out, indent, "inline __attribute__((used)) static bool " + name + " = []() {");
        std::string body = staticBlockBodyText(cm, sb, indent + 1);
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
        g_autoWeakOwnerStack.push_back(cm.name);
        const std::string fieldText = memberFieldText(m);
        g_autoWeakOwnerStack.pop_back();
        line(out, indent, fieldText + ";");
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
    // A `?` field (`x?: T`) is a nullable member: it lowers to
    // `std::optional<T>` (null empty state) so the struct can hold the
    // `undefined` a read of the field must report. Same rule as interface
    // records and the class model's optional-parameter lowering.
    if (m.field && m.field->optional && !fieldType.isNullable() &&
        !fieldType.isUnion()) {
        fieldType = Type::nullable(fieldType);
    }
    // (A `parameterProperty` carries its `?` from the ctor parameter.)
    if (m.parameterProperty && m.parameterProperty->optional &&
        !fieldType.isNullable() && !fieldType.isUnion()) {
        fieldType = Type::nullable(fieldType);
    }
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
    // Stage 2: a proven compiler-injected edge lowers exactly like a
    // manual `weak` field (WeakRef, weak-cost zero). Candidates and
    // rejected edges stay strong refs (report-only default).
    if (m.isWeak || memberIsAutoWeak(sema_, m)) {
        const detail::WeakPointee pointee = weakPointee(fieldType);
        if (pointee.empty()) {
            if (fieldNode != nullptr) {
                reportUnsupported(
                    *fieldNode, diagcode::kTypeMismatch,
                    "type mismatch: weak field '" + m.name +
                        "' must have a class reference type");
            }
            return stat + "double " + name + "{}";
        }
        return stat + weakRefCpp(pointee) + " " + name + "{}";
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
    if (md.isAsync) {
        // spec 14: the C++ signature is the coroutine's promise type. A cp3
        // `return` inside the body yields the promise's VALUE type.
        const Type value = asyncValueType(md);
        if (const auto tuple = tupleSpelling(value)) {
            return "::cp3::runtime::Promise<" + *tuple + ">";
        }
        const CppInfo ci = classify(detail::promiseOf(value));
        if (ci.representable) return ci.cpp;
        reportUnsupported(md, diagcode::kTypeMismatch,
                          "type mismatch: this async method's value type has "
                          "no C++ spelling");
        return "void";
    }
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
    if (ad.body && currentReturn_ != "void" && currentReturn_ != "auto") {
        // As in emit/decl.cpp's plainBody: a non-void getter must not fall
        // off its end (undefined behavior, exploited by clang at -O2).
        line(body, indent, "return {};");
    }
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
    const std::optional<Type> savedReturnType = currentReturnType_;
    const std::optional<Type> savedExpected = expected_;
    const bool savedInAsync = inAsyncBody_;
    if (md.isAsync) {
        // spec 14: `async m(): T` is a coroutine returning `Promise<T>`;
        // the loop must be driven before the process exits.
        needsEventLoop_ = true;
        const Type value = asyncValueType(md);
        const CppInfo ci = classify(value);
        if (const auto tuple = tupleSpelling(value)) {
            currentReturn_ = *tuple;
        } else {
            currentReturn_ = (ci.representable && ci.shape != Shape::Void)
                                 ? ci.cpp
                                 : "void";
        }
        // Inside the coroutine `return expr` yields the promise's VALUE
        // type (`co_return expr`); the `return` site reads currentReturn_.
        currentReturnType_ = value;
        inAsyncBody_ = true;
    } else {
        currentReturn_ = methodReturnType(md, &cm, methodTypeParams);
        // The return sites read currentReturnType_ to lower a nullish literal
        // to `std::nullopt` (the plainBody rule); keep it in sync with the
        // annotation, as plain bodies do.
        currentReturnType_.reset();
        if (md.returnType) {
            if (auto t = typeOfNode(*md.returnType)) {
                *t = untypedAsUnknown(*t);
                rewriteTypeParams(*t, cm.typeParams);
                rewriteTypeParams(*t, methodTypeParams);
                currentReturnType_ = std::move(*t);
            }
        }
    }
    expected_ = currentReturnType_;
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
    if (md.body && currentReturn_ != "void" && currentReturn_ != "auto") {
        // As in emit/decl.cpp's plainBody: a non-void method must not fall
        // off its end (undefined behavior, exploited by clang at -O2).
        line(body, indent, inAsyncBody_ ? "co_return {};" : "return {};");
    }
    classStack_.pop_back();
    popScope();
    --nestingDepth_;
    inAsyncBody_ = savedInAsync;
    currentReturnType_ = savedReturnType;
    expected_ = savedExpected;
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

WeakPointee Generator::weakPointee(const Type& fieldType) {
    const CppInfo ci = classify(stripNullish(fieldType));
    static const char* const kPrefixes[] = {"::cp3::runtime::Ref<",
                                            "::cp3::runtime::CycleRef<"};
    if (ci.shape != Shape::ClassRef) return {};
    for (std::size_t i = 0; i < std::size(kPrefixes); ++i) {
        const std::size_t n = std::char_traits<char>::length(kPrefixes[i]);
        if (ci.cpp.compare(0, n, kPrefixes[i]) != 0) continue;
        // The target's handle decides the weak-link spelling: a
        // collector-eligible target takes CycleWeakRef.
        return WeakPointee{ci.cpp.substr(n, ci.cpp.size() - n - 1), i == 1};
    }
    return {};
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
