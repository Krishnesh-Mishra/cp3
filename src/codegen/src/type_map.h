// =============================================================================
// src/codegen/src/type_map.h — PRIVATE cp3-type -> C++-type mapping.
//
// The value-shape model codegen uses: `Shape`/`CppInfo` describe how a cp3
// `Type` (cp3/types.hpp) lowers to a C++ spelling, and `NullishKind` tracks
// which nullish value an empty `std::optional<T>` denotes. Shared by the emit
// units (STYLE.md §8: type_map).
//
// Communication with sema is only through the frozen `Type` value; the
// declaration-type fallbacks here apply when sema is absent or inconclusive.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "cp3/types.hpp"

namespace cp3 {

struct Expr;
struct TypeNode;

namespace detail {

enum class Shape {
    Number, String, Bool, Undefined, Null, Void, Optional, Promise, Unknown,
    Unsupported
};

// Which nullish value an empty `std::optional<T>` denotes. `number | null`,
// `number | undefined` and `number | null | undefined` (the `T?` shorthand,
// folded to TypeKind::Nullable) all lower to `std::optional<T>`, but they print
// and compare differently, so the emitter tracks the flavor.
enum class NullishKind { None, Null, Undefined, Mixed };

bool isPrimitiveShape(Shape s);

struct CppInfo {
    Shape shape = Shape::Unsupported;
    bool representable = false;  // has a concrete C++ spelling we can emit
    std::string cpp;             // the C++ type spelling
    Type type;                   // the normalized cp3 type
    NullishKind emptyKind = NullishKind::None;
};

// Which of `null` / `undefined` a union's empty case denotes (see NullishKind).
NullishKind nullishKindOf(const Type& t);

// The cp3 `Promise<T>` model (spec §14), mirroring sema's: a nominal class
// type with name "Promise" whose single part is the value type.
bool isPromiseType(const Type& t);
Type promiseOf(const Type& value);
Type promiseValue(const Type& t);

// Does an empty optional of `kind` compare strictly equal to `nullishShape`?
bool optionalEmptyMatches(NullishKind kind, Shape nullishShape);

// The C++ lowering of `raw`, including the optional/nullish flavor.
CppInfo classify(const Type& raw);

// Structural fallback: a type annotation -> a cp3 Type (when sema is absent).
std::optional<Type> typeFromTypeNode(const TypeNode& tn);

bool isNullishLiteral(const Expr& e);

// Drops `null`/`undefined` members (used for the `??` result type).
Type stripNullish(const Type& t);

}  // namespace detail
}  // namespace cp3
