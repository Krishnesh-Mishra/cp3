// =============================================================================
// src/lexer/src/templates.cpp - template literal scanning.
//
// Implements `Scanner::scanTemplateChunk` (declared in detail/scanner.h). Per
// DECISIONS.md D7 the scanner consumes a template literal *fully* using an
// internal mode stack, emitting `TemplateHead`, the interpolation tokens, then
// `TemplateMiddle`... / `TemplateTail`, or a single `TemplateString` when there
// is no substitution. There is no parser->lexer callback and no re-lex.
//
// `Token::stringValue` holds the cooked text (`\r\n` normalised to `\n`) and
// `Token::rawText` the uncooked inner source. An EOF before the closing backtick
// or `${` reports CP3103 and sets `unterminated` (STYLE.md §6).
// =============================================================================
#include "detail/scanner.h"

#include <cstdint>
#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {
namespace {

// The kind for a chunk: head/string when it opens on a backtick, middle/tail
// when it opens on the `}` that closed an interpolation.
TokenKind templateChunkKind(bool continuing, bool sawInterp) noexcept {
    if (!continuing) {
        return sawInterp ? TokenKind::TemplateHead : TokenKind::TemplateString;
    }
    return sawInterp ? TokenKind::TemplateMiddle : TokenKind::TemplateTail;
}

}  // namespace

// Scans one template chunk. `continuing == false` when the chunk starts with a
// backtick (TemplateHead / TemplateString); `true` when it starts with the `}`
// that closed an interpolation (TemplateMiddle / TemplateTail).
void Scanner::scanTemplateChunk(bool continuing) {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    ++i_;  // consume ` or }
    const std::uint32_t rawStart = static_cast<std::uint32_t>(i_);
    std::string cooked;
    bool terminated = false;
    bool sawInterp = false;
    std::uint32_t contentEnd = rawStart;

    while (i_ < n_) {
        const unsigned char c = static_cast<unsigned char>(text_[i_]);
        if (c == '`') {
            contentEnd = static_cast<std::uint32_t>(i_);
            ++i_;
            terminated = true;
            break;
        }
        if (c == '\\') {
            scanEscape(cooked, true);
            continue;
        }
        if (c == '$' && i_ + 1 < n_ && text_[i_ + 1] == '{') {
            contentEnd = static_cast<std::uint32_t>(i_);
            i_ += 2;
            sawInterp = true;
            break;
        }
        if (c == '\r') {
            cooked.push_back('\n');
            ++i_;
            if (i_ < n_ && text_[i_] == '\n') ++i_;
            continue;
        }
        if (c == '\n') {
            cooked.push_back('\n');
            ++i_;
            continue;
        }
        cooked.push_back(static_cast<char>(c));
        ++i_;
    }
    if (!terminated && !sawInterp) contentEnd = static_cast<std::uint32_t>(i_);

    Token t;
    t.kind = templateChunkKind(continuing, sawInterp);
    t.span = span(start, static_cast<std::uint32_t>(i_));
    t.lexeme = file_.slice(t.span);
    t.stringValue = std::move(cooked);
    t.rawText = std::string(text_.substr(rawStart, contentEnd - rawStart));
    if (!terminated && !sawInterp) {
        t.unterminated = true;
        errorAt(diagcode::kUnterminatedTemplate, start,
                static_cast<std::uint32_t>(i_), "unterminated template literal");
    }
    if (sawInterp) templateDepth_.push_back(0);
    commit(std::move(t));
}

}  // namespace cp3
