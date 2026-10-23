// =============================================================================
// cp3/sema.hpp - AST -> diagnostics / resolved semantics.
//
// OWNERSHIP: sema module. The current implementation is a STUB: analyze() does
// nothing and reports no diagnostics. Replace the body; the public shape is the
// frozen boundary (extended and frozen by the contracts pass - DECISIONS D14).
//
// Sema intentionally stores its results in side tables keyed by Node::nodeId;
// it does NOT mutate the AST (see ast.hpp design rules). Those tables live in
// `SemaResult` (cp3/analysis.hpp) and are keyed by the 1-based nodeId contract
// of DECISIONS D6 (0 means "unassigned").
// =============================================================================
#pragma once

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/host/options.hpp"

namespace cp3 {

// Name resolution + type checking for one Program. Reports problems through
// the sink. Returns true when no error/fatal diagnostic was reported.
class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticSink& sink) noexcept : sink_(sink) {}

    // The project-aware form (spec 03): `options.typeSystem` selects the plain
    // type-name vocabulary and `options.strictNumericCasts` enables Rust-style
    // refusal of implicit native-width conversions. Additive: the one-argument
    // constructor keeps the TypeScript defaults.
    SemanticAnalyzer(DiagnosticSink& sink, const CompileOptions& options) noexcept
        : sink_(sink), options_(options) {}

    // Analyzes `program` and publishes every inferred type / symbol / signature
    // into `out` (keyed by Node::nodeId). `out` is cleared first. Returns
    // `out.ok` - true when no error/fatal diagnostic was reported.
    bool analyze(Program& program, SemaResult& out);

    // Convenience: analyze and discard the side tables when the caller only
    // needs the yes/no answer (e.g. `cp3 check`).
    bool analyze(Program& program);

private:
    DiagnosticSink& sink_;
    CompileOptions options_{};
};

}  // namespace cp3
