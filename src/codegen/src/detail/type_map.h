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
#include <vector>

#include "cp3/types.hpp"

namespace cp3 {

struct Expr;
struct TypeNode;

namespace detail {

enum class Shape {
    Number, String, Bool, Undefined, Null, Void, Optional, Maybe, Promise,
    Array,       // number[] / string[] / boolean[] / nested arrays
    Union,       // a non-nullish value union (`number | string` -> std::variant)
    TypedArray,  // Int8Array .. BigUint64Array
    ArrayBuffer, // ArrayBuffer / SharedArrayBuffer
    DataView,
    Collections, // Map<K, V> / Set<T> (std/collections)
    Buffer,      // the nominal std/buffer `Buffer` byte sequence
    Enum,        // a named enum (spec 04): `enum class <name> : long long`
    EventEmitter, // the builtin event type (D74): Ref<EventEmitter>
    Listener,     // a function-typed value (D74): the boxed callable
    Union,       // A | B over primitives: std::variant (cp3/core/union.hpp)
    Object,      // an all-primitive `$o{...}` shape: a generated value struct
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

// One field of an all-primitive `$o{...}` object value shape (Shape::Object):
// its cp3 name (also its generated-struct member spelling, modulo mapIdent)
// and the C++ spelling of its primitive type.
struct ObjectField {
    std::string name;
    std::string cpp;
};

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

// The builtin `EventEmitter` (D74): a nominal class type with name
// "EventEmitter"; codegen lowers it to cp3/async/emitter.hpp.
bool isEventEmitterType(const Type& t);

// Does an empty optional of `kind` compare strictly equal to `nullishShape`?
bool optionalEmptyMatches(NullishKind kind, Shape nullishShape);

// The C++ lowering of `raw`, including the optional/nullish flavor.
CppInfo classify(const Type& raw);

// The enum arm of `classify` (spec 04; src/emit/enum.cpp): a named enum ->
// `enum class <name> : long long`. Runs only for a `TypeKind::Enum` type.
CppInfo classifyEnum(const Type& t);

// `A | B` over primitives -> the canonical `std::variant<double, std::string
// [, bool]>`; and `T | null` / `T | undefined` / `T?` over a primitive or an
// object value shape -> `std::optional<T>` (the two-flavor primitive case
// keeps `Maybe<T>`). The two Union arms of `classify` (value_shapes.cpp).
void classifyValueUnion(const Type& t, CppInfo& info);
void classifyNullishUnion(const Type& t, CppInfo& info);

// The C++ spelling of an all-primitive `$o{...}` object shape: fills the
// generated struct name and its fields and registers the struct text for
// assembly. False when any field is optional/non-primitive or the shape is
// not a plain object label (tuples, ambient objects, interfaces, or a label
// another family claimed through `objectShapeExclude`).
bool objectShapeOf(const Type& t, std::string& structName,
                   std::vector<ObjectField>& fields);
CppInfo classifyObjectValue(const Type& t);
// A `$o{...}` label another family owns (a std/json interface record): its
// spelling is that family's, so the value-shape path must not claim it.
// Registered before the emit walk (std/json's collectJsonShapes).
void objectShapeExclude(const std::string& label);
// Does `t` denote an object value shape, directly or as the single non-nullish
// member of a nullish option (`{...} | null` -> `std::optional<Struct>`)?
// Fills `object` with the object `Type` and reports whether it was peeled.
bool objectReceiver(const Type& t, Type& object, bool& optional);
// The registered struct texts whose names occur in `text` (the decls prefix a
// unit adds for the shapes it references).
std::string objectDeclsFor(const std::string& text);

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
