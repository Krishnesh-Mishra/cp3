// =============================================================================
// src/sema/src/scopes.h — PRIVATE sema model, scope tables and type helpers.
//
// Declares the implementation-private value model the analyzer's scope and
// declaration tables are expressed in (`Binding`, `FuncSig`, `Shape`, `Field`),
// together with the small helpers over the frozen `Type` (cp3/types.hpp) that
// the analyzer's translation units share. The `Analyzer` class itself is
// declared in `analyzer.h` (STYLE.md §8).
//
// Everything here is implementation detail: this header lives under
// `src/sema/src/` and is never included by another module (STYLE.md §3).
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
bool hasNullish(const Type& t);
bool isTupleType(const Type& t);
std::string pretty(const Type& t);
std::string prettyFunction(const Type& t);

// A canonical key for a type used to build object/tuple shape labels.
std::string typeKey(const Type& t);

// True when `fields` already contains a field named `name` (object-literal
// duplicate suppression, D13).
bool hasFieldNamed(const std::vector<Field>& fields, const std::string& name);

}  // namespace cp3
