// =============================================================================
// src/cli/src/commands/lsp_intel_names.cpp - specifier / name completion.
//
// OWNERSHIP: cli module (src/cli/**). Completes the importSpecifierCandidates
// and generalCandidates declarations in detail/lsp_intel_complete.hpp.
//
// Two sources the compiler already knows: the `std/...` module registry
// (stdModuleSpecifiers) for a cursor inside an import string, and the names
// the document declares plus language keywords and snippets for a cursor in
// code. A relative specifier deliberately answers nothing: completion never
// scans the disk.
// =============================================================================
#include "detail/lsp_intel_complete.hpp"

#include <string>
#include <vector>

#include "cp3/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/std_modules.hpp"

namespace cp3::cli {

// -----------------------------------------------------------------------------
// Import specifiers, declarations, keywords, snippets
// -----------------------------------------------------------------------------

namespace {


bool hasLabel(const std::vector<lsp::CompletionCandidate>& items,
              const std::string& label) {
    for (const lsp::CompletionCandidate& item : items) {
        if (item.label == label) return true;
    }
    return false;
}

void appendNamed(std::vector<lsp::CompletionCandidate>& out,
                 const std::string& name, int kind, const std::string& detail) {
    if (name.empty() || hasLabel(out, name)) return;
    lsp::CompletionCandidate candidate;
    candidate.label = name;
    candidate.kind = kind;
    candidate.detail = detail;
    out.push_back(std::move(candidate));
}

void appendSnippet(std::vector<lsp::CompletionCandidate>& out,
                   const std::string& label, const std::string& insertText,
                   const std::string& detail) {
    if (hasLabel(out, label)) return;
    lsp::CompletionCandidate candidate;
    candidate.label = label;
    candidate.kind = 15;  // CompletionItemKind.Snippet
    candidate.detail = detail;
    candidate.insertText = insertText;
    candidate.insertTextFormat = 2;
    out.push_back(std::move(candidate));
}

void appendImportCandidates(const ImportDeclaration& import,
                            std::vector<lsp::CompletionCandidate>& out) {
    const std::string detail = "imported from \"" + import.source + "\"";
    appendNamed(out, import.defaultBinding, 6, detail);  // Variable
    appendNamed(out, import.namespaceBinding, 9, detail);  // Module
    appendNamed(out, import.alias, 6, detail);
    for (const ImportSpecifierPtr& specifier : import.specifiers) {
        if (specifier == nullptr) continue;
        const std::string& local =
            specifier->local.empty() ? specifier->imported : specifier->local;
        appendNamed(out, local, 6, detail);
    }
}

void appendStatementCandidates(const Stmt& statement,
                               std::vector<lsp::CompletionCandidate>& out) {
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration:
            appendNamed(out, static_cast<const FunctionDeclaration&>(statement).name,
                        3, "function");
            return;
        case NodeKind::ClassDeclaration:
            appendNamed(out, static_cast<const ClassDeclaration&>(statement).name,
                        7, "class");
            return;
        case NodeKind::InterfaceDeclaration:
            appendNamed(out,
                        static_cast<const InterfaceDeclaration&>(statement).name,
                        8, "interface");
            return;
        case NodeKind::EnumDeclaration:
            appendNamed(out, static_cast<const EnumDeclaration&>(statement).name,
                        13, "enum");
            return;
        case NodeKind::TypeAliasDeclaration:
            appendNamed(out,
                        static_cast<const TypeAliasDeclaration&>(statement).name,
                        22, "type");
            return;
        case NodeKind::VariableDeclaration: {
            const auto& declaration =
                static_cast<const VariableDeclaration&>(statement);
            const bool isConst = declaration.kind == VarKind::Const;
            for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
                if (declarator == nullptr || declarator->id == nullptr) continue;
                if (declarator->id->kind != NodeKind::Identifier) continue;
                appendNamed(out,
                            static_cast<const Identifier&>(*declarator->id).name,
                            isConst ? 21 : 6, isConst ? "const" : "let");
            }
            return;
        }
        case NodeKind::ImportDeclaration:
            appendImportCandidates(static_cast<const ImportDeclaration&>(statement),
                                   out);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& declaration =
                static_cast<const ExportDeclaration&>(statement);
            if (declaration.declaration != nullptr) {
                appendStatementCandidates(*declaration.declaration, out);
            }
            return;
        }
        default:
            return;
    }
}

void appendDeclarationCandidates(const AnalyzedDocument& document,
                                 std::vector<lsp::CompletionCandidate>& out) {
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        if (statement->span.file != document.file) continue;  // prelude tail
        appendStatementCandidates(*statement, out);
    }
}

const char* const kKeywords[] = {
    "let", "const", "var", "function", "class", "interface", "enum", "type",
    "extends", "implements", "abstract", "static", "public", "private",
    "protected", "get", "set", "new", "this", "import", "export", "from",
    "as", "async", "await", "return", "if", "else", "while", "do", "for",
    "of", "in", "switch", "case", "default", "break", "continue", "throw",
    "try", "catch", "finally", "typeof", "void", "true", "false", "null",
    "undefined", "weak", "ref",
};
}  // namespace

std::vector<lsp::CompletionCandidate> importSpecifierCandidates(
    const std::string& text, const CompletionContext& context,
    std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> candidates;
    if (context.stringBegin >= offset) return candidates;
    const std::string partial =
        text.substr(context.stringBegin + 1, offset - context.stringBegin - 1);
    if (!partial.empty() && partial.front() == '.') {
        return candidates;  // a relative path: no disk scanning, answer nothing
    }
    for (const std::string& specifier : stdModuleSpecifiers()) {
        lsp::CompletionCandidate candidate;
        candidate.label = specifier;
        candidate.kind = 9;  // CompletionItemKind.Module
        candidate.detail = "std module";
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}


std::vector<lsp::CompletionCandidate> generalCandidates(
    const AnalyzedDocument& document) {
    std::vector<lsp::CompletionCandidate> candidates;
    appendSnippet(candidates, "import", "import ${1:name} from \"${2:./module.cp3}\";",
                  "import a module");
    appendSnippet(candidates, "function", "function ${1:name}(${2}) {\n\t${0}\n}",
                  "declare a function");
    appendSnippet(candidates, "class", "class ${1:Name} {\n\t${0}\n}",
                  "declare a class");
    appendSnippet(candidates, "interface", "interface ${1:Name} {\n\t${0}\n}",
                  "declare an interface");
    appendDeclarationCandidates(document, candidates);
    for (const char* keyword : kKeywords) {
        appendNamed(candidates, keyword, 14, "keyword");
    }
    return candidates;
}

}  // namespace cp3::cli
