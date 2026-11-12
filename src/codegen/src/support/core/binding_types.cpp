// =============================================================================
// src/codegen/src/support/binding_types.cpp - a binding's declared/inferred type.
//
// OWNERSHIP: codegen module. Implements `Generator::bindingType` and
// `Generator::resolvedBindingType` declared in detail/generator.h, split out of
// type_map.cpp for the file budget (STYLE.md sec.2).
//
// This is where the concurrency families hook their results into ordinary
// `const x = ...` bindings: `parallel.reduce` takes the accumulator type and
// the worker handle's `request` promise / awaited message take the channel's
// `Out` type (D80), even though sema leaves the ambient call unknown.
// =============================================================================
#include "detail/generator/generator.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

Type Generator::memberReadType(const MemberExpr& m) const {
    if (!m.object || m.property.empty()) return Type::unknown();
    // An enum member (`Color.Green`): its type is the enum, even though an
    // imported name is only an ambient value in sema (no codegen binding).
    if (m.object->kind == NodeKind::Identifier) {
        const std::string& name = static_cast<const Identifier&>(*m.object).name;
        const auto found = enums_.find(name);
        if (found != enums_.end() &&
            found->second.member(m.property) != nullptr) {
            return Type::named(TypeKind::Enum, name);
        }
    }
    // A read through an unannotated worker listener parameter is ambient in
    // sema even though the channel fixes the parameter's shape. Recover the
    // member type from that shape: `length` on an array is a number, and a
    // record field keeps its (possibly nested) cp3 type.
    const Type recv = effectiveType(*m.object);
    if (m.property == "length" && recv.kind == TypeKind::Array) {
        return Type::number();
    }
    Type object;
    bool optional = false;
    if (!objectReceiver(recv, object, optional)) return Type::unknown();
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(object, structName, fields)) return Type::unknown();
    for (const ObjectField& field : fields) {
        if (field.name == m.property) return field.type;
    }
    return Type::unknown();
}

std::optional<Type> Generator::bindingType(const VariableDeclarator& d) const {
    if (d.type) {
        if (auto t = typeOfNode(*d.type)) {
            return t;
        }
    }
    // A `parallel.reduce` result takes the accumulator type even though sema
    // leaves the ambient call unknown (the binding must not be `auto`: its
    // closure was lowered against the init's C++ type).
    if (d.init && d.init->kind == NodeKind::CallExpr) {
        if (auto t = parallelCallType(static_cast<const CallExpr&>(*d.init))) {
            return *t;
        }
        // `const p = w.request(v)` is the typed request promise (D80).
        if (auto t = workerCallType(static_cast<const CallExpr&>(*d.init))) {
            return *t;
        }
    }
    // `const r = await w.request(v)` binds the next Out message (D80).
    if (d.init && d.init->kind == NodeKind::AwaitExpr) {
        const auto& awaited = static_cast<const AwaitExpr&>(*d.init);
        if (awaited.argument && awaited.argument->kind == NodeKind::CallExpr) {
            if (auto t = workerCallType(
                    static_cast<const CallExpr&>(*awaited.argument))) {
                if (isPromiseType(*t)) return promiseValue(*t);
            }
        }
    }
    // `const w = new Worker(...)` is the typed channel handle (D80). The marker
    // carries the channel's direction types and the worker unit's namespace.
    if (d.init && d.init->kind == NodeKind::NewExpr) {
        if (auto t = workerBindingFor(static_cast<const NewExpr&>(*d.init))) {
            return *t;
        }
    }
    if (const Type* t = sema_.typeOf(d.nodeId)) {
        if (!t->isUnknown() && !t->isError()) return *t;
    }
    if (d.id) {
        if (const Type* t = sema_.typeOf(d.id->nodeId)) {
            if (!t->isUnknown() && !t->isError()) return *t;
        }
    }
    return std::nullopt;
}

// The storage shape an unannotated binding gets when its initializer is a
// member read: the member's declared type (`Node | undefined`), not the
// engaged view sema narrows the read to at this position. The storage is
// what the initializer expression actually evaluates to, so the two must
// agree; uses of the binding unwrap or re-narrow as usual.
std::optional<Type> Generator::memberStorageShape(
    const VariableDeclarator& d) const {
    if (d.type || !d.init) return std::nullopt;
    if (d.init->kind != NodeKind::MemberExpr) return std::nullopt;
    const auto& m = static_cast<const MemberExpr&>(*d.init);
    if (!m.object) return std::nullopt;
    const Type baseT = effectiveType(*m.object);
    const Type base = stripNullish(baseT);
    if (const ClassModel* cm = classModelOf(base)) {
        if (const ClassMemberModel* mem =
                findClassMemberChain(*cm, m.property, nullptr)) {
            if (mem->isMethod) return std::nullopt;
            return mem->type;
        }
    }
    if (const ObjectModel* om = objectModelOf(base);
        om != nullptr && ModelRegistry::get().objectUsable(om->label) &&
        objectModelUsed(*om)) {
        if (const ObjectFieldModel* f = findObjectField(*om, m.property)) {
            if (f->isMethod) return std::nullopt;
            return f->type;
        }
    }
    return std::nullopt;
}

// The binding's effective type: declared/annotated first, then inferred from
// its initializer. This is what we both emit and register in the scope env.
std::optional<Type> Generator::resolvedBindingType(const VariableDeclarator& d) const {
    if (auto storage = memberStorageShape(d)) {
        return storage;
    }
    if (auto declared = bindingType(d)) {
        // `unknown` has no C++ spelling; a binding initialized with a concrete
        // value is stored as that value's representation (the declarator uses
        // the initializer's type), so the declaration environment must agree.
        // Without this, `typeof x` cannot see the C++ shape it is inspecting.
        if (!declared->isUnknown() && !declared->isError()) return declared;
    }
    if (d.init) {
        const Type inferred = effectiveType(*d.init);
        if (!inferred.isUnknown() && !inferred.isError()) {
            return inferred;
        }
    }
    return std::nullopt;
}

}  // namespace cp3
