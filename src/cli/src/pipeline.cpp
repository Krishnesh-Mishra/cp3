// =============================================================================
// cli/pipeline.cpp — wiring the frozen frontend stages together.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/pipeline.hpp.
//
// Stages communicate only through the frozen boundaries:
//   SourceManager -> Lexer -> Parser -> SemanticAnalyzer -> CodeGenerator
// and every stage reports through a DiagnosticSink; the CLI renders.
// =============================================================================
#include "private/pipeline.hpp"

#include <string>
#include <utility>

#include "cp3/lexer.hpp"
#include "cp3/parser.hpp"
#include "cp3/sema.hpp"

namespace cp3::cli {

ProgramPtr parseFile(const SourceFile& file, DiagnosticSink& sink) {
    Lexer lexer(file, sink);
    TokenStream tokens = lexer.lex();
    Parser parser(std::move(tokens), sink);
    return parser.parseProgram();
}

bool analyzeProgram(Program& program, DiagnosticSink& sink) {
    SemanticAnalyzer analyzer(sink);
    return analyzer.analyze(program);
}

// -----------------------------------------------------------------------------
// TODO(contracts): SemaResult threading.
//
// A concurrent contracts agent is moving the pipeline to share sema's side
// tables with codegen. When that lands, this is the SINGLE place to change:
//
//     SemaResult sema;
//     analyzer.analyze(program, sema);             // 2-arg analyze
//     codegen.generate(program, sema, moduleName); // 3-arg generate
//
// Both extended overloads already exist in the frozen headers
// (cp3/sema.hpp, cp3/codegen.hpp); they are intentionally NOT used yet — the
// CLI codes against the current signatures until the contracts pass is
// announced, so nothing here half-implements the shared side-table flow.
// -----------------------------------------------------------------------------
GeneratedModule generateModule(Program& program, DiagnosticSink& sink,
                               const std::string& moduleName) {
    CodeGenerator codegen(sink);
    return codegen.generate(program, moduleName);
}

}  // namespace cp3::cli
