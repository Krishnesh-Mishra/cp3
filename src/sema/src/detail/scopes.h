// =============================================================================
// src/sema/src/detail/scopes.h - PRIVATE sema model, scope tables and type helpers.
//
// Declares the implementation-private value model the analyzer's scope and
// declaration tables are expressed in (`Binding`, `FuncSig`, `Shape`, `Field`),
// together with the small helpers over the frozen `Type` (cp3/types.hpp) that
// the analyzer's translation units share. The `Analyzer` class itself is
// declared in `detail/analyzer.h` (STYLE.md §8).
//
// Everything here is implementation detail: this header lives under
// `src/sema/src/detail/` and is never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/source.hpp"
#include "cp3/types.hpp"

namespace cp3 {

// -----------------------------------------------------------------------------
// Small internal type/shape model.
//
// The frozen `Type` (cp3/types.hpp) has no member table for object types, so
// sema keeps an implementation-private shape table keyed by a canonical label
// embedded in `Type::name`. Structurally identical objects share a label, which
// keeps `Type::operator==`/`unify` well-behaved.
// -----------------------------------------------------------------------------
constexpr const char* kAmbientLabel = "$ambient";

// Function-type marker for the variadic array mutators (`push`/`unshift`):
// their parameter list is unbounded (at least one), which the fixed-arity
// `Type::function` cannot express, so we tag the type and widen the
// corresponding signature's `rest` flag in `inferCall`.
constexpr const char* kVariadicLabel = "$variadic";

// Function-type marker for a signature whose *last* declared parameter is
// optional (`slice(start, end?)`, `basename(p, ext?)`, `indexOf(needle,
// from?)`): `Type::function` cannot carry per-parameter optionality, so the
// tag marks the trailing parameter in `sigFromFunctionType` (member_check.cpp).
constexpr const char* kOptionalTailLabel = "$optionalTail";

// Marks the final declared parameter of a function type optional.
inline Type withOptionalTail(Type fn) {
    fn.name = kOptionalTailLabel;
    return fn;
}

// One field of an object/tuple shape (name, value type, `?` optionality).
struct Field {
    std::string name;
    Type type;
    bool optional = false;
};

// A structural object/tuple shape. `open` shapes (ambient / computed / spread)
// accept any property.
struct Shape {
    std::vector<Field> fields;
    std::vector<Type> tupleElems;
    bool tuple = false;
    bool open = false;  // ambient / computed / spread: any property is allowed
};

// A resolved call signature (function declaration or arrow), used for arity and
// parameter type checking and published (minus optionality) as FunctionInfo.
struct FuncSig {
    std::string name;
    std::vector<Type> params;
    std::vector<bool> optional;
    bool rest = false;
    bool annotatedReturn = false;
    Type ret;
    std::uint32_t declNodeId = 0;
    // `async function` / `async () =>` (spec §14): the callable's `ret` is the
    // Promise the call yields, while `asyncValueRet` is the (unwrapped) type the
    // body's `return` statements are checked against. For `Promise<void>` the
    // value type is Void.
    bool isAsync = false;
    Type asyncValueRet;
};

// A name in the scope stack. `publish` is false for builtins / narrowing
// overrides, which are never reported as SymbolInfo.
struct Binding {
    std::string name;
    Type type;
    bool isConst = false;
    bool isFunction = false;
    bool isAmbient = false;
    bool publish = true;  // false for builtins / narrowing overrides
    // `var`-related: `isVar` marks a real `var` declaration, `hoistedVar` the
    // `undefined`-typed placeholder `hoistVars` inserts before the declaration.
    bool isVar = false;
    bool hoistedVar = false;
    std::uint32_t declNodeId = 0;
    SourceSpan span{};
    FuncSig sig;
    // When two or more function declarations share a name in one scope the
    // binding becomes an overload set: `sig` holds only the first and every
    // candidate signature is collected here (size >= 2). Call resolution walks
    // this set and reports CP3416 when none is viable (member_check.cpp).
    std::vector<FuncSig> overloads;
};

// The declaration-shape flags for `Analyzer::declareBinding`, bundled so the
// method stays within the parameter budget (STYLE.md §2).
struct BindingSpec {
    bool isConst = false;
    bool isFunction = false;
    bool isVar = false;       // a `var` declaration (function-scoped)
    bool hoistedVar = false;  // the `undefined` placeholder hoistVars inserts
    bool publish = true;
    std::uint32_t declNodeId = 0;
    const FuncSig* sig = nullptr;  // borrowed; only read to seed Binding::sig
};

// --- free helpers on the frozen Type (defined in type_check.cpp) -------------

bool isAmbientType(const Type& t);
Type ambient();
Type elemOf(const Type& arrayLike);
Type widen(const Type& t);
bool isNumberish(const Type& t);
bool isStringish(const Type& t);
bool isPermissive(const Type& t);
Type stripNullish(const Type& t);
bool isStaticallyNullish(const Type& t);
bool hasNullish(const Type& t);
bool isTupleType(const Type& t);
std::string pretty(const Type& t);
std::string prettyFunction(const Type& t);

// --- the Promise<T> model (spec §14) -----------------------------------------
//
// `Promise<T>` is a nominal class type: kind == Class, name == "Promise",
// parts == { T }. There is no user-visible Promise class in M1, so the type is
// created only by `resolveNamedType`, by `await`'s unwrap, and by async
// function/arrow signatures.
bool isPromiseType(const Type& t);

// `Promise<T>` for a value type `T`.
Type promiseOf(const Type& value);

// The value type of a `Promise<T>` (Unknown for a non-Promise type).
Type promiseValue(const Type& t);

// A canonical key for a type used to build object/tuple shape labels.
std::string typeKey(const Type& t);

// True when `fields` already contains a field named `name` (object-literal
// duplicate suppression, D13).
bool hasFieldNamed(const std::vector<Field>& fields, const std::string& name);

}  // namespace cp3
