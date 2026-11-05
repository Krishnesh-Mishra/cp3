// =============================================================================
// cp3/utility_types.hpp - the native utility-type + generic-parameter model.
//
// This header is the small, dependency-light public vocabulary for the three
// "Planned" type features it backs:
//
//   * The built-in utility types Partial / Required / Readonly / Pick / Omit /
//     Record (ROADMAP "Built-in utility types"). They are resolved *in the
//     compiler* (sema, src/sema/src/checks/type/type_check.cpp) to a plain
//     structural `Type` - there is no runtime metadata, no mapped-type
//     syntax and no codegen special case beyond reading the resolved shape
//     the same way codegen reads every other object shape.
//   * Generic type-parameter specs `<T extends X = Y>` (ROADMAP "Generic
//     constraints and defaults"). A `GenericParamSpec` is the resolved form
//     of one `TypeParameter` AST node: its constraint and its default, both
//     already resolved to `Type`. Instantiation checking compares an
//     explicit type argument against the constraint and fills a missing
//     argument from the default (see docs/design/utility-types.md).
//   * `const enum` values and the numeric reverse map (ROADMAP "`const
//     enum` and enum reverse mapping"). A `ConstEnumInfo` is the resolved
//     form of one enum declaration: every member's compile-time value and,
//     for numeric-backed enums, the value -> name reverse map. Sema
//     publishes it in the `SemaResult::constEnums` side table
//     (cp3/core/analysis.hpp); codegen inlines member accesses from it, so
//     a `const enum` emits no runtime object.
//
// Everything here is a plain value type over the frozen `Type`
// (cp3/core/types.hpp): copy it freely, compare with operator== where
// provided. No interning, no shared ownership - the same rules as `Type`
// itself.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cp3/core/types.hpp"

namespace cp3 {

// -----------------------------------------------------------------------------
// Utility types
// -----------------------------------------------------------------------------
// The six built-in utility names. `Record` is the string-indexed record of
// docs/design/record-index-signatures.md; the other five are mapped-type
// combinations over a structural object shape.
enum class UtilityKind {
    Partial,   // Partial<T>  - every field optional
    Required,  // Required<T> - every field required
    Readonly,  // Readonly<T> - every field read-only (same shape, distinct type)
    Pick,      // Pick<T, K>  - keep only the fields named by K
    Omit,      // Omit<T, K>  - drop the fields named by K
    Record,    // Record<K, V> - fields named by K, each of type V
};

// The utility kind spelled by `name`, or std::nullopt when `name` is not one
// of the six built-ins. Resolution checks this *after* the user
// alias/interface/type-name lookups, so a user-declared `Partial` (alias,
// interface or class) shadows the builtin exactly as the existing `Record`
// builtin is shadowed.
inline std::optional<UtilityKind> utilityKindOf(const std::string& name) {
    if (name == "Partial") return UtilityKind::Partial;
    if (name == "Required") return UtilityKind::Required;
    if (name == "Readonly") return UtilityKind::Readonly;
    if (name == "Pick") return UtilityKind::Pick;
    if (name == "Omit") return UtilityKind::Omit;
    if (name == "Record") return UtilityKind::Record;
    return std::nullopt;
}

// True when `name` spells a built-in utility type.
inline bool isUtilityTypeName(const std::string& name) {
    return utilityKindOf(name).has_value();
}

// The number of type arguments the utility takes: 1 for
// Partial/Required/Readonly, 2 for Pick/Omit/Record.
inline constexpr std::size_t utilityArity(UtilityKind kind) noexcept {
    switch (kind) {
        case UtilityKind::Partial:
        case UtilityKind::Required:
        case UtilityKind::Readonly:
            return 1;
        case UtilityKind::Pick:
        case UtilityKind::Omit:
        case UtilityKind::Record:
            return 2;
    }
    return 0;
}

// The canonical spelling of a utility kind ("Partial", ...).
inline const char* utilityKindName(UtilityKind kind) noexcept {
    switch (kind) {
        case UtilityKind::Partial: return "Partial";
        case UtilityKind::Required: return "Required";
        case UtilityKind::Readonly: return "Readonly";
        case UtilityKind::Pick: return "Pick";
        case UtilityKind::Omit: return "Omit";
        case UtilityKind::Record: return "Record";
    }
    return "unknown";
}

// -----------------------------------------------------------------------------
// Generic parameter specs (`<T extends X = Y>`)
// -----------------------------------------------------------------------------
// The resolved form of one type parameter of a generic alias, interface,
// class or function. `constraint` is the resolved `extends` type
// (`Type::unknown()` when the parameter declares none) and `defaultType` is
// the resolved `= Y` type (meaningful only when `hasDefault` is true).
//
// Convention shared with the existing generic-function model: a
// `TypeKind::TypeParam` `Type` carries its constraint in `parts[0]` when
// present. `GenericParamSpec` is the wider record that also carries the
// default, so instantiation checking (arity, constraint, default-fill) can
// be written once against the spec instead of re-deriving it per
// declaration kind.
struct GenericParamSpec {
    std::string name;
    Type constraint = Type::unknown();
    bool hasConstraint = false;
    Type defaultType = Type::unknown();
    bool hasDefault = false;

    friend bool operator==(const GenericParamSpec& a,
                           const GenericParamSpec& b) {
        return a.name == b.name && a.constraint == b.constraint &&
               a.hasConstraint == b.hasConstraint &&
               a.defaultType == b.defaultType && a.hasDefault == b.hasDefault;
    }
    friend bool operator!=(const GenericParamSpec& a,
                           const GenericParamSpec& b) {
        return !(a == b);
    }
};

// -----------------------------------------------------------------------------
// `const enum` values and the reverse map
// -----------------------------------------------------------------------------
// One member's compile-time value. Numeric-backed members carry a number;
// string-backed members carry a string (and `isString` is true, mirroring
// `EnumMemberInfo::isString`). `hasValue` is false only for a member whose
// initializer could not be evaluated as a constant - sema diagnoses that
// separately (CP3401) and such a member never participates in the reverse
// map.
struct ConstEnumValue {
    std::string name;
    bool isString = false;
    double numberValue = 0.0;
    std::string stringValue;
    bool hasValue = false;

    friend bool operator==(const ConstEnumValue& a, const ConstEnumValue& b) {
        return a.name == b.name && a.isString == b.isString &&
               a.numberValue == b.numberValue &&
               a.stringValue == b.stringValue && a.hasValue == b.hasValue;
    }
    friend bool operator!=(const ConstEnumValue& a, const ConstEnumValue& b) {
        return !(a == b);
    }
};

// One entry of the numeric reverse map: the member name a numeric value
// reads back to (`Color[0] == "Red"`). Duplicate numeric values collapse to
// the *last* member with that value, matching TypeScript's reverse mapping
// (each member overwrites the previous entry for the same key).
struct ConstEnumReverseEntry {
    double numberValue = 0.0;
    std::string name;

    friend bool operator==(const ConstEnumReverseEntry& a,
                           const ConstEnumReverseEntry& b) {
        return a.numberValue == b.numberValue && a.name == b.name;
    }
    friend bool operator!=(const ConstEnumReverseEntry& a,
                           const ConstEnumReverseEntry& b) {
        return !(a == b);
    }
};

// The resolved model of one enum declaration, published by sema in
// `SemaResult::constEnums` keyed by the declaration's nodeId. `values` are
// in declaration order; `reverse` is empty for string-backed enums
// (TypeScript parity: string enums have no reverse mapping).
struct ConstEnumInfo {
    std::string name;
    bool isConst = false;       // declared `const enum`
    bool stringBacked = false;  // at least one string member
    std::uint32_t declNodeId = 0;
    std::vector<ConstEnumValue> values;
    std::vector<ConstEnumReverseEntry> reverse;

    // The member with this name, or nullptr when unknown.
    const ConstEnumValue* value(const std::string& member) const {
        for (const ConstEnumValue& v : values) {
            if (v.name == member) return &v;
        }
        return nullptr;
    }

    // The member name a numeric value reads back to, or nullptr when the
    // value is not a member of a numeric-backed enum.
    const std::string* reverseName(double number) const {
        for (const ConstEnumReverseEntry& e : reverse) {
            if (e.numberValue == number) return &e.name;
        }
        return nullptr;
    }
};

}  // namespace cp3
