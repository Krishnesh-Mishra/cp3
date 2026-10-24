// =============================================================================
// src/sema/src/checks/native_types.cpp - the native type-system tier (D65).
//
// `r_'...'` / `c_'...'` / `ts_'...'` prefix resolution and the native numeric
// operator rules. The shared name table and the C++ conversion model live in
// src/common (cp3/native_types.hpp); this unit owns the sema policy: the
// pointer/reference rejection, the nullish-union rule, strictNumericCasts,
// and the integral-promotion result.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"

namespace cp3 {

namespace {

bool numericOperand(const Type& t) {
    return isNativeType(t) || isNumberish(t);
}

}  // namespace

Type Analyzer::resolvePrefixedType(PrefixedType& pt) {
    // Spec 03: a prefixed name is a plain value type, never a pointer,
    // reference or array spelling.
    if (pt.name.find_first_of("*&[]") != std::string::npos) {
        report(diagcode::kUnknownTypeArgument, pt.span,
               "raw pointer, reference, and array types are not permitted in " +
                   pt.prefix + "_'...'");
        return Type::unknown();
    }
    // TypeScript reaches through the same escape hatch in a rust/cpp project.
    // `any` keeps the banned-type diagnostic; `object` is the permissive
    // ambient shape (no generated value type is needed).
    if (pt.prefix == "ts") {
        if (pt.name == "any") {
            checkBannedAny(pt.span);
            return ambient();
        }
        if (pt.name == "object") return ambient();
    }
    NativeNameResult native = resolveNativeTypeName(pt.prefix, pt.name);
    if (native.type) return *native.type;
    report(diagcode::kUnknownType, pt.span, native.error);
    return Type::unknown();
}

void Analyzer::rejectNativeUnion(const Type& result, const SourceSpan& span) {
    // Codegen lowers a nullish option over one native value type
    // (`r_'u32' | undefined` -> std::optional, `T?` -> Maybe). A non-nullish
    // variant with a native alternative has no stable alternative ordering and
    // no numeric coercion, so refuse it here with a real diagnostic rather
    // than letting the declared type reach codegen without a sound lowering.
    if (!result.isUnion()) return;
    std::size_t nonNullish = 0;
    bool hasNative = false;
    for (const Type& part : result.parts) {
        if (part.isNullish()) continue;
        ++nonNullish;
        if (isNativeType(part)) hasNative = true;
    }
    if (hasNative && nonNullish >= 2) {
        report(diagcode::kTypeMismatch, span,
               "type mismatch: a native value type cannot share a "
               "non-nullish union; convert to a common type first");
    }
}

std::optional<Type> Analyzer::evalNativeBinary(BinaryOp op, const Type& ln,
                                               const Type& rn,
                                               const SourceSpan& span) {
    if (!isNativeType(ln) && !isNativeType(rn)) return std::nullopt;
    const Type& native = isNativeType(ln) ? ln : rn;
    if (op == BinaryOp::Power) {
        report(diagcode::kInvalidOperandTypes, span,
               "the native type '" + pretty(native) +
                   "' has no '**' operator; convert it to number first");
        return Type::error();
    }
    if (op == BinaryOp::UnsignedShiftRight) {
        report(diagcode::kInvalidOperandTypes, span,
               "the native type '" + pretty(native) +
                   "' has no '>>>' operator; use '>>' or a number value");
        return Type::error();
    }
    // Comparisons and equality consume numeric operands and produce boolean;
    // only arithmetic/bitwise/shift feed the native result rule.
    const bool comparisonOp =
        op == BinaryOp::LessThan || op == BinaryOp::GreaterThan ||
        op == BinaryOp::LessThanEqual || op == BinaryOp::GreaterThanEqual ||
        op == BinaryOp::Equal || op == BinaryOp::NotEqual ||
        op == BinaryOp::StrictEqual || op == BinaryOp::StrictNotEqual ||
        op == BinaryOp::In;
    if (comparisonOp) return std::nullopt;
    if (isNativeType(ln) && isNativeType(rn)) {
        if (options_.strictNumericCasts && ln.name != rn.name) {
            report(diagcode::kMixedTypeArithmetic, span,
                   "strictNumericCasts refuses to mix '" + pretty(ln) +
                       "' and '" + pretty(rn) +
                       "'; convert one side explicitly");
            return Type::error();
        }
        if (auto widened = nativeArithmeticResult(ln, rn)) return *widened;
        report(diagcode::kMixedTypeArithmetic, span,
               "type mismatch: unsupported native arithmetic on '" +
                   pretty(ln) + "' and '" + pretty(rn) + "'");
        return Type::error();
    }
    if (!numericOperand(ln) || !numericOperand(rn)) return std::nullopt;
    if (options_.strictNumericCasts) {
        report(diagcode::kMixedTypeArithmetic, span,
               "strictNumericCasts refuses to mix '" + pretty(ln) + "' and '" +
                   pretty(rn) + "'; convert one side explicitly");
        return Type::error();
    }
    const bool integralOnly =
        op == BinaryOp::Modulo || op == BinaryOp::BitAnd ||
        op == BinaryOp::BitOr || op == BinaryOp::BitXor ||
        op == BinaryOp::ShiftLeft || op == BinaryOp::ShiftRight;
    if (integralOnly) {
        report(diagcode::kInvalidOperandTypes, span,
               "the native operator '" + binaryText(op) +
                   "' requires integral operands; convert the number side "
                   "explicitly");
        return Type::error();
    }
    return Type::number();
}

}  // namespace cp3
