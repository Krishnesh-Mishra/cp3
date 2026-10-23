// =============================================================================
// src/parser/tests/test_scanner.h - a minimal but capable scanner for the
// parser verification driver. Test-only: this is NOT the cp3 lexer.
//
// The lexer module is still a stub (it emits only EndOfFile), so the driver
// self-lexes. The scanner produces the same flat token shapes the frozen
// cp3/token.hpp documents (TemplateHead/Middle/Tail sequence, etc.).
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic.hpp"
#include "cp3/core/source.hpp"
#include "cp3/core/token.hpp"

namespace cp3test {

using cp3::DiagnosticSink;
using cp3::SourceFile;
using cp3::SourceSpan;
using cp3::Token;
using cp3::TokenKind;
using cp3::TokenStream;

class TestScanner {
public:
    TestScanner(const SourceFile& file, DiagnosticSink& sink)
        : file_(file), src_(file.text), sink_(sink) {}

    // Lexes the whole file and returns the token stream (ending in EndOfFile).
    TokenStream scan();

private:
    const SourceFile& file_;
    const std::string& src_;
    DiagnosticSink& sink_;
    std::size_t p_ = 0;
    bool pendingNl_ = false;
    std::size_t braceDepth_ = 0;
    std::vector<std::size_t> tmplStack_;  // brace depths awaiting a `}`
    std::vector<Token> toks_;

    char cur() const;
    char at(std::size_t k) const;
    bool eof() const;
    SourceSpan span(std::size_t b, std::size_t e) const;

    void emit(TokenKind k, std::size_t b, std::size_t e);
    TokenKind lastKind() const;
    bool prevIsValue() const;
    void skipTrivia();

    bool lexOne();
    void lexIdentifier();
    void lexPunctuation();
    void lexSingleCharPunctuation();

    double parseNumberText(std::string s) const;
    void lexNumber();
    std::string cook(const std::string& raw) const;
    static unsigned hexVal(char c);
    void lexString();
    void lexTemplateOpen();
    std::string readTemplateRaw(bool& hasSub);
    void lexTemplateContinuation();
    void lexRegExp();
};

}  // namespace cp3test
