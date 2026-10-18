// =============================================================================
// src/sema/src/semantic_analyzer.cpp - sema entry point + orchestration.
//
// Owns the frozen `SemanticAnalyzer::analyze` boundary (cp3/sema.hpp), the
// `Analyzer` diagnostic helpers (D2 anchoring: column 1 of the offending node's
// begin line), and the top-level `Analyzer::run` pass. Scopes and declarations
// live in scopes.cpp / declarations.cpp, type logic in type_check.cpp, and so
// on (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstdint>
#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"
#include "cp3/sema.hpp"
#include "cp3/suggest.hpp"

namespace cp3 {

// =============================================================================
// Analyzer - diagnostics
// =============================================================================
SourceSpan Analyzer::anchorSpan(const SourceSpan& s) const {
    if (!s.file) return s;
    const std::uint32_t line = s.begin.line;
    if (line == 0) return s;
    const auto& starts = s.file->lineStarts;
    std::uint32_t offset = s.begin.offset;
    if (line - 1 < starts.size()) offset = starts[line - 1];
    SourcePos p;
    p.offset = offset;
    p.line = line;
    p.column = 1;
    return SourceSpan{s.file, p, p};
}

void Analyzer::report(const char* code, const SourceSpan& span,
                      std::string message) {
    sink_.error(code, anchorSpan(span), std::move(message));
}

void Analyzer::reportSuggestive(const char* code, const SourceSpan& span,
                                std::string message,
                                std::vector<std::string> suggestions) {
    Diagnostic d(code, Severity::Error, anchorSpan(span), std::move(message));
    d.suggestions = std::move(suggestions);
    sink_.report(std::move(d));
}

void Analyzer::notImplemented(const SourceSpan& span, const std::string& what) {
    report(diagcode::kNotImplemented, span, "not implemented yet: " + what);
}

void Analyzer::reportUndefinedName(const SourceSpan& span,
                                   const std::string& name) {
    std::string message = "cannot find name '" + name + "'";
    std::vector<std::string> suggestions;
    // `Math` / `console` / ... without their import: hand over the exact
    // statement instead of only naming the problem (works without a TTY).
    const std::string hint = stdImportHint(name);
    if (!hint.empty()) {
        message += "; add `" + hint + "`";
        suggestions.push_back("Add `" + hint + "`");
    }
    const std::vector<std::string> ranked = suggestNames(name, visibleNames());
    if (!ranked.empty()) {
        message += "; did you mean '" + ranked.front() + "'?";
        suggestions.push_back(didYouMeanPhrase(ranked));
    }
    reportSuggestive(diagcode::kUndefinedIdentifier, span, std::move(message),
                     std::move(suggestions));
}

void Analyzer::reportMemberMiss(const std::string& receiver,
                                const std::string& prop, const SourceSpan& span,
                                const std::vector<std::string>& candidates) {
    std::string message = receiver + " has no member '" + prop + "'";
    std::vector<std::string> suggestions;
    const std::vector<std::string> ranked = suggestNames(prop, candidates);
    if (!ranked.empty()) {
        message += "; did you mean '" + ranked.front() + "'?";
        suggestions.push_back(didYouMeanPhrase(ranked));
    }
    reportSuggestive(diagcode::kMemberNotFound, span, std::move(message),
                     std::move(suggestions));
}

// =============================================================================
// Analyzer - top-level orchestration
// =============================================================================
void Analyzer::run(Program& program) {
    pushScope();
    declareBuiltins();
    pushScope();
    checkStmts(program.body);
    popScope();
    popScope();
}

// =============================================================================
// SemanticAnalyzer entry points (frozen boundary)
// =============================================================================
bool SemanticAnalyzer::analyze(Program& program, SemaResult& out) {
    out.clear();
    Analyzer analyzer(sink_, out);
    analyzer.run(program);
    out.ok = !sink_.hasErrors();
    return out.ok;
}

bool SemanticAnalyzer::analyze(Program& program) {
    SemaResult discarded;
    return analyze(program, discarded);
}

}  // namespace cp3
