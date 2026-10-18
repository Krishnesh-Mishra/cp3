// =============================================================================
// cli/pipeline.cpp - wiring the frozen frontend stages together.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/pipeline.hpp.
//
// Stages communicate only through the frozen boundaries:
//   SourceManager -> ModuleLoader -> SemanticAnalyzer -> CodeGenerator
// and every stage reports through a DiagnosticSink; the CLI renders. The
// module loader owns §18 prelude injection and import resolution; sema's
// nodeId-keyed side tables (SemaResult) are threaded into the three-argument
// generator (DECISIONS D1/D14) - this file is the single change site.
// =============================================================================
#include "private/pipeline.hpp"

#include <string>

#include "cp3/modules.hpp"
#include "cp3/sema.hpp"

namespace cp3::cli {

std::unique_ptr<ModuleGraph> loadModuleGraph(SourceManager& sm, const SourceFile& entry,
                                             DiagnosticSink& sink) {
    ModuleLoader loader(sink);
    return loader.load(sm, entry);
}

bool analyzeProgram(Program& program, SemaResult& out, DiagnosticSink& sink) {
    SemanticAnalyzer analyzer(sink);
    return analyzer.analyze(program, out);
}

GeneratedModule generateModule(const Program& program, const SemaResult& sema,
                               const std::string& moduleName, DiagnosticSink& sink) {
    CodeGenerator codegen(sink);
    return codegen.generate(program, sema, moduleName);
}

}  // namespace cp3::cli
