// =============================================================================
// cp3/codegen.hpp — checked AST -> generated C++20 (+ source map).
//
// OWNERSHIP: codegen module. The current implementation is a STUB: generate()
// returns empty strings. Replace the body; the public shape (construct over a
// DiagnosticSink, generate from a Program + logical module name, return the
// .hpp/.cpp pair) is the frozen boundary.
//
// One .cp3 file -> exactly one .hpp/.cpp pair (docs/cli-tooling/01-...).
// =============================================================================
#pragma once

#include <string>

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

    // Emits C++20 for `program`. `moduleName` is the logical/relative module
    // name used to derive the output file pair and any include guards.
    // Never returns a partially-written module: on internal failure the
    // generator reports a diagnostic and returns empty strings.
    GeneratedModule generate(const Program& program, const std::string& moduleName);

private:
    DiagnosticSink& sink_;
};

}  // namespace cp3
