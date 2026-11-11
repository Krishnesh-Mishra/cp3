// =============================================================================
// src/codegen/src/support/class_collect.cpp - Generator model collection for
// the spec Â§15 object surface (STYLE.md Â§8).
//
// Builds the unit's ClassModel/ObjectModel registry (detail/class_model.h)
// before the emit walk: `collectModels` registers the class names, the
// interface shapes, every member of every class, and the structural object
// shapes the unit's bodies use (via support/class_walk.cpp). Member types come
// from the annotation when there is one, else from sema's node types (fields
// inferred from an initializer).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/class/class_model.h"
#include "detail/class/class_walk.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

void Generator::collectModels(const Program& program) {
    ModelRegistry::get().registerBuiltinErrorClasses();
    for (const auto& s : program.body) {
        if (s) collectClassNames(*s);
    }
    // Imported (from another unit) top-level classes: register the same TU-scope
    // model so emitModelMember / new / class-ref can find them. The TU of
    // the owning module emits the struct itself; the consumer TU re-emits
    // its own class under its own namespace. Same-named classes in
    // different namespaces are distinct types at the C++ level, which
    // matches cp3's per-unit ABI contract.
    for (const ClassDeclaration* c : options_.importedClasses) {
        if (c != nullptr) collectClassNames(*c);
    }
    // Imported interfaces: register the model in this TU too, so a type
    // annotation like `Tab` has a wrapper here as well as in the owning
    // module (each unit emits its own, layouts agree).
    for (const InterfaceDeclaration* i : options_.importedInterfaces) {
        if (i != nullptr) registerInterface(*i);
    }
    collectInterfaces(program);
    for (ClassModel* cm : ModelRegistry::get().allClasses()) {
        collectClassMemberModels(*cm);
    }
    for (const auto& s : program.body) {
        if (s) collectBodyModels(*s);
    }
    // The body walk reaches places no statement walk does (a class inside an
    // arrow-function body), so it may have registered classes the first member
    // pass never saw: collect their members now.
    for (ClassModel* cm : ModelRegistry::get().allClasses()) {
        collectClassMemberModels(*cm);
    }
}

// A class declared at the module root is top-level; anything the statement
// nests (a block, a function body) is a local class.
void Generator::collectClassNames(const Stmt& s) {
    // Top-level statements of a unit live at namespace scope. Those that
    // hold a class declaration register a namespace-scope struct; everything
    // else has nothing to register. Nested walks (declarations inside a
    // function / block / if-statement) receive `true` from callers below.
    walkClassesForModels(s, /*local=*/false, sema_);
}

void Generator::collectInterfaces(const Program& program) {
    for (const auto& sp : program.body) {
        if (!sp) continue;
        const Stmt* s = sp.get();
        if (s->kind == NodeKind::ExportDeclaration) {
            const auto& ed = static_cast<const ExportDeclaration&>(*s);
            s = ed.declaration.get();
        }
        if (s == nullptr || s->kind != NodeKind::InterfaceDeclaration) continue;
        registerInterface(static_cast<const InterfaceDeclaration&>(*s));
    }
}

void Generator::registerInterface(const InterfaceDeclaration& i) {
    if (!i.typeParams.empty()) {
        // A generic interface (D26): the declaration is registered, and
        // every use site instantiates it (support/type_map.cpp).
        GenericInterfaceModel generic;
        generic.decl = &i;
        for (const auto& tp : i.typeParams) {
            if (tp) generic.typeParams.push_back(tp->name);
        }
        ModelRegistry::get().addGenericInterface(i.name,
                                                 std::move(generic));
        return;
    }
    std::vector<ObjectFieldModel> fields;
    bool hasIndex = false;
    Type indexType;
    for (const auto& mp : i.members) {
        if (!mp) continue;
        ObjectFieldModel f;
        if (mp->kind == NodeKind::IndexSignature) {
            // `[name: string]: T` (v0.2.4): the value type rides on the
            // model so a typed EventEmitter's dynamic event name can
            // resolve its payload type through the Events map.
            const auto& is = static_cast<const IndexSignature&>(*mp);
            indexType = is.valueType ? modelTypeFromTypeNode(*is.valueType)
                                     : Type::unknown();
            hasIndex = true;
            continue;
        }
        if (mp->kind == NodeKind::PropertySignature) {
            const auto& ps = static_cast<const PropertySignature&>(*mp);
            f.name = keyNameOf(ps.key.get());
            f.optional = ps.optional;
            if (ps.type) f.type = modelTypeFromTypeNode(*ps.type);
        } else if (mp->kind == NodeKind::MethodSignature) {
            const auto& ms = static_cast<const MethodSignature&>(*mp);
            f.name = keyNameOf(ms.key.get());
            f.optional = ms.optional;
            std::vector<Type> params;
            for (const auto& p : ms.params) {
                if (!p) continue;
                params.push_back(p->type ? modelTypeFromTypeNode(*p->type)
                                         : Type::unknown());
            }
            const Type ret = ms.returnType
                                 ? modelTypeFromTypeNode(*ms.returnType)
                                 : Type::unknown();
            f.type = Type::function(std::move(params), ret);
            f.isFunction = true;
            f.isMethod = true;
        } else {
            continue;
        }
        if (!f.name.empty()) fields.push_back(std::move(f));
    }
    // Sema's shape label carries the index signature entry; the registry
    // must key the model under the same label or lookups from the typed
    // EventEmitter<Events> path miss it.
    const std::string label =
        objectLabelOf(fields, hasIndex ? &indexType : nullptr);
    ObjectModel& om = ModelRegistry::get().addInterface(i.name, label, fields);
    if (hasIndex) {
        om.hasIndex = true;
        om.indexType = std::move(indexType);
    }
}
void Generator::collectBodyModels(const Stmt& s) { walkStmtModels(s, sema_, false); }

void Generator::collectClassMemberModels(ClassModel& cm) {
    if (cm.membersCollected) return;
    cm.membersCollected = true;
    const ClassDeclaration& c = *cm.decl;
    for (const auto& mp : c.members) {
        if (!mp) continue;
        if (mp->kind == NodeKind::PropertyDeclaration) {
            const auto& p = static_cast<const PropertyDeclaration&>(*mp);
            const std::string name = classMemberName(p.key.get(), p.computed);
            // A computed name that is not a literal has no C++ spelling; the
            // sema reported it (CP3707), so the member is left out here.
            if (name.empty()) continue;
            ClassMemberModel m;
            m.name = name;
            m.isStatic = p.isStatic;
            m.isPrivate = p.access == AccessModifier::Private ||
                          (!name.empty() && name.front() == '#');
            m.isProtected = p.access == AccessModifier::Protected;
            m.isWeak = p.isWeak;
            m.hasInitializer = p.initializer != nullptr;
            m.field = &p;
            if (p.type) {
                m.type = modelTypeFromTypeNode(*p.type);
            } else if (p.initializer) {
                m.type = widenType(effectiveType(*p.initializer));
            } else {
                m.type = Type::unknown();
            }
            rewriteTypeParams(m.type, cm.typeParams);
            cm.members.push_back(std::move(m));
            continue;
        }
        if (mp->kind == NodeKind::MethodDeclaration) {
            collectClassMethodModel(
                cm, static_cast<const MethodDeclaration&>(*mp));
            continue;
        }
        if (mp->kind == NodeKind::ConstructorDeclaration) {
            const auto& cd = static_cast<const ConstructorDeclaration&>(*mp);
            for (const auto& p : cd.params) {
                if (!p) continue;
                if (p->access == AccessModifier::None && !p->isReadonly) {
                    continue;
                }
                const Identifier* id = parameterIdentifier(*p);
                if (id == nullptr) continue;
                ClassMemberModel field;
                field.name = id->name;
                field.isPrivate = p->access == AccessModifier::Private ||
                                  (!field.name.empty() &&
                                   field.name.front() == '#');
                field.isProtected = p->access == AccessModifier::Protected;
                field.hasInitializer = true;
                field.parameterProperty = p.get();
                if (p->type) {
                    field.type = modelTypeFromTypeNode(*p->type);
                } else if (p->defaultValue) {
                    field.type = widenType(effectiveType(*p->defaultValue));
                } else {
                    field.type = Type::unknown();
                }
                if (p->optional && !field.type.isUnknown()) {
                    field.type = Type::nullable(field.type);
                }
                rewriteTypeParams(field.type, cm.typeParams);
                cm.members.push_back(std::move(field));
            }
            ClassMemberModel m;
            m.name = "constructor";
            m.isMethod = true;
            m.ctor = &cd;
            cm.members.push_back(std::move(m));
            continue;
        }
        if (mp->kind == NodeKind::AccessorDeclaration) {
            collectClassAccessorModel(cm, *mp);
            continue;
        }
        if (mp->kind == NodeKind::StaticBlock) {
            if (cm.local) {
                sink_.error(diagcode::kGenericLocalMethod, mp->span,
                            "a static initialization block in a function-local "
                            "class is not supported: C++ local classes cannot "
                            "have static data members");
                continue;
            }
            ClassMemberModel m;
            m.name = "static block";
            m.staticBlock = &static_cast<const StaticBlock&>(*mp);
            cm.members.push_back(std::move(m));
            continue;
        }
        // The parser only creates Property, Method, Constructor, Accessor and
        // StaticBlock members; reaching here means the AST violates that
        // invariant.
        reportUnsupported(*mp, diagcode::kInternalError,
                          "internal error: unhandled class member kind");
    }
}

// A method member: its name, modifiers, signature and (for a generic method)
// its own type parameters - the method lowers to a C++ member function
// template over them (spec Â§16).
void Generator::collectClassMethodModel(ClassModel& cm,
                                        const MethodDeclaration& md) {
    const std::string name = keyNameOf(md.key.get());
    if (name.empty()) {
        // A cp3 class is a fixed set of members, so a computed member name is
        // only known at runtime: the program cannot compile.
        sink_.error(diagcode::kComputedMemberName, md.span,
                    "a computed class member name must be a literal: a cp3 "
                    "class has a fixed set of members");
        return;
    }
    ClassMemberModel m;
    m.name = name;
    m.isStatic = md.isStatic;
    m.isPrivate = md.access == AccessModifier::Private ||
                  (!name.empty() && name.front() == '#');
    m.isProtected = md.access == AccessModifier::Protected;
    m.isMethod = true;
    m.isAbstract = md.isAbstract;
    m.method = &md;
    for (const auto& tp : md.typeParams) {
        if (tp) m.typeParams.push_back(tp->name);
    }
    if (!m.typeParams.empty() && cm.local) {
        // C++ has no member template in a local class, so a generic method of
        // a function-local class has no lowering.
        sink_.error(diagcode::kGenericLocalMethod, md.span,
                    "a generic method of a function-local class is not "
                    "supported: C++ has no member template in a local class");
    }
    std::vector<Type> params;
    for (const auto& p : md.params) {
        if (!p) continue;
        params.push_back(p->type ? modelTypeFromTypeNode(*p->type)
                                 : Type::unknown());
    }
    const Type ret = md.returnType ? modelTypeFromTypeNode(*md.returnType)
                                   : Type::unknown();
    m.type = Type::function(std::move(params), ret);
    rewriteTypeParams(m.type, cm.typeParams);
    rewriteTypeParams(m.type, m.typeParams);
    if (!m.isStatic && virtualMethodSet_ != nullptr &&
        virtualMethodSet_->find(cm.name + "::" + name) !=
            virtualMethodSet_->end()) {
        // The program-wide override analysis found an override of this
        // member somewhere in the whole program.
        m.isVirtual = true;
    }
    cm.members.push_back(std::move(m));
}

// Get/set accessors (D21) lower to generated get_/set_ methods; the getter's
// type governs reads of the property name.
void Generator::collectClassAccessorModel(ClassModel& cm, const ClassMember& mp) {
    const auto& ad = static_cast<const AccessorDeclaration&>(mp);
    const std::string name = classMemberName(ad.key.get(), ad.computed);
    // A computed name that is not a literal has no C++ spelling; the sema
    // reported it (CP3707), so the accessor is left out here.
    if (name.empty()) return;
    ClassMemberModel m;
    m.name = name;
    m.isStatic = ad.isStatic;
    m.isPrivate = ad.access == AccessModifier::Private ||
                  (!name.empty() && name.front() == '#');
    m.isProtected = ad.access == AccessModifier::Protected;
    m.accessor = &ad;
    m.isGetter = ad.accessor == AccessorKind::Get;
    if (m.isGetter) {
        m.type = ad.returnType ? modelTypeFromTypeNode(*ad.returnType)
                               : Type::unknown();
    } else if (!ad.params.empty() && ad.params.front() &&
               ad.params.front()->type) {
        m.type = modelTypeFromTypeNode(*ad.params.front()->type);
    } else {
        m.type = Type::unknown();
    }
    rewriteTypeParams(m.type, cm.typeParams);
    cm.members.push_back(std::move(m));
}

}  // namespace cp3

