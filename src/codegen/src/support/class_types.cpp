// =============================================================================
// src/codegen/src/support/class_types.cpp - class / interface / annotation
// type lowering shared by the emit units (part of the type_map contract).
//
// The class and structural-model arms of `classify` and the annotation ->
// cp3 `Type` mapper live here so type_map.cpp stays a focused value-shape
// unit. Class handles are `Ref<T>`; interfaces use their generated wrapper.
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail/class_model.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace detail {

// The Class-kind tail of `classify`: the family spellings (Buffer, typed
// views, Map/Set, Date), then the builtin EventEmitter (D74), then `Promise<T>`
// (the only other class with a spelling). Anything else stays out of M1.
CppInfo classifyClassValue(const Type& t) {
    CppInfo info;
    info.type = t;
    // The nominal std/buffer `Buffer` (src/std/buffer/codegen.cpp).
    if (classifyByteBuffer(t, info)) return info;
    // The JS-compat family (typed views, buffers, DataView).
    if (classifyFamily(t, info)) return info;
    // The std/collections family (`Map<K, V>` / `Set<T>`).
    if (classifyCollections(t, info)) return info;
    // The `std/date` value type (src/std/date/codegen.cpp).
    if (classifyDate(t, info)) return info;
    if (classifyRegex(t, info)) return info;  // core regex (src/emit/regex.cpp)
    // The builtin EventEmitter (D74): a reference type, so its C++
    // spelling is `Ref<EventEmitter>` (cp3/async/emitter.hpp).
    if (t.name == "EventEmitter") {
        info.shape = Shape::EventEmitter;
        info.representable = true;
        info.cpp = "::cp3::runtime::Ref<::cp3::runtime::EventEmitter>";
        return info;
    }
    // A user class (spec 15): a `Ref<T>` handle over the generated struct. A
    // generic class spells its instantiation (`Box<double>`) from the Type
    // parts; all arguments must have a C++ spelling.
    if (const ClassModel* cm = ModelRegistry::get().classByName(t.name)) {
        if (!cm->typeParams.empty()) {
            if (t.parts.size() != cm->typeParams.size()) return info;
            std::string spelled = cm->cpp + "<";
            for (std::size_t i = 0; i < t.parts.size(); ++i) {
                const CppInfo arg = classify(t.parts[i]);
                if (!arg.representable) return info;
                if (i != 0) spelled += ", ";
                spelled += arg.cpp;
            }
            spelled += ">";
            info.shape = Shape::ClassRef;
            info.representable = true;
            info.cpp = "::cp3::runtime::Ref<" + spelled + ">";
            return info;
        }
        info.shape = Shape::ClassRef;
        info.representable = true;
        info.cpp = classRefCpp(*cm);
        return info;
    }
    // `Promise<T>` is the only other class type with a spelling (M1).
    if (isPromiseType(t)) return classifyPromise(t);
    return info;
}

// A structural object/interface model with a generated spelling (spec 15).
// Interfaces always use the wrapper; a plain object uses its model only when
// the all-primitive value-shape path cannot spell it (master's `$o{...}`
// value structs stay the lowering for pure-primitive literals).
CppInfo classifyObjectModel(const Type& t) {
    CppInfo info;
    ModelRegistry& reg = ModelRegistry::get();
    const ObjectModel* om = reg.objectByLabel(t.name);
    if (om == nullptr || !reg.objectUsable(t.name)) return info;
    if (!objectModelUsed(*om)) return info;
    info.shape = Shape::Object;
    info.representable = true;
    info.type = t;
    info.cpp = om->isInterface ? om->wrapper
                               : "::cp3::runtime::Ref<" + om->structName + ">";
    return info;
}

// A named annotation: primitives, `Promise<T>`, a registered interface name,
// or a class/type name with instantiation arguments.
static std::optional<Type> typeFromNamedType(const NamedType& nt) {
    if (nt.name == "number") return Type::number();
    if (nt.name == "string") return Type::string();
    if (nt.name == "boolean" || nt.name == "bool") return Type::boolean();
    if (nt.name == "void") return Type::voidType();
    if (nt.name == "undefined") return Type::undefined();
    if (nt.name == "null") return Type::nullType();
    if (nt.name == "unknown") return Type::unknown();
    if (nt.name == "Promise") {
        // `Promise<T>` (spec 14); a bare `Promise` is `Promise<unknown>`.
        Type p(TypeKind::Class, "Promise");
        Type inner = Type::unknown();
        if (!nt.typeArgs.empty() && nt.typeArgs.front()) {
            auto mapped = typeFromTypeNode(*nt.typeArgs.front());
            if (!mapped) return std::nullopt;
            inner = *mapped;
        }
        p.parts.push_back(inner);
        return p;
    }
    // An interface name resolves to its structural object type (spec 15);
    // only names registered by the model pass qualify.
    if (const ObjectModel* om =
            ModelRegistry::get().interfaceByName(nt.name)) {
        return Type(TypeKind::Object, om->label);
    }
    Type named = Type::named(TypeKind::Class, nt.name);
    for (const auto& ta : nt.typeArgs) {
        if (!ta) continue;
        auto mapped = typeFromTypeNode(*ta);
        if (!mapped) return std::nullopt;
        named.parts.push_back(*mapped);
    }
    return named;
}

// `{ x: number }`: register the field struct and name it by the sema object
// label so the literal emitter can find it.
static std::optional<Type> typeFromObjectType(const ObjectType& ot) {
    std::vector<ObjectFieldModel> fields;
    for (const auto& mp : ot.members) {
        if (!mp) continue;
        ObjectFieldModel f;
        if (mp->kind == NodeKind::PropertySignature) {
            const auto& ps = static_cast<const PropertySignature&>(*mp);
            f.name = keyNameOf(ps.key.get());
            if (f.name.empty()) continue;
            if (ps.type) f.type = modelTypeFromTypeNode(*ps.type);
            f.optional = ps.optional;
        } else if (mp->kind == NodeKind::MethodSignature) {
            const auto& ms = static_cast<const MethodSignature&>(*mp);
            f.name = keyNameOf(ms.key.get());
            if (f.name.empty()) continue;
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
        } else {
            continue;
        }
        fields.push_back(std::move(f));
    }
    const std::string label = objectLabelOf(fields);
    ModelRegistry::get().ensureObject(label, fields);
    return Type(TypeKind::Object, label);
}

std::optional<Type> typeFromTypeNode(const TypeNode& tn) {
    switch (tn.kind) {
        case NodeKind::NamedType:
            return typeFromNamedType(static_cast<const NamedType&>(tn));
        case NodeKind::ObjectType:
            return typeFromObjectType(static_cast<const ObjectType&>(tn));
        case NodeKind::UnionType: {
            std::vector<Type> members;
            for (const auto& part : static_cast<const UnionType&>(tn).types) {
                if (!part) return std::nullopt;
                auto mapped = typeFromTypeNode(*part);
                if (!mapped) return std::nullopt;
                members.push_back(*mapped);
            }
            return Type::unionOf(std::move(members));
        }
        case NodeKind::ArrayType: {
            const auto& at = static_cast<const ArrayType&>(tn);
            if (!at.element) return std::nullopt;
            auto element = typeFromTypeNode(*at.element);
            if (!element) return std::nullopt;
            return Type::array(*element);
        }
        case NodeKind::ParenthesizedType: {
            const auto& pt = static_cast<const ParenthesizedType&>(tn);
            if (!pt.inner) return std::nullopt;
            return typeFromTypeNode(*pt.inner);
        }
        default:
            return std::nullopt;
    }
}

Type modelTypeFromTypeNode(const TypeNode& tn) {
    if (auto t = typeFromTypeNode(tn)) return *t;
    return Type::unknown();
}

}  // namespace detail
}  // namespace cp3
