// =============================================================================
// cp3/types.hpp - the cp3 value-type model.
//
// FROZEN INTERFACE (Beta One, frozen by the contracts pass - DECISIONS D14).
//
// This is the *semantic* type of a cp3 expression/binding, not the C++ spelling.
// sema computes it and publishes it in nodeId-keyed side tables (cp3/analysis.hpp);
// codegen reads it back to choose C++ types and emitter strategies.
//
// Design rules:
//   * `Type` is a small, extensible VALUE type (no interning, no shared
//     ownership). Copy it freely; compare with operator==.
//   * M1 correctness bar: the primitive kinds (Number/String/Boolean/
//     Undefined/Null/Void/Unknown/Error) and the Literal→base rule are exact.
//     Array/Function/Object/Class/Enum/Union/Nullable/TypeParam are *shape-frozen*
//     but their inference/assignability rules are explicitly DEFERRED - see the
//     notes on isAssignable()/unify() below. Deferred rules must stay honest:
//     they never claim two different deferred kinds are compatible.
//
// undefined / null → C++ (M1):
//   * cp3 `undefined`  -> `cp3::runtime::Undefined`, a singleton empty struct;
//     the value is `cp3::runtime::undefined` (see cp3/core/runtime.hpp).
//   * cp3 `null`       -> `std::nullptr_t`, the value is `nullptr`.
//   These two are deliberately DISTINCT (spec §3 keeps `undefined` and `null`
//   separate, TS-style) and both are "nullish" for unification purposes.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3 {

// The kind tag of a Type. Order is not significant; it exists only to group.
enum class TypeKind {
    Error,      // no type because of an earlier error (suppresses cascades)
    Unknown,    // not yet inferred / genuinely unknown (M1: permissive)
    Number,     // 64-bit float (docs/language-spec/23)
    String,     // UTF-8 string
    Boolean,    // true/false
    Undefined,  // the `undefined` value type
    Null,       // the `null` value type
    Void,       // return type of a function that yields nothing
    Array,      // T[]           - parts = { element }
    Function,    // (A, B) => R   - parts = { A, B, R } (return type is last)
    Object,      // structural object literal type - name holds a printable label
    Class,       // nominal class type           - name is the class name
    Enum,        // enum type                    - name is the enum name
    Union,       // A | B | ...   - parts = members
    Nullable,    // T | null | undefined - parts = { T }  (the optional shorthand)
    TypeParam,   // a generic type parameter     - name is the parameter name
    Literal,     // a literal type "data"/42/true - see literalBase + payload
};

// Stable printable name of a kind ("number", "union", ...). Never null.
const char* typeKindName(TypeKind kind) noexcept;

// A cp3 value type.
struct Type {
    TypeKind kind = TypeKind::Unknown;

    // Kind-specific payload. Interpretation per kind:
    //   Array    : { element }
    //   Function : { param0, param1, ..., returnType }  (return type last)
    //   Union    : { member0, member1, ... }            (flattened, distinct)
    //   Nullable : { inner }                            (means inner | null | undefined)
    //   others   : unused in M1
    std::vector<Type> parts;

    // Printable label: class/enum/type-param name, object-type label, etc.
    // Empty for primitives.
    std::string name;

    // --- Literal payload (kind == Literal) -----------------------------------
    // The underlying primitive the literal belongs to (Number/String/Boolean).
    TypeKind literalBase = TypeKind::Unknown;
    double numberValue = 0.0;
    std::string stringValue;
    bool boolValue = false;

    Type() = default;
    explicit Type(TypeKind k) noexcept : kind(k) {}
    Type(TypeKind k, std::string n) : kind(k), name(std::move(n)) {}

    // --- primitive constructors / accessors ---------------------------------
    static Type error();
    static Type unknown();
    static Type number();
    static Type string();
    static Type boolean();
    static Type undefined();
    static Type nullType();  // `null` (named without the reserved-ish spelling)
    static Type voidType();  // `void`

    // --- literal constructors ------------------------------------------------
    static Type numberLiteral(double value);
    static Type stringLiteral(std::string value);
    static Type boolLiteral(bool value);

    // --- compound constructors (shape frozen; inference deferred) ------------
    static Type array(Type element);
    // parts = params... then returnType last.
    static Type function(std::vector<Type> params, Type returnType);
    static Type unionOf(std::vector<Type> members);
    static Type nullable(Type inner);  // inner | null | undefined
    static Type named(TypeKind kind, std::string name);

    // --- predicates ----------------------------------------------------------
    bool isError() const noexcept { return kind == TypeKind::Error; }
    bool isUnknown() const noexcept { return kind == TypeKind::Unknown; }
    bool isNumber() const noexcept { return kind == TypeKind::Number; }
    bool isString() const noexcept { return kind == TypeKind::String; }
    bool isBoolean() const noexcept { return kind == TypeKind::Boolean; }
    bool isUndefined() const noexcept { return kind == TypeKind::Undefined; }
    bool isNull() const noexcept { return kind == TypeKind::Null; }
    bool isVoid() const noexcept { return kind == TypeKind::Void; }
    bool isLiteral() const noexcept { return kind == TypeKind::Literal; }
    bool isUnion() const noexcept { return kind == TypeKind::Union; }
    bool isNullable() const noexcept { return kind == TypeKind::Nullable; }
    // True for Undefined and Null only (the cp3 "nullish" set).
    bool isNullish() const noexcept {
        return kind == TypeKind::Undefined || kind == TypeKind::Null;
    }

    // The primitive a literal belongs to (Number/String/Boolean); for a
    // non-literal Type this returns itself.
    Type literalBaseType() const;

    friend bool operator==(const Type& a, const Type& b) noexcept;
    friend bool operator!=(const Type& a, const Type& b) noexcept { return !(a == b); }
};

// True for the cp3 primitive *value* types: Number, String, Boolean,
// Undefined, Null. (Void/Unknown/Error are not value primitives.)
bool isPrimitive(const Type& t) noexcept;

// Human-readable rendering for diagnostics, e.g. "number",
// "number | string", "string[]", "(number) => boolean".
std::string describe(const Type& t);

// Can a value of type `from` be stored where `to` is expected?
//
// M1 rules (exact):
//   * Error on either side -> true (suppress cascade errors);
//   * Unknown on either side -> true (M1 is permissive about the unknown type);
//   * identical types -> true;
//   * a Literal is assignable iff its base primitive is;
//   * to Union/Nullable -> assignable to any member (Null/Undefined allowed
//     for Nullable);
//   * from Union/Nullable -> every member must be assignable to `to`;
//   * otherwise primitives only match themselves.
// DEFERRED: structural object/function assignability, class subtyping, generics.
// For every deferred kind the honest fallback is "identical kinds only".
bool isAssignable(const Type& from, const Type& to);

// The least upper bound used for inference (e.g. `a ? x : y`, array elements).
//
// M1 rules (exact):
//   * equal types collapse to themselves;
//   * Error absorbs (returns the other side);
//   * Unknown absorbs to the other side;
//   * Void on either side -> void;
//   * a nullish type unified with a non-nullish T -> Nullable(T);
//   * a Literal widens to its base primitive;
//   * otherwise the distinct members become a Union (flattened).
// DEFERRED: numeric widening across integer widths, class hierarchy joins.
Type unify(const Type& a, const Type& b);

}  // namespace cp3
