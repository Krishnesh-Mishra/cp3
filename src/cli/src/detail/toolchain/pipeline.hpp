// =============================================================================
// src/cli/src/detail/pipeline.hpp - wiring the frozen frontend stages DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/toolchain/pipeline.cpp (STYLE.md §3/§8: headers declare,
// `.cpp` defines).
//
// Stages communicate only through the frozen boundaries:
//   SourceManager -> ModuleLoader -> (Parser) -> SemanticAnalyzer -> CodeGenerator
// and every stage reports through a DiagnosticSink; the CLI renders. The
// module loader (cp3_modules) owns prelude injection and import resolution;
// sema and codegen share the nodeId-keyed `SemaResult` side tables
// (DECISIONS D1/D14).
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/codegen_program.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/host/options.hpp"
#include "cp3/core/source.hpp"

namespace cp3::cli {

// Loads the entry file (already registered with `sm`) and every module it
// reaches, applying §18 prelude injection and D4 import resolution. Returns a
// graph whose entry unit always carries a parsed `ast`; `sm` must outlive the
// returned graph (token lexemes and spans view its files).
std::unique_ptr<ModuleGraph> loadModuleGraph(SourceManager& sm, const SourceFile& entry,
                                             DiagnosticSink& sink);

// The "std/..." specifiers the graph resolved (as written): they select the
// native backends a build links (DECISIONS D68).
std::vector<std::string> importedStdModules(const ModuleGraph& graph);

// Runs sema with the project-aware signature (spec 03: `typeSystem` and
// `strictNumericCasts`), publishing the nodeId-keyed side tables into `out`.
// Returns true when no error/fatal diagnostic was reported.
bool analyzeProgram(Program& program, SemaResult& out, DiagnosticSink& sink,
                    const CompileOptions& compile = CompileOptions{});

// The inputs of one `generateModule` call. `lineDirectives` turns on `#line`
// source mapping, which the Debug profile needs so gdb (and the DAP adapter)
// see .cp3 files and lines. `options` carries the concurrency lowering's
// per-unit inputs (spec 13): the worker import of an entry unit, or
// worker-module mode for the worker unit itself. (The struct keeps the call
// within the parameter budget.)
struct GenerateModuleInput {
    const Program& program;
    const SemaResult& sema;
    const std::string& moduleName;
    DiagnosticSink& sink;
    bool lineDirectives = false;
    CodegenOptions options;
};

// Emits C++20 for one checked program through the generator, consulting
// `input.sema` for inferred types and resolved bindings.
GeneratedModule generateModule(const GenerateModuleInput& input);

// Emits C++20 for a whole local-module program through `generateProgram`
// (cp3/codegen_program.hpp): one generated translation unit, one C++ namespace
// per checked unit, local imports resolved through the link table.
GeneratedModule generateProgramModule(const GenerateProgramInput& input);

}  // namespace cp3::cli
