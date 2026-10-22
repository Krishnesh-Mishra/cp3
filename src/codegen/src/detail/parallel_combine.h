// =============================================================================
// src/codegen/src/detail/parallel_combine.h - the conservative gate for the
// chunked `parallel.reduce` lowering.
//
// `parallel.reduce(range, init, body)` keeps the sequential 4-argument runtime
// lowering unless the body is one of the associative shapes and the compiler
// can prove the reduction is pure and integer exact. The runtime schedules
// chunks in an unordered order and combines the per-chunk partials in index
// order, so an impure expression or a floating-point addition order would
// change the observable result.
//
// This header exposes the whole decision so emit/parallel.cpp stays a thin
// caller. The classifier lives in support/parallel_safety.cpp.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"

namespace cp3 {
namespace detail {

// The synthesized combine op for a chunkable reduce body. `kind` selects the
// runtime's prefix-exactness guard (`ParallelCombineKind::Add`, `Multiply`, or
// `Max`) and `lambda` is the matching C++ combine argument. When this is
// nullopt the body must keep the sequential 4-argument lowering. `functions`
// maps a declared function name to its declaration
// (Generator::functionDecls_), and `sema` supplies the resolved
// parameter/return types.
struct ParallelCombine {
    std::string lambda;
    const char* kind;
};

std::optional<ParallelCombine> parallelReduceCombine(
    const ArrowFunction& body, const Expr& init,
    const std::unordered_map<std::string, const FunctionDeclaration*>& functions,
    const SemaResult& sema);

}  // namespace detail
}  // namespace cp3
