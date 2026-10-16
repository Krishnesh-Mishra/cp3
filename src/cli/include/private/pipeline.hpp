// =============================================================================
// cli/private/pipeline.hpp — wiring the frozen frontend stages DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/pipeline.cpp (STYLE.md §3/§8: headers declare,
// `.cpp` defines).
//
// Stages communicate only through the frozen boundaries:
//   SourceManager -> Lexer -> Parser -> SemanticAnalyzer -> CodeGenerator
// and every stage reports through a DiagnosticSink; the CLI renders.
// =============================================================================
#pragma once

#include <string>

#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/parser.hpp"
#include "cp3/source.hpp"

namespace cp3::cli {

// Lexes + parses one SourceFile. Always returns a non-null Program (the parser
// recovers); lexical/syntax problems land in `sink`.
ProgramPtr parseFile(const SourceFile& file, DiagnosticSink& sink);

// Runs sema with the CURRENT one-argument signature. Returns true when no
// error/fatal diagnostic was reported.
bool analyzeProgram(Program& program, DiagnosticSink& sink);

// Emits C++ for one program, using the CURRENT two-argument CodeGenerator
// signature (empty SemaResult internally). See the TODO in src/pipeline.cpp for
// the single place the future SemaResult threading lands.
GeneratedModule generateModule(Program& program, DiagnosticSink& sink,
                               const std::string& moduleName);

}  // namespace cp3::cli
