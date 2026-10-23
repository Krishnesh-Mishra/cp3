// =============================================================================
// cp3/native_types.hpp - the native type-name vocabulary (spec 03, D65).
//
// A native type is a plain value type borrowed from Rust or C++ by name. The
// canonical identity lives in `Type::name` (TypeKind::Native); this header owns
// the spelling table shared by sema (resolution, widening) and codegen (the
// exact C++ spelling). Numbers, strings and booleans keep their ordinary cp3
// types: `r_'f64'` is `number`, `c_'double'` is `number`, `r_'bool'` is
// `boolean`.
//
// The table is deliberately closed: a spelling that is not listed gets a
// precise diagnostic from `resolveNativeTypeName` / the sema caller, never a
// silent `unknown`. Pointer, reference and array spellings are rejected before
// the table is consulted (spec 03: prefixed names are plain value types only).
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "cp3/host/options.hpp"
#include "cp3/core/types.hpp"

namespace cp3 {

// How a native value behaves in arithmetic.
enum class NativeClass { Integer, Float };

// One supported native value type. `id` is the canonical Type::name; `cpp` is
// the exact spelling codegen emits.
struct NativeTypeInfo {
    std::string id;
    std::string cpp;
    NativeClass cls = NativeClass::Integer;
    int bits = 0;       // value bits (platform-width long values are resolved)
    bool isSigned = false;
    int rank = 0;       // C++ conversion rank: char 10, short 20, int 30,
                        // long 40, long long 50
};

// Why a spelling did not resolve.
enum class NativeNameStatus {
    Ok,
    Unknown,      // no such type name in that vocabulary
    Unsupported,  // a real type name, but with no cp3/C++ value representation
};

// The result of resolving one spelling. `type` is set only for Ok; `error`
// carries a complete diagnostic message (including a did-you-mean) otherwise.
struct NativeNameResult {
    NativeNameStatus status = NativeNameStatus::Unknown;
    std::optional<Type> type;
    std::string error;
};

// The table row for a canonical native id, or nullptr for an unknown id.
const NativeTypeInfo* nativeById(const std::string& id) noexcept;

bool isNativeType(const Type& t) noexcept;
bool isNativeInteger(const Type& t) noexcept;
bool isNativeFloat(const Type& t) noexcept;

// Resolves an inline prefix ("r", "c" or "ts") plus its quoted spelling.
NativeNameResult resolveNativeTypeName(const std::string& prefix,
                                       const std::string& spelling);

// Resolves a plain type name under a Rust or C++ project vocabulary. A
// TypeScript project resolves nothing here (its ordinary names win).
NativeNameResult resolvePlainNativeTypeName(TypeSystem ts,
                                            const std::string& spelling);

// The parser's multi-word C++ name support: `let a: long long = 5;`. True when
// extending `current` with `next` keeps (a prefix of) a supported C++ spelling.
bool isCppTypeContinuation(const std::string& current, const std::string& next);

// The C++ usual-arithmetic-conversion result of two native operands, or
// nullopt when either side is not a native type. Integral promotions apply, so
// `u8 + u8` is `i32` and `u32 + i64` is `i64`, exactly as C++ computes them.
std::optional<Type> nativeArithmeticResult(const Type& l, const Type& r);

// True when the literal `value` can be stored in `native` without losing its
// value: an integral value for an integer type (range-checked), any finite
// value for a float.
bool nativeCanRepresentLiteral(const Type& native, double value);

}  // namespace cp3
