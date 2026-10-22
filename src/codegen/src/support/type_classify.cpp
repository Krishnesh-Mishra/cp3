// =============================================================================
// src/codegen/src/support/type_classify.cpp - Type -> CppInfo classification.
//
// The value-shape mapping shared by every emit unit: primitives, native
// Rust/C++ value types (D65), nullish options, primitive unions, object value
// shapes and the class families. `type_map.cpp` owns the AST side
// (`typeFromTypeNode`) and the shared helpers.
// =============================================================================
#include <string>
#include <utility>
#include <vector>

#include "cp3/native_types.hpp"

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/parallel.h"
#include "detail/type_map.h"

namespace cp3 {
namespace detail {

bool isTupleType(const Type& t) {
    return t.kind == TypeKind::Object && t.name.rfind("$t", 0) == 0;
}

// The C++ spelling of a tuple's element list (`std::tuple<A, B>`), when every
// element carries one; used for generic tuple signatures (emit/decl.cpp).
bool tupleCpp(const Type& t, std::string& out) {
    std::string cpp = "std::tuple<";
    for (std::size_t i = 0; i < t.parts.size(); ++i) {
        const CppInfo ec = classify(t.parts[i]);
        if (!ec.representable) return false;
        if (i != 0) cpp += ", ";
        cpp += ec.cpp;
    }
    out = std::move(cpp) + ">";
    return true;
}

// `Promise<T>` -> `::cp3::runtime::Promise<T>` (cp3/async/promise.hpp). The
// promise carries a value of type `T`, so every representable value type has
// a spelling: a class handle, an object, a tuple, an optional, ... The one
// exception is a promise of a promise (`Promise<Promise<T>>` has no cp3
// meaning). Implementation detail of `classify`.
CppInfo classifyPromise(const Type& t) {
    CppInfo info;
    const Type inner = promiseValue(t);
    if (inner.isVoid()) {
        info.shape = Shape::Promise; info.representable = true;
        info.cpp = "::cp3::runtime::Promise<void>";
        return info;
    }
    const CppInfo ic = classify(inner);
    if (ic.representable && ic.shape != Shape::Promise) {
        info.shape = Shape::Promise; info.representable = true;
        info.cpp = "::cp3::runtime::Promise<" + ic.cpp + ">";
    }
    return info;
}

// The single-member nullable over a primitive or a class/object handle:
// `T | null`, `T | undefined` and the `T?` shorthand all lower to
// `std::optional<T>` (the flavor is kept in `emptyKind`). Implementation
// detail of `classify`.
static void classifyNullable(const Type& t, CppInfo& info) {
    if (t.parts.size() != 1) return;
    const CppInfo inner = classify(t.parts.front());
    if (!inner.representable) return;
    if (!isPrimitiveShape(inner.shape) && inner.shape != Shape::ClassRef &&
        inner.shape != Shape::Object) {
        return;
    }
    info.shape = Shape::Optional;
    info.representable = true;
    info.cpp = "std::optional<" + inner.cpp + ">";
    info.emptyKind = NullishKind::Mixed;
}

// The Function-kind arm of `classify`: a void function over non-representable
// parameters is the boxed Listener (D74); every other function is a
// `std::function` built from the recursive shapes of its parts.
static CppInfo classifyFunctionValue(const Type& t) {
    CppInfo info;
    info.type = t;
    if (t.parts.empty()) return info;
    // A void-returning function whose parameters have no value shape
    // (an event handler's `unknown` payload) is the boxed `Listener`
    // (D74): every copy shares one identity. A value-returning (or
    // representable-parameter) function is a plain `std::function`
    // built from the recursive shapes of its parts.
    bool listener = t.parts.back().isVoid();
    for (std::size_t i = 0; i + 1 < t.parts.size(); ++i) {
        if (classify(t.parts[i]).representable) {
            listener = false;
            break;
        }
    }
    if (listener) {
        info.shape = Shape::Listener;
        info.representable = true;
        info.cpp = "::cp3::runtime::Listener";
        return info;
    }
    std::vector<std::string> params;
    for (std::size_t i = 0; i + 1 < t.parts.size(); ++i) {
        const CppInfo part = classify(t.parts[i]);
        if (!part.representable) return info;
        params.push_back(part.cpp);
    }
    const CppInfo ret = classify(t.parts.back());
    if (!ret.representable) return info;
    info.shape = Shape::Function;
    info.representable = true;
    info.cpp = "std::function<" + ret.cpp + "(" +
               joinStrings(params, ", ") + ")>";
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
        case TypeKind::Object: {
            // A tuple annotation (`$t[...]`) is a std::tuple value: unlike an
            // object shape it has no nominal struct and no Ref handle, so it
            // must be classified before the object-model arms.
            if (isTupleType(t)) {
                std::string cpp;
                bool primitives = !t.parts.empty();
                for (const Type& part : t.parts) {
                    if (!isPrimitiveShape(classify(part).shape)) primitives = false;
                }
                if (primitives && tupleCpp(t, cpp)) {
                    info.type = t;
                    info.shape = Shape::Object;
                    info.representable = true;
                    info.cpp = std::move(cpp);
                }
                return info;
            }
            // A registered interface/structural model first; the all-primitive
            // `$o{...}` value shape (support/value_shapes.cpp) otherwise.
            CppInfo model = classifyObjectModel(t);
            if (model.representable) return model;
            return classifyObjectValue(t);
        }
        case TypeKind::Class:
            return classifyClassValue(t);
        case TypeKind::Enum: return classifyEnum(t);  // spec 04: enum class
        case TypeKind::TypeParam:
            // A generic field/function type: the C++ spelling is the template
            // parameter itself (`T`), usable anywhere a value type is.
            info.type = t;
            info.shape = Shape::TypeParam;
            info.representable = true;
            info.cpp = mapIdent(t.name);
            return info;
        case TypeKind::Function:
            return classifyFunctionValue(t);
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
