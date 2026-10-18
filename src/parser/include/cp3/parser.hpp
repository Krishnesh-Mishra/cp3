// =============================================================================
// cp3/parser.hpp - TokenStream -> AST.
//
// OWNERSHIP: parser module. The current implementation is a STUB: parseProgram()
// returns an empty Program. Replace the body; the public shape (construct over
// a TokenStream + DiagnosticSink, return a ProgramPtr) is the frozen boundary.
// =============================================================================
#pragma once

#include <utility>

#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/token.hpp"

namespace cp3 {

// Recursive-descent parser over one module's tokens. Syntax problems are
// reported to the sink; the parser recovers and still returns a Program (with
// nodeIds assigned) so later stages can run in a "best effort" mode.
class Parser {
public:
    Parser(TokenStream tokens, DiagnosticSink& sink) noexcept
        : tokens_(std::move(tokens)), sink_(sink) {}

    // Parses the whole token stream into a Program. Never returns nullptr.
    ProgramPtr parseProgram();

    TokenStream& tokens() noexcept { return tokens_; }

private:
    TokenStream tokens_;
    DiagnosticSink& sink_;
};

}  // namespace cp3
