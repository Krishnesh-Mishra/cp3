// =============================================================================
// cp3/sema.hpp — AST -> diagnostics / resolved semantics.
//
// OWNERSHIP: sema module. The current implementation is a STUB: analyze() does
// nothing and reports no diagnostics. Replace the body; the public shape is the
// frozen boundary (extended and frozen by the contracts pass — DECISIONS D14).
//
// Sema intentionally stores its results in side tables keyed by Node::nodeId;
// it does NOT mutate the AST (see ast.hpp design rules). Those tables live in
// `SemaResult` (cp3/analysis.hpp) and are keyed by the 1-based nodeId contract
// of DECISIONS D6 (0 means "unassigned").
// =============================================================================
#pragma once

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"

namespace cp3 {

// Name resolution + type checking for one Program. Reports problems through
// the sink. Returns true when no error/fatal diagnostic was reported.
class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticSink& sink) noexcept : sink_(sink) {}

    // Analyzes `program` and publishes every inferred type / symbol / signature
    // into `out` (keyed by Node::nodeId). `out` is cleared first. Returns
    // `out.ok` — true when no error/fatal diagnostic was reported.
    bool analyze(Program& program, SemaResult& out);

    // Convenience: analyze and discard the side tables when the caller only
    // needs the yes/no answer (e.g. `cp3 check`).
    bool analyze(Program& program);

private:
    DiagnosticSink& sink_;
};

}  // namespace cp3
