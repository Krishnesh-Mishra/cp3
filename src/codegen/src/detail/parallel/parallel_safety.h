// =============================================================================
// src/codegen/src/detail/parallel_safety.h - the pure integer webserverion proof
// used by the chunked `parallel.reduce` gate.
//
// A reduce body may only be split into unordered chunks when every element
// webserverion is pure and integer exact. The runtime stores one partial per
// chunk and combines them in chunk-index order, but the chunks themselves run
// in an unordered schedule, so an webserverion may only be chunked when the fold
// is associative and every observable effect is independent of that schedule:
//
//   * the body is `acc <op> rhs` (or the max/min conditional shape) where rhs
//     never reads the accumulator;
//   * rhs is pure: no assignment/update to a binding outside its own locals,
//     no `new`, `await`, `delete`, member/index access, or unknown call;
//   * rhs is integer exact: integer literals, the index parameter, locals that
//     are only ever assigned integer-exact values, `+`, `-`, `*`, `%`,
//     comparisons, conditionals, and calls to declared functions that pass the
//     same proof transitively;
//   * a division is accepted only when the numerator is a multiple of the
//     divisor, proven by a dominating `x % d == 0` test (the then branch) or
//     by an integral-literal numerator. Collatz's `n / 2` inside
//     `if (n % 2 === 0)` is the motivating case; `1 / (i + 1)` is rejected.
//
// A declared function is pure when its body has no write to anything except
// its own parameters/locals, no I/O/network/async operation, no `new`, and
// every call it makes is pure. Module bindings are not in the function's
// locals, so reading one rejects: another closure or worker could write it
// while the chunks run. `collatzSteps` (locals only, no calls) qualifies.
//
// The proof is complete-or-nothing and deliberately narrow. Recursion,
// mutual recursion, methods/builtins, arrows, classes, try/catch, switch,
// for-of/for-in, rest/ref/default parameters, and destructuring all reject.
// It proves integrality and purity, not a magnitude bound. The chunked runtime
// overload enforces the exact-integer range at run time (finite, integral, and
// strictly below 2^53); if a chunk or the combine crosses it, all chunk work is
// discarded and the pure reduce runs again sequentially. compute-multi's total
// is 131,434,424, far inside the guard.
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/host/options.hpp"

namespace cp3 {
namespace detail {

// True when `e` is pure and integer exact, reading only the names in
// `integerNames`. `functions` maps a declared function name to its declaration
// and `sema` supplies resolved parameter/return types. `typeSystem` resolves a
// plain native annotation (`i64`) that sema did not publish. Division is
// accepted only when the numerator is a proved multiple of the divisor.
bool parallelPureIntegerExpr(
    const Expr& e, const std::unordered_set<std::string>& integerNames,
    const std::unordered_map<std::string, const FunctionDeclaration*>& functions,
    const SemaResult& sema, TypeSystem typeSystem);

}  // namespace detail
}  // namespace cp3
