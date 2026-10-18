// =============================================================================
// src/modules/src/support/export_surface.cpp - export surfaces + import validation.
//
// OWNERSHIP: modules module (src/modules/**). Implements the export_surface
// concern declared in detail/loader.h.
//
// A surface is collected straight from a unit's parsed AST: exported
// declarations (function/class/interface/enum/type alias/let), `export {a as b}`
// lists, `export default`, and `export * from` chains (the re-exported target is
// loaded and its named exports are unioned in; cycles yield partial surfaces,
// matching ES-module reality). Diagnostics are anchored at the import
// declaration itself (line-granular, DECISIONS D54) so every code lands on the
// import's own line/column.
// =============================================================================
#include "detail/loader.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/ast.hpp"
#include "cp3/diagnostic_codes.hpp"
#include "cp3/suggest.hpp"

#include "detail/file_paths.h"
#include "detail/specifier.h"
#include "detail/std_registry.h"

namespace cp3::modules {

namespace {

// The default-import / named-import / misspelling hint text shares one shape
// with sema's member misses: a single quoted list with an Oxford "or".
std::string quotedNames(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            const bool last = i + 1 == names.size();
            out += last ? ((names.size() == 2) ? " or " : ", or ") : ", ";
        }
        out += '\'';
        out += names[i];
        out += '\'';
    }
    return out;
}

// At most three names for a message hint (the full list would bury the point).
std::vector<std::string> firstThree(const std::vector<std::string>& names) {
    return std::vector<std::string>(names.begin(),
                                    names.begin() + static_cast<std::ptrdiff_t>(
                                                         std::min<std::size_t>(3, names.size())));
}

// The named export to import instead: the closest to `binding`, or the only
// export when the module has exactly one.
std::string pickNamedExport(const std::string& binding,
                            const std::vector<std::string>& exports) {
    const std::vector<std::string> ranked = suggestNames(binding, exports, 1);
    if (!ranked.empty()) return ranked.front();
    return exports.size() == 1 ? exports.front() : std::string();
}

// Every identifier a binding pattern introduces (`{a, b: c, ...rest}`, `[x]`).
void collectPatternNames(const Node& node, ExportSurface& out) {
    switch (node.kind) {
        case NodeKind::Identifier:
            out.named.insert(static_cast<const Identifier&>(node).name);
            return;
        case NodeKind::AssignmentPattern:
            if (const Node* left = static_cast<const AssignmentPattern&>(node).left.get()) {
                collectPatternNames(*left, out);
            }
            return;
        case NodeKind::RestElement:
            if (const Node* arg = static_cast<const RestElement&>(node).argument.get()) {
                collectPatternNames(*arg, out);
            }
            return;
        case NodeKind::ArrayPattern: {
            const auto& pattern = static_cast<const ArrayPattern&>(node);
            for (const NodePtr& element : pattern.elements) {
                if (element) collectPatternNames(*element, out);
            }
            if (pattern.rest) collectPatternNames(*pattern.rest, out);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& pattern = static_cast<const ObjectPattern&>(node);
            for (const ObjectPatternPropertyPtr& prop : pattern.properties) {
                if (prop && prop->value) collectPatternNames(*prop->value, out);
            }
            if (pattern.rest) collectPatternNames(*pattern.rest, out);
            return;
        }
        default:
            return;
    }
}

// The names `export <declaration>` makes visible.
void collectDeclaredNames(const Stmt& decl, ExportSurface& out) {
    switch (decl.kind) {
        case NodeKind::FunctionDeclaration:
            out.named.insert(static_cast<const FunctionDeclaration&>(decl).name);
            return;
        case NodeKind::ClassDeclaration:
            out.named.insert(static_cast<const ClassDeclaration&>(decl).name);
            return;
        case NodeKind::InterfaceDeclaration:
            out.named.insert(static_cast<const InterfaceDeclaration&>(decl).name);
            return;
        case NodeKind::EnumDeclaration:
            out.named.insert(static_cast<const EnumDeclaration&>(decl).name);
            return;
        case NodeKind::TypeAliasDeclaration:
            out.named.insert(static_cast<const TypeAliasDeclaration&>(decl).name);
            return;
        case NodeKind::VariableDeclaration:
            for (const VariableDeclaratorPtr& d :
                 static_cast<const VariableDeclaration&>(decl).declarations) {
                if (d && d->id) collectPatternNames(*d->id, out);
            }
            return;
        default:
            return;
    }
}

}  // namespace

const ExportSurface& ProgramLoader::surfaceOf(CompilationUnit& unit) {
    const auto it = surfaces_.find(unit.id);
    if (it != surfaces_.end()) return it->second;

    // A cycle through `export * from` chains, or a prelude that was never
    // parsed standalone, contributes nothing to the surface.
    static const ExportSurface emptySurface;
    if (!unit.ast || surfaceInProgress_.count(unit.id) != 0) return emptySurface;

    surfaceInProgress_.insert(unit.id);
    ExportSurface computed = computeSurface(unit);
    surfaceInProgress_.erase(unit.id);
    return surfaces_.emplace(unit.id, std::move(computed)).first->second;
}

ExportSurface ProgramLoader::computeSurface(CompilationUnit& unit) {
    ExportSurface out;
    if (!unit.ast) return out;
    for (const StmtPtr& stmt : unit.ast->body) {
        if (stmt && stmt->kind == NodeKind::ExportDeclaration) {
            applyExport(static_cast<const ExportDeclaration&>(*stmt), unit, out);
        }
    }
    return out;
}

void ProgramLoader::applyExport(const ExportDeclaration& decl, CompilationUnit& unit,
                                ExportSurface& out) {
    switch (decl.kind) {
        case ExportKind::Default:
            out.hasDefault = true;
            return;
        case ExportKind::Declaration:
            if (decl.declaration) collectDeclaredNames(*decl.declaration, out);
            return;
        case ExportKind::Named:
        case ExportKind::ReExportNamed:
            for (const ExportSpecifierPtr& spec : decl.specifiers) {
                if (spec) out.named.insert(spec->exported.empty() ? spec->local
                                                                  : spec->exported);
            }
            return;
        case ExportKind::ReExportAll:
            applyReExportAll(decl, unit, out);
            return;
    }
}

void ProgramLoader::applyReExportAll(const ExportDeclaration& decl, CompilationUnit& unit,
                                     ExportSurface& out) {
    if (!decl.namespaceExport.empty()) out.named.insert(decl.namespaceExport);
    const fs::path from = folderOfSource(decl.span.file ? *decl.span.file : *unit.file);
    if (classifyModuleSpecifier(decl.source) == ModuleResolution::Local) {
        if (CompilationUnit* target = loadLocal(decl.source, from, decl.span)) {
            const ExportSurface& targetSurface = surfaceOf(*target);
            out.named.insert(targetSurface.named.begin(), targetSurface.named.end());
        }
        return;
    }
    if (const ExportSurface* target = stdModuleSurface(decl.source)) {
        out.named.insert(target->named.begin(), target->named.end());
    }
}

const ExportSurface* ProgramLoader::stdSurface(const std::string& specifier,
                                               const SourceSpan& span) {
    const ExportSurface* surface = stdModuleSurface(specifier);
    if (surface == nullptr) {
        reportOnce(diagcode::kModuleNotFound, span,
                   "cannot find module '" + specifier + "'");
    }
    return surface;
}

void ProgramLoader::validateImport(const ImportDeclaration& decl,
                                   const ExportSurface& surface) {
    const std::vector<std::string> exports(surface.named.begin(),
                                           surface.named.end());
    if (!decl.defaultBinding.empty() && !surface.hasDefault) {
        std::string message =
            "module '" + decl.source + "' has no default export";
        std::vector<std::string> suggestions;
        if (!exports.empty()) {
            message += "; named exports: " + quotedNames(firstThree(exports));
            const std::string pick = pickNamedExport(decl.defaultBinding, exports);
            if (!pick.empty()) {
                const std::string form =
                    "import { " + pick + " } from \"" + decl.source + "\";";
                message += "; use a named import: `" + form + "`";
                suggestions.push_back("Use a named import: `" + form + "`");
            }
        }
        reportOnce(diagcode::kMissingDefaultExport, decl.span, message,
                   suggestions);
    }
    for (const ImportSpecifierPtr& spec : decl.specifiers) {
        if (!spec) continue;
        if (surface.named.count(spec->imported) == 0) {
            const std::string binding =
                spec->local.empty() ? spec->imported : spec->local;
            std::string message = "module '" + decl.source + "' does not export '" +
                                  spec->imported + "'";
            std::vector<std::string> suggestions;
            if (surface.hasDefault && exports.empty()) {
                // A default-only module (`std/console`): the named form is the
                // mistake, so hand over the default-import statement.
                const std::string form =
                    "import " + binding + " from \"" + decl.source + "\";";
                message += "; use the default import: `" + form + "`";
                suggestions.push_back("Use the default import: `" + form + "`");
            } else {
                const std::vector<std::string> ranked =
                    suggestNames(spec->imported, exports);
                if (!ranked.empty()) {
                    message += "; did you mean '" + ranked.front() + "'?";
                    suggestions.push_back(didYouMeanPhrase(ranked));
                }
                if (!exports.empty()) {
                    message += "; named exports: " + quotedNames(firstThree(exports));
                }
            }
            reportOnce(diagcode::kUnresolvedImport, decl.span, message,
                       suggestions);
        }
    }
}

}  // namespace cp3::modules
