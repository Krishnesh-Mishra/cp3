// =============================================================================
// src/cli/src/commands/lsp_intel_complete.cpp - completion (context detection).
//
// OWNERSHIP: cli module (src/cli/**). Implements completeAt
// (detail/lsp_intel.hpp).
//
// completeAt decides where the cursor sits from the raw text and one lexical
// pass (partial word, `receiver.|`, an import specifier string, a
// comment/template) and then delegates to the three answer builders declared
// in detail/lsp_intel_complete.hpp: memberCandidates (probe-validated,
// lsp_intel_members.cpp), importSpecifierCandidates and generalCandidates
// (lsp_intel_names.cpp).
//
// Everything is bounded and total: a broken document lexes with recovery, an
// unparseable probe degrades to an empty list, and the try/catch in the hook
// factory keeps the server alive.
// =============================================================================
#include "detail/lsp_intel_complete.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/lexer.hpp"
#include "cp3/lsp.hpp"
#include "cp3/source.hpp"
#include "cp3/token.hpp"

namespace cp3::cli {
namespace {

// -----------------------------------------------------------------------------
// Text helpers
// -----------------------------------------------------------------------------

bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$';
}

std::uint32_t wordBeginAt(const std::string& text, std::uint32_t offset) {
    std::uint32_t at = offset;
    while (at > 0 && isIdentChar(text[at - 1])) --at;
    return at;
}

// Start of the receiver expression ending at `dot`: maximal run of identifier
// chars, dots, optional-chain markers and balanced brackets/parens (bounded).
std::uint32_t scanReceiverBegin(const std::string& text, std::uint32_t dot) {
    const std::uint32_t floor = dot > 200 ? dot - 200 : 0;
    std::uint32_t at = dot;
    while (at > floor) {
        const char c = text[at - 1];
        if (c == '?' || isIdentChar(c) || c == '.') {
            --at;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            const char open = c == ')' ? '(' : (c == ']' ? '[' : '{');
            int depth = 1;
            std::uint32_t back = at - 1;
            while (back > floor && depth > 0) {
                const char d = text[back - 1];
                if (d == c) {
                    ++depth;
                } else if (d == open) {
                    --depth;
                }
                --back;
            }
            at = back;
            continue;
        }
        break;
    }
    return at;
}

}  // namespace

// Last identifier-shaped word in `text`. Shared with the member builder
// (declared in detail/lsp_intel_complete.hpp).
std::string lastIdentifier(const std::string& text) {
    std::size_t end = text.size();
    while (end > 0 && !isIdentChar(text[end - 1])) --end;
    std::size_t begin = end;
    while (begin > 0 && isIdentChar(text[begin - 1])) --begin;
    return text.substr(begin, end - begin);
}

namespace {

// -----------------------------------------------------------------------------
// Context detection (token stream around the cursor)
// -----------------------------------------------------------------------------

const Token* tokenAt(const std::vector<Token>& tokens, std::uint32_t offset) {
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::EndOfFile) return nullptr;
        if (offset <= token.span.begin.offset) return nullptr;
        if (offset <= token.span.end.offset) return &token;
    }
    return nullptr;
}

bool insideToken(const Token& token, std::uint32_t offset) {
    if (offset <= token.span.begin.offset) return false;
    if (offset < token.span.end.offset) return true;
    return token.unterminated && offset <= token.span.end.offset;
}

bool isTrivia(TokenKind kind) {
    return kind == TokenKind::LineComment || kind == TokenKind::BlockComment;
}

bool isStringKind(TokenKind kind) { return kind == TokenKind::StringLiteral; }

bool isTemplateKind(TokenKind kind) {
    return kind == TokenKind::TemplateString || kind == TokenKind::TemplateHead ||
           kind == TokenKind::TemplateMiddle || kind == TokenKind::TemplateTail;
}

// True when the string token follows `from` (or `import`): the cursor is then
// inside a module specifier. Purely lexical, so it also holds for the
// unterminated string the user is still typing.
bool specifierString(const std::vector<Token>& tokens, const Token& stringToken) {
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].span.begin.offset != stringToken.span.begin.offset) continue;
        for (std::size_t j = i; j-- > 0;) {
            if (isTrivia(tokens[j].kind)) continue;
            return tokens[j].kind == TokenKind::KwFrom ||
                   tokens[j].kind == TokenKind::KwImport;
        }
        return false;
    }
    return false;
}

CompletionContext detectContext(const std::vector<Token>& tokens,
                                const std::string& text, std::uint32_t offset) {
    CompletionContext context;
    context.wordBegin = wordBeginAt(text, offset);
    if (context.wordBegin > 0 && text[context.wordBegin - 1] == '.') {
        const std::uint32_t dot = context.wordBegin - 1;
        const bool rangeOperator = dot > 0 && text[dot - 1] == '.';  // `0..`
        if (!rangeOperator) {
            context.member = true;
            context.dot = dot;
            context.receiverBegin = scanReceiverBegin(text, dot);
        }
    }
    const Token* token = tokenAt(tokens, offset);
    if (token == nullptr) return context;
    if (!isTrivia(token->kind) && !isStringKind(token->kind) &&
        !isTemplateKind(token->kind)) {
        return context;
    }
    const bool eolComment = token->kind == TokenKind::LineComment &&
                            offset == token->span.end.offset;
    if (!insideToken(*token, offset) && !eolComment) return context;
    context.member = false;
    if (isStringKind(token->kind) && specifierString(tokens, *token)) {
        context.importSpecifier = true;
        context.stringBegin = token->span.begin.offset;
        return context;
    }
    context.blocked = true;
    return context;
}

}  // namespace

std::vector<lsp::CompletionCandidate> completeAt(const std::string& path,
                                                 const std::string& text,
                                                 std::uint32_t line,
                                                 std::uint32_t column) {
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return {};
    const std::uint32_t offset = offsetOf(*document->file, line, column);
    // One lexical pass for the cursor context (kept alive for the token views).
    SourceManager lexerSources;
    SourceFile* lexed = lexerSources.addFile(path, text);
    if (lexed == nullptr) return {};
    CollectingDiagnosticSink sink;
    Lexer lexer(*lexed, sink);
    const std::vector<Token> tokens = lexer.lex().tokens();
    const CompletionContext context = detectContext(tokens, text, offset);
    if (context.blocked) return {};
    if (context.importSpecifier) {
        return importSpecifierCandidates(text, context, offset);
    }
    if (context.member) {
        return memberCandidates(*document, text, context, offset);
    }
    return generalCandidates(*document);
}

}  // namespace cp3::cli
