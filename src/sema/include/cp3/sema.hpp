// =============================================================================
// cp3/sema.hpp — AST -> diagnostics / resolved semantics.
//
// OWNERSHIP: sema module. The current implementation is a STUB: analyze() does
// nothing and reports no diagnostics. Replace the body; the public shape
// (construct over a DiagnosticSink, analyze a Program, return "no errors")
// is the frozen boundary.
//
// Sema intentionally stores its results in side tables keyed by Node::nodeId;
// it does NOT mutate the AST (see ast.hpp design rules).
// =============================================================================
#pragma once

#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"

namespace cp3 {

// Name resolution + type checking for one Program. Reports problems through
// the sink. Returns true when no error/fatal diagnostic was reported.
class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticSink& sink) noexcept : sink_(sink) {}

    bool analyze(Program& program);

private:
    DiagnosticSink& sink_;
};

}  // namespace cp3
