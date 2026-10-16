// =============================================================================
// cp3/codegen.hpp — checked AST -> generated C++20 (+ source map).
//
// OWNERSHIP: codegen module. The current implementation is a STUB: generate()
// returns empty strings. Replace the body; the public shape is the frozen
// boundary (extended and frozen by the contracts pass — DECISIONS D14).
//
// One .cp3 file -> exactly one .hpp/.cpp pair (docs/cli-tooling/01-...).
//
// Codegen and sema communicate ONLY through the frozen AST plus the `SemaResult`
// side tables (DECISIONS D1): codegen looks up `sema.typeOf(node.nodeId)` to
// obtain an expression's inferred type and uses `sema.symbols`/`sema.functions`
// for resolved bindings and signatures.
// =============================================================================
#pragma once

#include <string>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"

namespace cp3 {

// The generated C++20 for one compilation unit.
struct GeneratedModule {
    std::string header;  // contents of the generated .hpp
    std::string source;  // contents of the generated .cpp
};

class CodeGenerator {
public:
    explicit CodeGenerator(DiagnosticSink& sink) noexcept : sink_(sink) {}

    // Emits C++20 for `program`, consulting `sema` for inferred types and
    // resolved bindings. `moduleName` is the logical/relative module name used
    // to derive the output file pair and any include guards.
    // Never returns a partially-written module: on internal failure the
    // generator reports a diagnostic and returns empty strings.
    GeneratedModule generate(const Program& program,
                             const SemaResult& sema,
                             const std::string& moduleName);

    // Convenience: generate against an empty SemaResult (types stay unknown).
    // Intended for probes/tests, not for the real pipeline.
    GeneratedModule generate(const Program& program, const std::string& moduleName);

private:
    DiagnosticSink& sink_;
};

}  // namespace cp3
