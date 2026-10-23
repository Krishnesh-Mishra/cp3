// =============================================================================
// src/cli/src/commands/lsp_intel_names.cpp - specifier / name completion.
//
// OWNERSHIP: cli module (src/cli/**). Completes the importSpecifierCandidates,
// legalKeywords, namesAType and generalCandidates declarations in
// detail/lsp_intel_complete.hpp.
//
// Two sources the compiler already knows: the `std/...` module registry
// (stdModuleSpecifiers) for a cursor inside an import string, and the names
// the document declares plus language keywords and snippets for a cursor in
// code. A relative specifier deliberately answers nothing: completion never
// scans the disk.
//
// The general answer is ordered like the TypeScript extension's popup: the
// names in scope at the cursor first (lsp_intel_scope.cpp), then the names
// the file imports, then the module-level declarations, and the language
// keywords last - and only the ones that may legally appear at the cursor.
// =============================================================================
#include "detail/lsp/lsp_intel_complete.hpp"
#include "detail/lsp/lsp_intel_docs.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/std_modules.hpp"
#include "cp3/core/types.hpp"

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

// One named candidate with the documentation the editor shows next to it.
// A candidate that has a doc comment carries it; one that does not carries an
// empty string, and the editor shows the detail line alone.
void appendNamed(std::vector<lsp::CompletionCandidate>& out,
                 const std::string& name, int kind, const std::string& detail,
                 const std::string& documentation) {
    if (name.empty() || hasLabel(out, name)) return;
    lsp::CompletionCandidate candidate;
    candidate.label = name;
    candidate.kind = kind;
    candidate.detail = detail;
    candidate.documentation = documentation;
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

// The type the compiler inferred for a declaration, when it knows one.
std::string typeDetail(const SemaResult& sema, std::uint32_t nodeId,
                       const std::string& fallback) {
    const Type* type = sema.typeOf(nodeId);
    if (type == nullptr || type->isUnknown() || type->isError()) return fallback;
    if (type->kind == TypeKind::Object && !type->name.empty() &&
        type->name.front() == '$') {
        return fallback;  // the compiler's placeholder for an unresolved import
    }
    return describe(*type);
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

// The names a variable declaration binds, with the type the compiler gave
// and the doc comment of the declaration they came from.
void appendVariableDeclaration(const AnalyzedDocument& document,
                                const VariableDeclaration& declaration,
                                std::vector<lsp::CompletionCandidate>& out) {
    const bool isConst = declaration.kind == VarKind::Const;
    for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
        if (declarator == nullptr || declarator->id == nullptr) continue;
        if (declarator->id->kind != NodeKind::Identifier) continue;
        appendNamed(out, static_cast<const Identifier&>(*declarator->id).name,
                    isConst ? 21 : 6,
                    typeDetail(document.sema, declarator->id->nodeId,
                               isConst ? "const" : "let"),
                    docCommentFor(document, declarator->nodeId));
    }
}

void appendStatementCandidates(const AnalyzedDocument& document,
                               const Stmt& statement,
                               std::vector<lsp::CompletionCandidate>& out,
                               bool typesOnly) {
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration: {
            if (typesOnly) return;
            const auto& declaration =
                static_cast<const FunctionDeclaration&>(statement);
            appendNamed(out, declaration.name, 3,
                        typeDetail(document.sema, statement.nodeId, "function"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::ClassDeclaration: {
            const auto& declaration = static_cast<const ClassDeclaration&>(statement);
            appendNamed(out, declaration.name, 7,
                        typeDetail(document.sema, statement.nodeId, "class"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& declaration =
                static_cast<const InterfaceDeclaration&>(statement);
            appendNamed(out, declaration.name, 8,
                        typeDetail(document.sema, statement.nodeId, "interface"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::EnumDeclaration: {
            const auto& declaration = static_cast<const EnumDeclaration&>(statement);
            appendNamed(out, declaration.name, 13,
                        typeDetail(document.sema, statement.nodeId, "enum"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& declaration =
                static_cast<const TypeAliasDeclaration&>(statement);
            appendNamed(out, declaration.name, 22,
                        typeDetail(document.sema, statement.nodeId, "type"),
                        docCommentFor(document, statement.nodeId));
            return;
        }
        case NodeKind::VariableDeclaration:
            if (typesOnly) return;
            appendVariableDeclaration(
                document, static_cast<const VariableDeclaration&>(statement), out);
            return;
        case NodeKind::ImportDeclaration:
            if (typesOnly) return;
            appendImportCandidates(static_cast<const ImportDeclaration&>(statement),
                                   out);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& declaration = static_cast<const ExportDeclaration&>(statement);
            if (declaration.declaration != nullptr) {
                appendStatementCandidates(document, *declaration.declaration, out,
                                          typesOnly);
            }
            return;
        }
        default:
            return;
    }
}

// The names the module declares at the top level.
void appendDeclarationCandidates(const AnalyzedDocument& document,
                                 std::vector<lsp::CompletionCandidate>& out,
                                 bool typesOnly) {
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        if (statement->span.file != document.file) continue;  // prelude tail
        if (statement->kind == NodeKind::ImportDeclaration) continue;
        appendStatementCandidates(document, *statement, out, typesOnly);
    }
}

// The names the file imports.
void appendImportedCandidates(const AnalyzedDocument& document,
                              std::vector<lsp::CompletionCandidate>& out) {
    for (const StmtPtr& statement : document.entry->ast->body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        if (statement->span.file != document.file) continue;  // prelude tail
        if (statement->kind != NodeKind::ImportDeclaration) continue;
        appendImportCandidates(static_cast<const ImportDeclaration&>(*statement),
                               out);
    }
}

// The keywords a statement may start with.
const char* const kStatementKeywords[] = {
    "let", "const", "var", "function", "class", "interface", "enum", "type",
    "import", "export", "if", "else", "while", "do", "for", "switch", "try",
    "return", "throw", "break", "continue", "async", "abstract",
};

// The keywords an expression may start with.
const char* const kExpressionKeywords[] = {
    "new", "this", "await", "typeof", "async", "true", "false", "null",
    "undefined",
};

// The keywords a class member may start with.
const char* const kClassBodyKeywords[] = {
    "static", "public", "private", "protected", "abstract", "async", "get",
    "set", "function",
};

// True when a completion kind names a type (a class, an interface, an enum
// or a type alias) rather than a value.
bool namesAType(int kind) {
    return kind == 7 || kind == 8 || kind == 13 || kind == 22;
}

}  // namespace

std::vector<std::string> legalKeywords(CompletionPosition position) {
    std::vector<std::string> out;
    switch (position) {
        case CompletionPosition::StatementStart:
            break;
        case CompletionPosition::TryBlock:
            out.push_back("catch");
            out.push_back("finally");
            break;
        case CompletionPosition::SwitchBlock:
            out.push_back("case");
            out.push_back("default");
            break;
        case CompletionPosition::ExpressionStart:
            for (const char* word : kExpressionKeywords) out.push_back(word);
            return out;
        case CompletionPosition::ClassBody:
            for (const char* word : kClassBodyKeywords) out.push_back(word);
            return out;
        case CompletionPosition::ParameterList:
            out.push_back("ref");
            return out;
        case CompletionPosition::ImportClause:
            out.push_back("as");
            out.push_back("from");
            out.push_back("native");
            return out;
        case CompletionPosition::TypePosition:
        case CompletionPosition::DeclarationName:
        case CompletionPosition::MemberPosition:
        case CompletionPosition::Unknown:
            return out;
    }
    for (const char* word : kStatementKeywords) out.push_back(word);
    return out;
}

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
    const AnalyzedDocument& document, const CompletionContext& context,
    std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> candidates;
    const CompletionPosition position = context.position;
    const bool typesOnly = position == CompletionPosition::TypePosition;
    // 1. The names in scope at the cursor: the local scope comes first.
    for (const lsp::CompletionCandidate& candidate :
         scopeCandidates(document, position, offset)) {
        if (typesOnly && !namesAType(candidate.kind)) continue;
        appendNamed(candidates, candidate.label, candidate.kind, candidate.detail);
    }
    // 2. The names the file imports.
    if (!typesOnly) appendImportedCandidates(document, candidates);
    // 3. The module-level declarations.
    appendDeclarationCandidates(document, candidates, typesOnly);
    // 4. The snippets: a statement may start here.
    if (position == CompletionPosition::StatementStart) {
        appendSnippet(candidates, "import", "import ${1:name} from \"${2:./module.cp3}\";",
                      "import a module");
        appendSnippet(candidates, "function", "function ${1:name}(${2}) {\n\t${0}\n}",
                      "declare a function");
        appendSnippet(candidates, "class", "class ${1:Name} {\n\t${0}\n}",
                      "declare a class");
        appendSnippet(candidates, "interface", "interface ${1:Name} {\n\t${0}\n}",
                      "declare an interface");
    }
    // 5. The keywords that may legally appear here, last.
    for (const std::string& keyword : legalKeywords(position)) {
        appendNamed(candidates, keyword, 14, "keyword");
    }
    return candidates;
}

}  // namespace cp3::cli
