// =============================================================================
// src/cli/src/commands/lsp_intel_define.cpp - go-to-definition.
//
// OWNERSHIP: cli module (src/cli/**). Implements definitionAt
// (detail/lsp_intel.hpp).
//
// Answers only where the compiler has a declaration to point at:
//   * a name declared in the same file -> its declaration (the nearest one at
//     or before the cursor, so shadowing resolves to the right binding);
//   * a name bound from a LOCAL module import -> the exported declaration in
//     the target file, through the module graph's resolved dependency edge;
//   * a std/package/native import -> no location (empty), never a wrong one.
// Member (property/method) definitions and namespace imports deliberately
// answer nothing: SemaResult publishes value bindings and function signatures,
// but not the resolved class/interface member declarations, so there is no
// member table to point a definition at.
// =============================================================================
#include "detail/lsp/lsp_intel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

namespace cp3::cli {
namespace {

struct ExportHit {
    const Node* node = nullptr;
    std::string name;
};

std::string declarationName(const Stmt& statement) {
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(statement).name;
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(statement).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(statement).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(statement).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(statement).name;
        default:
            return std::string();
    }
}

ExportHit declaratorHit(const VariableDeclaration& declaration,
                        const std::string& name) {
    for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
        if (declarator == nullptr || declarator->id == nullptr) continue;
        if (declarator->id->kind != NodeKind::Identifier) continue;
        const auto& id = static_cast<const Identifier&>(*declarator->id);
        if (id.name == name) return ExportHit{declarator.get(), name};
    }
    return {};
}

// `name` declared directly by this statement (variables check their
// declarators), or an empty hit.
ExportHit hitInStatement(const Stmt& statement, const std::string& name) {
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration:
        case NodeKind::ClassDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::TypeAliasDeclaration:
            if (declarationName(statement) == name) return ExportHit{&statement, name};
            return {};
        case NodeKind::VariableDeclaration:
            return declaratorHit(static_cast<const VariableDeclaration&>(statement),
                                 name);
        default:
            return {};
    }
}

// The declaration of `name` in `program`, including declarations wrapped in
// `export <declaration>` (the parser may attach either shape).
ExportHit findDirect(const Program& program, const std::string& name) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        ExportHit hit = hitInStatement(*statement, name);
        if (hit.node != nullptr) return hit;
        if (statement->kind != NodeKind::ExportDeclaration) continue;
        const auto& declaration = static_cast<const ExportDeclaration&>(*statement);
        if (declaration.declaration == nullptr) continue;
        hit = hitInStatement(*declaration.declaration, name);
        if (hit.node != nullptr) return hit;
    }
    return {};
}

// The default export's declaration, when the module has one.
ExportHit findDefault(const Program& program) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || statement->kind != NodeKind::ExportDeclaration) {
            continue;
        }
        const auto& declaration = static_cast<const ExportDeclaration&>(*statement);
        if (declaration.kind != ExportKind::Default) continue;
        if (declaration.declaration != nullptr) {
            return ExportHit{declaration.declaration.get(),
                             declarationName(*declaration.declaration)};
        }
        if (declaration.defaultExpr == nullptr) continue;
        if (declaration.defaultExpr->kind == NodeKind::Identifier) {
            return ExportHit{
                declaration.defaultExpr.get(),
                static_cast<const Identifier&>(*declaration.defaultExpr).name};
        }
        return ExportHit{declaration.defaultExpr.get(), std::string()};
    }
    return {};
}

// The exported declaration a local import should jump to.
ExportHit findExported(const Program& program, const std::string& name,
                       bool wantDefault) {
    if (wantDefault) return findDefault(program);
    const ExportHit direct = findDirect(program, name);
    if (direct.node != nullptr) return direct;
    // `export { local as name }` renames a local declaration.
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || statement->kind != NodeKind::ExportDeclaration) {
            continue;
        }
        const auto& declaration = static_cast<const ExportDeclaration&>(*statement);
        for (const ExportSpecifierPtr& specifier : declaration.specifiers) {
            if (specifier == nullptr || specifier->exported != name) continue;
            if (specifier->local.empty() || specifier->local == name) return {};
            return findDirect(program, specifier->local);
        }
    }
    return {};
}

std::optional<lsp::SourceTarget> targetOf(const SourceSpan& span) {
    if (!span.valid() || span.file == nullptr) return std::nullopt;
    lsp::SourceTarget target;
    target.path = span.file->path;
    target.line = span.begin.line;
    target.column = span.begin.column;
    target.endLine = span.end.line;
    target.endColumn = span.end.column;
    return target;
}

// The import declaration that binds `name` in this unit's file, or nullptr.
const ImportDeclaration* importForName(const Program& program,
                                       const std::string& name,
                                       const SourceFile& file) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr || statement->kind != NodeKind::ImportDeclaration) {
            continue;
        }
        if (!statement->span.valid() || statement->span.file != &file) continue;
        const auto& import = static_cast<const ImportDeclaration&>(*statement);
        if (import.defaultBinding == name || import.namespaceBinding == name ||
            import.alias == name) {
            return &import;
        }
        for (const ImportSpecifierPtr& specifier : import.specifiers) {
            if (specifier == nullptr) continue;
            const std::string& local =
                specifier->local.empty() ? specifier->imported : specifier->local;
            if (local == name) return &import;
        }
    }
    return nullptr;
}

// True when `symbol` is one of the bindings the import statement itself
// declares. Named specifiers get a span on the identifier while default
// bindings may carry the statement span, so containment (not equality) is the
// test; a local declaration that shadows the import starts after the
// statement's end and is therefore not an import binding.
bool isImportBinding(const SymbolInfo& symbol, const ImportDeclaration& import) {
    if (!symbol.span.valid() || symbol.span.file != import.span.file) return false;
    return symbol.span.begin.offset >= import.span.begin.offset &&
           symbol.span.begin.offset < import.span.end.offset;
}

const ModuleDependency* dependencyFor(const ModuleGraph& graph,
                                      std::uint32_t importer,
                                      const std::string& specifier) {
    for (const ModuleDependency& dependency : graph.dependencies()) {
        if (dependency.importer == importer && dependency.specifier == specifier) {
            return &dependency;
        }
    }
    return nullptr;
}

// The imported name's declaration in the target module, or nothing (std,
// package, native, cpp and unresolved imports answer no location).
std::vector<lsp::SourceTarget> importedTargets(
    const AnalyzedDocument& document, const ImportDeclaration& import,
    const std::string& localName) {
    std::vector<lsp::SourceTarget> targets;
    if (document.graph == nullptr || document.entry == nullptr) return targets;
    const ModuleDependency* dependency =
        dependencyFor(*document.graph, document.entry->id, import.source);
    if (dependency == nullptr || !dependency->resolved ||
        dependency->resolution != ModuleResolution::Local) {
        return targets;
    }
    CompilationUnit* target = document.graph->unit(dependency->imported);
    if (target == nullptr || target->ast == nullptr) return targets;
    const bool wantDefault = import.defaultBinding == localName;
    std::string importedName;
    bool matched = wantDefault;
    for (const ImportSpecifierPtr& specifier : import.specifiers) {
        if (specifier == nullptr) continue;
        const std::string& local =
            specifier->local.empty() ? specifier->imported : specifier->local;
        if (local != localName) continue;
        importedName = specifier->imported;
        matched = true;
        break;
    }
    if (!matched) return targets;  // namespace/alias binding: no single declaration
    const ExportHit hit = findExported(*target->ast, importedName, wantDefault);
    if (hit.node == nullptr) return targets;
    const std::optional<lsp::SourceTarget> found =
        targetOf(nameSpanIn(hit.node->span, hit.name));
    if (found.has_value()) targets.push_back(*found);
    return targets;
}

// Cursor on a declaration's own name: answer the declaration (a no-op jump
// that still tells the client "this is the definition").
std::vector<lsp::SourceTarget> selfTarget(const Node& node) {
    std::vector<lsp::SourceTarget> targets;
    std::string name;
    if (node.kind == NodeKind::FunctionDeclaration ||
        node.kind == NodeKind::ClassDeclaration ||
        node.kind == NodeKind::InterfaceDeclaration ||
        node.kind == NodeKind::EnumDeclaration ||
        node.kind == NodeKind::TypeAliasDeclaration) {
        name = declarationName(static_cast<const Stmt&>(node));
    } else if (node.kind == NodeKind::VariableDeclarator) {
        const auto& declarator = static_cast<const VariableDeclarator&>(node);
        if (declarator.id != nullptr && declarator.id->kind == NodeKind::Identifier) {
            name = static_cast<const Identifier&>(*declarator.id).name;
        }
    }
    if (name.empty()) return targets;
    const std::optional<lsp::SourceTarget> found =
        targetOf(nameSpanIn(node.span, name));
    if (found.has_value()) targets.push_back(*found);
    return targets;
}

}  // namespace

std::vector<lsp::SourceTarget> definitionAt(const std::string& path,
                                            const std::string& text,
                                            std::uint32_t line,
                                            std::uint32_t column) {
    std::vector<lsp::SourceTarget> targets;
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return targets;
    const Node* node =
        innermostNodeAt(*document->entry->ast, *document->file, line, column);
    if (node == nullptr) return targets;
    if (node->kind != NodeKind::Identifier) return selfTarget(*node);
    const std::string& name = static_cast<const Identifier&>(*node).name;
    const std::uint32_t offset = offsetOf(*document->file, line, column);
    const SymbolInfo* symbol =
        nearestSymbolAt(document->sema, name, *document->file, offset);
    const ImportDeclaration* import =
        importForName(*document->entry->ast, name, *document->file);
    if (import != nullptr &&
        (symbol == nullptr || isImportBinding(*symbol, *import))) {
        return importedTargets(*document, *import, name);
    }
    if (symbol != nullptr) {
        const std::optional<lsp::SourceTarget> found =
            targetOf(nameSpanIn(symbol->span, name));
        if (found.has_value()) targets.push_back(*found);
    }
    return targets;
}

}  // namespace cp3::cli
