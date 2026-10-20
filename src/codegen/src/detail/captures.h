// =============================================================================
// src/codegen/src/detail/captures.h - PRIVATE closure-capture analysis.
//
// The `Cell<T>` lowering (cp3/core/value.hpp, spec §10 "Closures") is driven
// by this lexical walk over the checked AST: within every frame (the module
// body, a function, an arrow) a local becomes a shared cell exactly when it is
//
//   * declared by a variable declarator of that frame (parameters, `for`
//     headers and `for ... of` bindings are excluded: they are per-call or
//     per-iteration copies), and
//   * reassigned somewhere in the frame's subtree (a plain `=`, a compound
//     assignment, `x++`, or a destructuring assignment target), and
//   * observed from a nested frame (a deeper function or arrow), so the copy
//     semantics of a plain C++ capture would be wrong.
//
// Everything else stays a plain value: a module-scope global is visible to
// every lambda without capture, and a captured-but-never-reassigned binding
// keeps the cheap copy (spec §10).
//
// `arrowOf` records `let f = (...) => ...` bindings so a call through `f` can
// still see the arrow's parameter defaults (C++ lambdas cannot carry them).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "cp3/ast.hpp"

namespace cp3 {
namespace detail {

struct CaptureInfo {
    // Frame nodeId (Program / FunctionDeclaration / ArrowFunction) -> the
    // frame's locals that need the shared-cell representation.
    std::unordered_map<std::uint32_t, std::unordered_set<std::string>> frameCells;
    // Binding name -> the arrow it was initialized with.
    std::unordered_map<std::string, const ArrowFunction*> arrowOf;
};

CaptureInfo analyzeCaptures(const Program& program);

// Does a function body contain an arrow, a destructuring pattern, a for-of /
// for-in loop or an object literal? The int64 fast path does not model any of
// them, so an eligible-looking body then bails out of the fast-path attempt.
bool bodyHasClosureOrPattern(const Stmt& body);

}  // namespace detail
}  // namespace cp3
