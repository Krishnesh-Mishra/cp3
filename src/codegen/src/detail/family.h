// =============================================================================
// src/codegen/src/detail/family.h - PRIVATE family data for the JS-compat
// typed-array family (D69).
//
// Shared by src/std/typed-arrays/codegen.cpp and codegen_methods.cpp: the class
// table, the tag / constructor-name lookups, the array-element shape gate and
// the `Array(...)` constructor recognizer.
// =============================================================================
#pragma once

#include <string>

#include "cp3/ast.hpp"
#include "detail/type_map.h"

namespace cp3 {
namespace detail {

// One family class: its JS name, its tag spelling ("" when it is not a typed
// view) and its element size in bytes.
struct FamilyClass {
    const char* cls;
    const char* tag;
    int bytes;
};

inline constexpr FamilyClass kFamilyClasses[] = {
    {"Int8Array", "Int8Tag", 1},
    {"Uint8Array", "Uint8Tag", 1},
    {"Uint8ClampedArray", "Uint8ClampedTag", 1},
    {"Int16Array", "Int16Tag", 2},
    {"Uint16Array", "Uint16Tag", 2},
    {"Int32Array", "Int32Tag", 4},
    {"Uint32Array", "Uint32Tag", 4},
    {"Float32Array", "Float32Tag", 4},
    {"Float64Array", "Float64Tag", 8},
    {"BigInt64Array", "BigInt64Tag", 8},
    {"BigUint64Array", "BigUint64Tag", 8},
    {"ArrayBuffer", "", 0},
    {"SharedArrayBuffer", "", 0},
    {"DataView", "", 0},
};

inline const FamilyClass* familyClass(const std::string& name) {
    for (const FamilyClass& fc : kFamilyClasses) {
        if (name == fc.cls) return &fc;
    }
    return nullptr;
}

// A typed-view class name -> its tag spelling (null for the other classes).
inline const char* familyTag(const std::string& name) {
    const FamilyClass* fc = familyClass(name);
    return fc != nullptr && fc->tag[0] != '\0' ? fc->tag : nullptr;
}

inline bool isFamilyCtorName(const std::string& name) {
    return name == "Array" || familyClass(name) != nullptr;
}

// The element shapes `Array<E>` has a C++ spelling for (elements recurse).
// Class instances, structural objects and interface wrappers are reference
// handles / small wrappers (spec 15), so they are storable in cp3 arrays too.
//
// The gate is "can a `std::vector<E>` hold one", not "is it a primitive":
// the value-like wrappers and handles with a C++ spelling (a promise, an
// optional, a typed view, a buffer, a DataView, a boxed listener) are
// copyable values too, so an array of them is a statically typed array like
// any other (the spec's "no fully dynamic array" rule is about the *element
// type* being known, not about the element being a primitive).
inline bool isArrayElementShape(Shape s) {
    return s == Shape::Number || s == Shape::String || s == Shape::Bool ||
           s == Shape::Array || s == Shape::Enum || s == Shape::Union ||
           s == Shape::Native || s == Shape::Function ||
           s == Shape::ClassRef || s == Shape::Object ||
           s == Shape::TypeParam ||
           s == Shape::Promise || s == Shape::Optional || s == Shape::Maybe ||
           s == Shape::TypedArray || s == Shape::ArrayBuffer ||
           s == Shape::DataView || s == Shape::Listener;
}

// The array-element spelling of `elem` (classify + shape gate).
inline bool arrayElementInfo(const Type& elem, CppInfo& ec) {
    ec = classify(elem);
    return ec.representable && isArrayElementShape(ec.shape);
}

// The element of an `Array<E>` type (Unknown for anything else).
inline Type arrayElemOf(const Type& t) {
    return t.kind == TypeKind::Array && !t.parts.empty() ? t.parts.front()
                                                         : Type::unknown();
}

// The `Array(...)` constructor's argument list when `e` is such a call (with
// or without `new`); null otherwise.
inline const ExprList* arrayCtorArgs(const Expr& e) {
    const Expr* callee = nullptr;
    if (e.kind == NodeKind::CallExpr) {
        callee = static_cast<const CallExpr&>(e).callee.get();
    } else if (e.kind == NodeKind::NewExpr) {
        callee = static_cast<const NewExpr&>(e).callee.get();
    } else {
        return nullptr;
    }
    if (callee == nullptr || callee->kind != NodeKind::Identifier ||
        static_cast<const Identifier&>(*callee).name != "Array") {
        return nullptr;
    }
    if (e.kind == NodeKind::CallExpr) {
        return &static_cast<const CallExpr&>(e).arguments;
    }
    return &static_cast<const NewExpr&>(e).arguments;
}

}  // namespace detail
}  // namespace cp3
