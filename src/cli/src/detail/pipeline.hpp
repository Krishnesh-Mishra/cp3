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

#include "cp3/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/module.hpp"
#include "cp3/source.hpp"

namespace cp3::cli {

// Loads the entry file (already registered with `sm`) and every module it
// reaches, applying §18 prelude injection and D4 import resolution. Returns a
// graph whose entry unit always carries a parsed `ast`; `sm` must outlive the
// returned graph (token lexemes and spans view its files).
std::unique_ptr<ModuleGraph> loadModuleGraph(SourceManager& sm, const SourceFile& entry,
                                             DiagnosticSink& sink);

// Runs sema with the extended two-argument signature, publishing the
// nodeId-keyed side tables into `out`. Returns true when no error/fatal
// diagnostic was reported.
bool analyzeProgram(Program& program, SemaResult& out, DiagnosticSink& sink);

// Emits C++20 for one checked program through the three-argument generator,
// consulting `sema` for inferred types and resolved bindings. `lineDirectives`
// turns on `#line` source mapping, which the Debug profile needs so gdb (and
// the DAP adapter) see .cp3 files and lines.
GeneratedModule generateModule(const Program& program, const SemaResult& sema,
                               const std::string& moduleName, DiagnosticSink& sink,
                               bool lineDirectives = false);

}  // namespace cp3::cli
