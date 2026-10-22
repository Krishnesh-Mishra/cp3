// =============================================================================
// src/codegen/src/support/class_collect.cpp - Generator model collection for
// the spec §15 object surface (STYLE.md §8).
//
// Builds the unit's ClassModel/ObjectModel registry (detail/class_model.h)
// before the emit walk: `collectModels` registers the class names, the
// interface shapes, every member of every class, and the structural object
// shapes the unit's bodies use (via support/class_walk.cpp). Member types come
// from the annotation when there is one, else from sema's node types (fields
// inferred from an initializer).
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <utility>
#include <vector>

#include "detail/class_model.h"
#include "detail/class_walk.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

void Generator::collectModels(const Program& program) {
    for (const auto& s : program.body) {
        if (s) collectClassNames(*s);
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
    walkClassesForModels(s, s.kind != NodeKind::ClassDeclaration, sema_);
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
        const auto& i = static_cast<const InterfaceDeclaration&>(*s);
        if (!i.typeParams.empty()) continue;  // generic interface: CP3002 later
        std::vector<ObjectFieldModel> fields;
        for (const auto& mp : i.members) {
            if (!mp) continue;
            ObjectFieldModel f;
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
        ModelRegistry::get().addInterface(i.name, objectLabelOf(fields), fields);
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
            const std::string name = keyNameOf(p.key.get());
            if (name.empty()) continue;
            ClassMemberModel m;
            m.name = name;
            m.isStatic = p.isStatic;
            m.isPrivate = p.access == AccessModifier::Private;
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
            const auto& md = static_cast<const MethodDeclaration&>(*mp);
            const std::string name = keyNameOf(md.key.get());
            if (name.empty()) {
                notImplemented(md, "computed class member names");
                continue;
            }
            ClassMemberModel m;
            m.name = name;
            m.isStatic = md.isStatic;
            m.isPrivate = md.access == AccessModifier::Private;
            m.isProtected = md.access == AccessModifier::Protected;
            m.isMethod = true;
            m.isAbstract = md.isAbstract;
            m.method = &md;
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
            cm.members.push_back(std::move(m));
            continue;
        }
        if (mp->kind == NodeKind::ConstructorDeclaration) {
            ClassMemberModel m;
            m.name = "constructor";
            m.isMethod = true;
            m.ctor = &static_cast<const ConstructorDeclaration&>(*mp);
            cm.members.push_back(std::move(m));
            continue;
        }
        if (mp->kind == NodeKind::AccessorDeclaration) {
            collectClassAccessorModel(cm, *mp);
            continue;
        }
        notImplemented(*mp, "this class member");
    }
}

// Get/set accessors (D21) lower to generated get_/set_ methods; the getter's
// type governs reads of the property name.
void Generator::collectClassAccessorModel(ClassModel& cm, const ClassMember& mp) {
    const auto& ad = static_cast<const AccessorDeclaration&>(mp);
    const std::string name = keyNameOf(ad.key.get());
    if (name.empty()) {
        notImplemented(ad, "computed class member names");
        return;
    }
    ClassMemberModel m;
    m.name = name;
    m.isStatic = ad.isStatic;
    m.isPrivate = ad.access == AccessModifier::Private;
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
