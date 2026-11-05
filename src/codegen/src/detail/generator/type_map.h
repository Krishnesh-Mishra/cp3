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

#include "cp3/host/options.hpp"
#include "cp3/core/types.hpp"

namespace cp3 {

struct Expr;
struct TypeNode;

namespace detail {

enum class Shape {
    Number, String, Bool, Undefined, Null, Void, Optional, Maybe, Promise,
    Array,       // number[] / string[] / boolean[] / nested arrays
    Union,       // a non-nullish value union: std::variant (cp3/core/union.hpp)
    TypedArray,  // Int8Array .. BigUint64Array
    ArrayBuffer, // ArrayBuffer / SharedArrayBuffer
    DataView,
    Collections, // Map<K, V> / Set<T> (std/collections)
    Buffer,      // the nominal std/buffer `Buffer` byte sequence
    Enum,        // a named enum (spec 04): `enum class <name> : long long`
    EventEmitter, // the builtin event type (D74): Ref<EventEmitter>
    Listener,     // a function-typed value (D74): the boxed callable
    Function,     // a value-returning function value: std::function<R(Params)>
    ClassRef,     // a user class instance (spec 15): Ref<Class>
    Object,      // an all-primitive `$o{...}` shape: a generated value struct
    Native,      // a Rust/C++ native numeric value (D65): its exact C++ type
    TypeParam,   // a generic function's type parameter (spec §16)
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

// One field of an `$o{...}` object value shape (Shape::Object): its cp3 name
// (also its generated-struct member spelling, modulo mapIdent), the C++
// spelling of its type, and the cp3 type itself (so nested member reads keep
// their shape).
struct ObjectField {
    std::string name;
    std::string cpp;
    Type type;
};

struct CppInfo {
    Shape shape = Shape::Unsupported;
    bool representable = false;  // has a concrete C++ spelling we can emit
    std::string cpp;             // the C++ type spelling
    Type type;                   // the normalized cp3 type
    NullishKind emptyKind = NullishKind::None;
};

// One entry of the declaration-type environment: the binding's cp3 type plus
// whether it lowers to the shared `Cell<T>` representation (a local that is
// captured by a nested frame and reassigned, detail/captures.h). `global` is
// set for a module-scope binding (a namespace-scope global), whose references
// are emitted qualified (`cp3_mod_x::name`) so a name that also exists in the
// global namespace (`log`, from the C math header) cannot become ambiguous in
// `main`, where a `using namespace` is in effect.
struct Binding {
    Type type;
    bool cell = false;
    bool global = false;
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

// A sema tuple type: an Object-kind type whose label starts `$t` and whose
// element types ride in `parts` (type_check.cpp's makeObjectType).
bool isTupleType(const Type& t);

// The C++ spelling of a tuple's element list (`std::tuple<A, B>`), when every
// element carries one (the generic signature sites in emit/decl.cpp).
bool tupleCpp(const Type& t, std::string& out);

// Does an empty optional of `kind` compare strictly equal to `nullishShape`?
bool optionalEmptyMatches(NullishKind kind, Shape nullishShape);

// The C++ lowering of `raw`, including the optional/nullish flavor.
CppInfo classifyPromise(const Type& t);

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
// The C++ spelling of the shape field named `property` (false when absent).
bool fieldCppText(const std::vector<ObjectField>& fields,
                  const std::string& property, std::string& out);
CppInfo classifyObjectValue(const Type& t);
// The class and structural-model arms of `classify` (class_types.cpp): a user
// class / generic instantiation is a `Ref<T>` handle; an interface/object model
// uses its generated wrapper/struct.
CppInfo classifyClassValue(const Type& t);
CppInfo classifyObjectModel(const Type& t);
// `Promise<T>` -> `::cp3::runtime::Promise<T>` (only spellable value types).
CppInfo classifyPromise(const Type& t);
// The general `Promise<T>` spelling: the runtime promise is a template over
// the value type, so every value type cp3 can represent (a primitive, an
// array, an object shape, a class handle) has a promise spelling. The
// `classify` arm keeps the primitive-only rule; this one is the wide form
// (the promise combinators and the array element gate need it).
CppInfo classifyPromiseValue(const Type& t);
// A `$o{...}` label another family owns (a std/json interface record): its
// spelling is that family's, so the value-shape path must not claim it.
// Registered before the emit walk (std/json's collectJsonShapes).
void objectShapeExclude(const std::string& label);
// Does `t` denote an object value shape, directly or as the single non-nullish
// member of a nullish option (`{...} | null` -> `std::optional<Struct>`)?
// Fills `object` with the object `Type` and reports whether it was peeled.
bool objectReceiver(const Type& t, Type& object, bool& optional);
// The registered struct texts whose names occur in `text`, each inside its
// `CP3_OBJ_<HEX>_DEFINED` guard for emission at global scope: the worker TU and
// the entry TU spell the same `$o{...}` shape as one nominal type.
std::string objectGlobalDeclsFor(const std::string& text, bool cloneValues);

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
// The Stopwatch value type (std/time).
bool classifyStopwatch(const Type& t, CppInfo& info);
// TextEncoder/TextDecoder (std/text).
bool classifyText(const Type& t, CppInfo& info);
// Logger (std/log).
bool classifyLogger(const Type& t, CppInfo& info);

// The core regex value type (D48; src/emit/regex.cpp): a nominal class named
// "RegExp" lowering to `::cp3::runtime::regex::Regex`. Returns false for every
// other type.
bool classifyRegex(const Type& t, CppInfo& info);

// The nominal `Buffer` of std/buffer (src/std/buffer/codegen.cpp): a
// `TypeKind::Class` named "Buffer" -> `::cp3::runtime::buffer::Buffer`.
bool classifyByteBuffer(const Type& t, CppInfo& info);

// Structural fallback: a type annotation -> a cp3 Type (when sema is absent).
// `typeSystem` enables the project's plain Rust/C++ value-type names.
std::optional<Type> typeFromTypeNode(const TypeNode& tn,
                                     TypeSystem typeSystem =
                                         TypeSystem::TypeScript);

// The annotation's cp3 type, or `unknown` when it has no mapping; the model
// pass (support/class_walk.cpp) uses this where an annotation must always
// yield a Type value.
Type modelTypeFromTypeNode(const TypeNode& tn);

// The instantiated shape of a generic interface (D26): the type arguments of
// the named type are substituted into the interface's members and the shape is
// registered as an interface model (the wrapper is what makes the structural
// typing work). nullopt when the annotation does not name a generic interface.
std::optional<Type> instantiateGenericInterface(const TypeNode& tn);

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

// `null`, `undefined`, or a union of only nullish members: a value that is
// always nullish, so `??` can drop it outright.
bool isStaticallyNullish(const Type& t);

}  // namespace detail
}  // namespace cp3
