// =============================================================================
// cp3/lexer.hpp — .cp3 source text -> TokenStream.
//
// OWNERSHIP: lexer module. The current implementation is a STUB: it emits a
// single EndOfFile token so the pipeline links and downstream stages can be
// built independently. Replace Lexer::lex() with the real scanner; the public
// shape here (construct over a SourceFile + DiagnosticSink, return a
// TokenStream) is the frozen module boundary.
// =============================================================================
#pragma once

#include "cp3/diagnostic.hpp"
#include "cp3/source.hpp"
#include "cp3/token.hpp"

namespace cp3 {

// Scans one SourceFile into a TokenStream. Lexical problems are reported to the
// sink (never thrown across this boundary); recovery is the lexer's business.
class Lexer {
public:
    Lexer(const SourceFile& file, DiagnosticSink& sink) noexcept
        : file_(file), sink_(sink) {}

    // Produces the token stream for file_. Always returns at least an
    // EndOfFile token; a default-constructed stream is never a valid result.
    TokenStream lex();

private:
    const SourceFile& file_;
    DiagnosticSink& sink_;
};

}  // namespace cp3
