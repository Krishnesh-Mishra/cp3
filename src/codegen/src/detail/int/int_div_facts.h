// =============================================================================
// src/codegen/src/detail/int_div_facts.h - PRIVATE divisibility facts of the
// int64 fast path.
//
// A fact `name % k == 0` is a compile-time claim about the value of `name` at
// every later point in the branch that tested it. The fast path (D64) uses it
// to drop the remainder check of an exact division. The facts live in
// `IntMode`; these helpers read and invalidate them.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "cp3/core/ast.hpp"

namespace cp3 {

struct IntMode;

namespace detail {

// A write to `name` invalidates every divisibility fact about it; the guard's
// proof only holds until the value changes. The entry is kept (marked invalid)
// so a branch exit can truncate by depth without an earlier erase shifting a
// live fact out of place.
void dropDivisibleFacts(IntMode* mode, const std::string& name);

// The modulus `k` when `b` is `name / k` and the enclosing branch has proven
// `name % k == 0`; the quotient is then exact, so a plain int64 divide (or a
// shift, for a power of two) has the same result as idiv without its remainder
// check.
std::optional<std::int64_t> divisibleModulus(const BinaryExpr& b,
                                             const IntMode* mode);

// True when the branch runs its statements once, in source order, and nothing
// between the guard and the division can change the guarded name except a
// syntactic write. A branch that defers, repeats or outsources execution
// breaks the fact: a lost fact only costs one remainder check, a missed
// mutation is a miscompile.
bool branchIsFactStable(const Stmt& branch);

}  // namespace detail
}  // namespace cp3
