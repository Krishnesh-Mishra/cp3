// =============================================================================
// src/codegen/src/support/type_classify.cpp - Type -> CppInfo classification.
//
// The value-shape mapping shared by every emit unit: primitives, native
// Rust/C++ value types (D65), nullish options, primitive unions, object value
// shapes and the class families. `type_map.cpp` owns the AST side
// (`typeFromTypeNode`) and the shared helpers.
// =============================================================================
#include <string>

#include "cp3/native_types.hpp"

#include "detail/parallel.h"
#include "detail/type_map.h"

namespace cp3 {
namespace detail {

// `Promise<T>` -> `::cp3::runtime::Promise<T>` (cp3/async/promise.hpp). Only
// M1-representable value types (primitives and `void`) have a spelling;
// anything else stays not-implemented. Implementation detail of `classify`.
static CppInfo classifyPromise(const Type& t) {
    CppInfo info;
    const Type inner = promiseValue(t);
    if (inner.isVoid()) {
        info.shape = Shape::Promise; info.representable = true;
        info.cpp = "::cp3::runtime::Promise<void>";
        return info;
    }
    const CppInfo ic = classify(inner);
    if (ic.representable && isPrimitiveShape(ic.shape)) {
        info.shape = Shape::Promise; info.representable = true;
        info.cpp = "::cp3::runtime::Promise<" + ic.cpp + ">";
    }
    return info;
}

// The single-member nullable over a primitive: `T | null`, `T | undefined`
// and the `T?` shorthand all lower to `std::optional<T>` (the flavor is kept
// in `emptyKind`). Implementation detail of `classify`.
static void classifyNullable(const Type& t, CppInfo& info) {
    if (t.parts.size() != 1) return;
    const CppInfo inner = classify(t.parts.front());
    if (!inner.representable || !isPrimitiveShape(inner.shape)) return;
    info.shape = Shape::Optional;
    info.representable = true;
    info.cpp = "std::optional<" + inner.cpp + ">";
    info.emptyKind = NullishKind::Mixed;
}

// The Class-kind tail of `classify`: the family spellings (Buffer, typed
// views, Map/Set, Date), then the builtin EventEmitter (D74), then `Promise<T>`
// (the only other class with a spelling). Anything else stays out of M1.
static CppInfo classifyClassValue(const Type& t) {
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
    // `Promise<T>` is the only other class type with a spelling (M1).
    if (isPromiseType(t)) return classifyPromise(t);
    return info;
}

CppInfo classify(const Type& raw) {
    CppInfo info;
    Type t = raw;
    if (t.isLiteral()) {
        t = t.literalBaseType();
    }
    info.type = t;
    // The parallel.run result marker must be caught before the Array arm:
    // classifyFamily keys on the element type alone and would spell it
    // `Array<double>`. Keeping it non-representable leaves the binding `auto`.
    if (isParallelRunType(t)) {
        info.shape = Shape::Unknown;
        return info;
    }
    switch (t.kind) {
        case TypeKind::Number:
            info.shape = Shape::Number; info.representable = true; info.cpp = "double";
            return info;
        case TypeKind::String:
            info.shape = Shape::String; info.representable = true; info.cpp = "std::string";
            return info;
        case TypeKind::Boolean:
            info.shape = Shape::Bool; info.representable = true; info.cpp = "bool";
            return info;
        case TypeKind::Native: {
            // A declared Rust/C++ numeric value (D65): the exact C++ spelling
            // the table resolved, never a double round-trip.
            const NativeTypeInfo* row = nativeById(t.name);
            if (row == nullptr) return info;
            info.shape = Shape::Native;
            info.representable = true;
            info.cpp = row->cpp;
            return info;
        }
        case TypeKind::Undefined:
            info.shape = Shape::Undefined; info.representable = true;
            info.cpp = "cp3::runtime::Undefined";
            return info;
        case TypeKind::Null:
            info.shape = Shape::Null; info.representable = true; info.cpp = "std::nullptr_t";
            return info;
        case TypeKind::Void:
            info.shape = Shape::Void; info.representable = true; info.cpp = "void";
            return info;
        case TypeKind::Unknown:
        case TypeKind::Error:
            info.shape = Shape::Unknown; info.representable = false;
            return info;
        case TypeKind::Nullable:
            classifyNullable(t, info);
            return info;
        case TypeKind::Union:
            classifyValueUnion(t, info);
            if (!info.representable) classifyNullishUnion(t, info);
            return info;
        case TypeKind::Object:
            // A structural `$o{...}` shape: an all-primitive object becomes a
            // generated value struct (support/value_shapes.cpp).
            return classifyObjectValue(t);
        case TypeKind::Class:
            return classifyClassValue(t);
        case TypeKind::Enum: return classifyEnum(t);  // spec 04: enum class
        case TypeKind::Function:
            // A function-typed value (D74): boxed as `Listener` so every copy
            // shares one identity; only void-returning functions have one.
            if (t.parts.empty() || !t.parts.back().isVoid()) return info;
            info.shape = Shape::Listener;
            info.representable = true;
            info.cpp = "::cp3::runtime::Listener";
            return info;
        case TypeKind::Array:
            // Regular arrays have a spelling when their element type does
            // (src/std/typed-arrays/codegen.cpp); otherwise they stay out of
            // M1.
            classifyFamily(t, info);
            return info;
        default:
            return info;  // Object/TypeParam: out of M1
    }
}

}  // namespace detail
}  // namespace cp3
