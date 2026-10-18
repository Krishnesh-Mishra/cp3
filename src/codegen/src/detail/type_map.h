// =============================================================================
// src/codegen/src/detail/type_map.h - PRIVATE cp3-type -> C++-type mapping.
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

#include <cstdint>
#include <optional>
#include <string>

#include "cp3/types.hpp"

namespace cp3 {

struct Expr;
struct TypeNode;

namespace detail {

enum class Shape {
    Number, String, Bool, Undefined, Null, Void, Optional, Maybe, Promise,
    Array,       // number[] / string[] / boolean[] / nested arrays
    TypedArray,  // Int8Array .. BigUint64Array
    ArrayBuffer, // ArrayBuffer / SharedArrayBuffer
    DataView,
    Collections, // Map<K, V> / Set<T> (std/collections)
    Buffer,      // the nominal std/buffer `Buffer` byte sequence
    Unknown, Unsupported
};

// Which nullish value an empty `std::optional<T>` denotes. `number | null`,
// `number | undefined` and `number | null | undefined` (the `T?` shorthand,
// folded to TypeKind::Nullable) all lower to `std::optional<T>`, but they print
// and compare differently, so the emitter tracks the flavor. The explicit
// three-way union `T | null | undefined` needs both flavors at once and lowers
// to `cp3::runtime::Maybe<T>` instead (Shape::Maybe).
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

// The JS-compat typed-array family (D69; src/std/typed-arrays/codegen.cpp):
// regular arrays with a value-like element type (`number`, `string`,
// `boolean` or a nested array), the 11 typed views, the buffer classes and
// DataView. Fills `info` and returns true when `t` is one of them; every other
// type is left for `classify`'s own arms.
bool classifyFamily(const Type& t, CppInfo& info);

// The std/collections family (src/std/collections/codegen.cpp): `Map<K, V>`
// and `Set<T>` over primitive keys/values lower to the runtime's
// insertion-ordered collection handles. Fills `info` and returns true when
// `t` is one of them; every other type is left for `classify`'s own arms.
bool classifyCollections(const Type& t, CppInfo& info);

// The Date value type (std/date; src/std/date/codegen.cpp): a nominal class
// named "Date" lowering to `::cp3::runtime::date::Date`. Returns false for
// every other type.
bool classifyDate(const Type& t, CppInfo& info);

// The nominal `Buffer` of std/buffer (src/std/buffer/codegen.cpp): a
// `TypeKind::Class` named "Buffer" -> `::cp3::runtime::buffer::Buffer`.
bool classifyByteBuffer(const Type& t, CppInfo& info);

// Structural fallback: a type annotation -> a cp3 Type (when sema is absent).
std::optional<Type> typeFromTypeNode(const TypeNode& tn);

bool isNullishLiteral(const Expr& e);

// The exact value of an integral number literal (`5`, `-2`), if it has one:
// finite, integral, and within 2^53, so it is exact in both the int64 and the
// double domain. Anything else (a fractional literal, an out-of-range literal,
// a non-literal expression) has no value, which keeps the caller on the
// `std::fmod` path. Used by the `%` lowering (`modConst<K>`) and the int64 fast
// path (`imulConst<K>`).
std::optional<std::int64_t> integralLiteralValue(const Expr& e);

// Drops `null`/`undefined` members (used for the `??` result type).
Type stripNullish(const Type& t);

}  // namespace detail
}  // namespace cp3
