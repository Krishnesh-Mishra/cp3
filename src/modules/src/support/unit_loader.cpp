// =============================================================================
// src/modules/src/support/unit_loader.cpp - prelude token injection + parsing.
//
// OWNERSHIP: modules module (src/modules/**). Implements the unit_loader
// concern declared in detail/loader.h.
//
// Each applicable prelude is lexed exactly once (cached, so its lexical
// diagnostics are reported once) and its tokens are concatenated in front of
// the unit's own tokens, with the unit's trailing EOF kept last. A single
// Parser pass over the assembled stream assigns unique nodeIds to the whole
// program while every token keeps the SourceSpan of the file it came from.
// =============================================================================
#include "detail/loader.h"

#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/lexer.hpp"
#include "cp3/parser.hpp"

#include "detail/file_paths.h"
#include "detail/prelude_scan.h"

namespace cp3::modules {

namespace {

std::vector<Token> lexFile(const SourceFile& file, DiagnosticSink& sink) {
    Lexer lexer(file, sink);
    return lexer.lex().tokens();
}

// Drops every EndOfFile token; the unit's own EOF is appended afterwards.
void stripEofTokens(std::vector<Token>& tokens) {
    while (!tokens.empty() && tokens.back().kind == TokenKind::EndOfFile) {
        tokens.pop_back();
    }
}

}  // namespace

void ProgramLoader::parseUnit(CompilationUnit& unit, bool withPreludes) {
    std::vector<Token> tokens = unitTokens(unit, withPreludes);
    Parser parser(TokenStream(std::move(tokens)), sink_);
    unit.ast = parser.parseProgram();
    unit.parsed = true;
}

std::vector<Token> ProgramLoader::unitTokens(CompilationUnit& unit, bool withPreludes) {
    std::vector<Token> assembled;
    if (withPreludes && unit.file != nullptr) {
        for (const PreludeRef& ref : preludesForFolder(folderOfSource(*unit.file))) {
            appendPrelude(unit, ref, assembled);
        }
    }
    std::vector<Token> own = lexFile(*unit.file, sink_);
    assembled.insert(assembled.end(), own.begin(), own.end());
    return assembled;
}

bool ProgramLoader::appendPrelude(CompilationUnit& unit, const PreludeRef& ref,
                                  std::vector<Token>& assembled) {
    CompilationUnit* prelude = ensureUnit(ref.path);
    if (prelude == nullptr) {
        reportOnce(diagcode::kPreludeError, SourceSpan{},
                   "cannot read prelude file '" + ref.path.generic_string() + "'");
        return false;
    }
    unit.injectedPreludes.push_back(prelude->id);
    const std::vector<Token>& tokens = preludeTokens(*prelude);
    assembled.insert(assembled.end(), tokens.begin(), tokens.end());
    return true;
}

const std::vector<Token>& ProgramLoader::preludeTokens(CompilationUnit& prelude) {
    const std::string key = canonicalKey(fs::path(prelude.file->path));
    const auto it = preludeTokenCache_.find(key);
    if (it != preludeTokenCache_.end()) return it->second;
    std::vector<Token> tokens = lexFile(*prelude.file, sink_);
    stripEofTokens(tokens);
    return preludeTokenCache_.emplace(key, std::move(tokens)).first->second;
}

}  // namespace cp3::modules
