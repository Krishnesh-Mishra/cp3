// =============================================================================
// src/cli/src/commands/lsp/intel/lsp_intel_references.cpp - references & rename.
//
// OWNERSHIP: cli module (src/cli/**). Implements referencesAt, prepareRenameAt
// and renameAt (detail/lsp_intel.hpp).
//
// The three answers come out of one resolution pipeline:
//   * the cursor must sit on an Identifier (a binding use or a declared name)
//     or on the NAME inside a declaration statement node whose name is a plain
//     string (function/class/interface/enum/type alias); member keys,
//     object-literal keys, specifier strings and type references are not
//     identifiers and are refused;
//   * the binding is resolved through `SemaResult::resolvedBindings` for uses,
//     or to the declaration node for declaration cursors (shadowing is the
//     compiler's own answer);
//   * an import-bound cursor resolves the way go-to-definition resolves it: a
//     Local edge leads to the declaring unit (re-export chains are followed);
//     std/package/native/unknown imports refuse rename (references answer the
//     local occurrences; an aliased/default/namespace binding renames locally
//     and provably cannot dangle another file);
//   * occurrences are identifiers bound to the declaration's node id (per-unit
//     sema, so shadowed same-name locals elsewhere are never collected) plus
//     the declaration's own name occurrence; names that must follow the
//     declaration across module boundaries (unaliased export specifiers, and
//     the exported name of the declaration itself) are rewritten in every unit
//     that imports or re-exports them;
//   * when any step cannot prove the binding, the whole answer is empty -
//     an empty rename is safe, a wrong rename is data loss.
// Type-position references (`x: combine`) carry no identifier node and are
// deliberately not collected.
// =============================================================================
#include "detail/lsp/lsp_intel.hpp"

#include <cstddef>
#include <cstdint>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/host/options.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

#include "detail/pkg/package_json.hpp"
#include "detail/toolchain/pipeline.hpp"

namespace cp3::cli {
namespace {

// One occurrence of the renaming target (a byte range plus the file it lives
// in). `isDeclaration` marks the binding site; rename rewrites every
// occurrence, references drops it when asked to.
struct Occurrence {
    const SourceFile* file = nullptr;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    bool isDeclaration = false;
};

void addOccurrence(std::vector<Occurrence>& occurrences, Occurrence occurrence) {
    if (occurrence.file == nullptr || occurrence.end <= occurrence.begin) return;
    for (Occurrence& existing : occurrences) {
        if (existing.file == occurrence.file &&
            existing.begin == occurrence.begin &&
            existing.end == occurrence.end) {
            existing.isDeclaration = existing.isDeclaration || occurrence.isDeclaration;
            return;
        }
    }
    occurrences.push_back(occurrence);
}

void addOccurrence(std::vector<Occurrence>& occurrences, const SourceSpan& span,
                   bool isDeclaration) {
    if (!span.valid() || span.file == nullptr) return;
    Occurrence occurrence;
    occurrence.file = span.file;
    occurrence.begin = span.begin.offset;
    occurrence.end = span.end.offset;
    occurrence.isDeclaration = isDeclaration;
    addOccurrence(occurrences, occurrence);
}

bool isIdentByte(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '$';
}

// Carve `name` inside `span`, requiring identifier-boundary bytes around the
// occurrence (an occurrence embedded in a longer identifier is skipped).
// Returns false when there is no such occurrence.
bool carveName(const SourceSpan& span, const std::string& name, SourceSpan* out) {
    if (!span.valid() || span.file == nullptr || name.empty() || out == nullptr) {
        return false;
    }
    const std::string& text = span.file->text;
    const std::uint32_t limit = span.end.offset;
    const std::size_t wanted = name.size();
    std::uint32_t at = span.begin.offset;
    for (;;) {
        const std::size_t hit = text.find(name, at);
        if (hit == std::string::npos) return false;
        if (hit >= limit || hit + wanted > limit) return false;
        const bool leftOk = hit == 0 ||
            !isIdentByte(static_cast<unsigned char>(text[hit - 1]));
        const bool rightOk = hit + wanted >= text.size() ||
            !isIdentByte(static_cast<unsigned char>(text[hit + wanted]));
        if (leftOk && rightOk) {
            SourceSpan found;
            found.file = span.file;
            found.begin = span.file->positionAt(static_cast<std::uint32_t>(hit));
            found.end =
                span.file->positionAt(static_cast<std::uint32_t>(hit + wanted));
            *out = found;
            return true;
        }
        at = static_cast<std::uint32_t>(hit + 1);
    }
}

bool spanCovers(const SourceSpan& outer, const SourceSpan& inner) {
    if (!outer.valid() || !inner.valid() || outer.file != inner.file) return false;
    return outer.begin.offset <= inner.begin.offset &&
           inner.end.offset <= outer.end.offset;
}

// The name a declaration node whose name is a plain string carries (the kinds
// that have no Identifier node at the declaration site).
std::string declarationNameOf(const Node& node) {
    switch (node.kind) {
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(node).name;
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(node).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(node).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(node).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(node).name;
        default:
            return std::string();
    }
}

const ModuleDependency* findDependency(const ModuleGraph& graph,
                                       std::uint32_t importer,
                                       const std::string& specifier) {
    for (const ModuleDependency& dependency : graph.dependencies()) {
        if (dependency.importer == importer && dependency.specifier == specifier) {
            return &dependency;
        }
    }
    return nullptr;
}

// The declaration node for `name` directly inside `program`, or nullptr.
// Returns the statement for Function/Class/Interface/Enum/TypeAlias, the
// declarator for a variable binding.
const Node* directHitIn(const Program& program, const std::string& name) {
    const auto hitInStatement = [&name](const Stmt& statement) -> const Node* {
        switch (statement.kind) {
            case NodeKind::FunctionDeclaration:
            case NodeKind::ClassDeclaration:
            case NodeKind::InterfaceDeclaration:
            case NodeKind::EnumDeclaration:
            case NodeKind::TypeAliasDeclaration:
                return declarationNameOf(statement) == name ? &statement : nullptr;
            case NodeKind::VariableDeclaration: {
                const auto& declaration =
                    static_cast<const VariableDeclaration&>(statement);
                for (const VariableDeclaratorPtr& declarator :
                     declaration.declarations) {
                    if (declarator == nullptr || declarator->id == nullptr) continue;
                    if (declarator->id->kind != NodeKind::Identifier) continue;
                    if (static_cast<const Identifier&>(*declarator->id).name !=
                        name) {
                        continue;
                    }
                    return declarator.get();
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
    };
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr) continue;
        const Node* hit = hitInStatement(*statement);
        if (hit != nullptr) return hit;
        if (statement->kind != NodeKind::ExportDeclaration) continue;
        const auto& declaration = static_cast<const ExportDeclaration&>(*statement);
        if (declaration.declaration == nullptr) continue;
        hit = hitInStatement(*declaration.declaration);
        if (hit != nullptr) return hit;
    }
    return nullptr;
}

// The terminal declaration for `name` exported by `unit`, following re-export
// chains (`export { x } from "./m"`). std/package/native/unknown hops refuse.
struct ChainTarget {
    CompilationUnit* unit = nullptr;
    const Node* declaration = nullptr;
    std::string name;
};

std::optional<ChainTarget> resolveExportedChain(const ModuleGraph& graph,
                                                CompilationUnit* unit,
                                                std::string name) {
    const std::vector<CompilationUnit*>& units = graph.units();
    for (std::size_t hops = 0; hops <= units.size(); ++hops) {
        if (unit == nullptr || unit->ast == nullptr) return std::nullopt;
        if (const Node* hit = directHitIn(*unit->ast, name)) {
            return ChainTarget{unit, hit, name};
        }
        // An alias over a local declaration (`export { combine as x }`) in the
        // same unit.
        bool followed = false;
        for (const StmtPtr& statement : unit->ast->body) {
            if (statement == nullptr ||
                statement->kind != NodeKind::ExportDeclaration) {
                continue;
            }
            const auto& declaration =
                static_cast<const ExportDeclaration&>(*statement);
            for (const ExportSpecifierPtr& specifier : declaration.specifiers) {
                if (specifier == nullptr || specifier->exported != name) continue;
                if (declaration.source.empty()) {
                    if (const Node* hit = directHitIn(*unit->ast, specifier->local)) {
                        return ChainTarget{unit, hit, specifier->local};
                    }
                    return std::nullopt;
                }
                const ModuleDependency* dependency = findDependency(
                    graph, unit->id, declaration.source);
                if (dependency == nullptr || !dependency->resolved ||
                    dependency->resolution != ModuleResolution::Local) {
                    return std::nullopt;
                }
                unit = graph.unit(dependency->imported);
                name = specifier->local;
                followed = true;
                break;
            }
            if (followed) break;
        }
        if (!followed) return std::nullopt;
    }
    return std::nullopt;  // hop bound exceeded: refuse
}

// Which names a declaration is exported under from its own unit.
//
//   * `export function combine` (or `export let combine = ...`) exports the
//     declaration under its own name - those names must follow a rename.
//   * `export { combine }` renames a local declaration and exports it under a
//     stable alias when the specifier spells the alias out (`{ combine as c }`);
//     the `local` occurrence always rewrites (it refers to the binding).
struct ExportForms {
    std::vector<std::string> follows;
    std::vector<std::string> stable;
    std::vector<Occurrence> localOccurrences;
};

void collectExportForms(const Program& program, const Node* declaration,
                        const std::string& name, ExportForms& forms) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr ||
            statement->kind != NodeKind::ExportDeclaration) {
            continue;
        }
        const auto& exporter = static_cast<const ExportDeclaration&>(*statement);
        if (exporter.kind == ExportKind::Named ||
            exporter.kind == ExportKind::ReExportNamed) {
            // A source ("./m") refers to another module's export; the importer
            // walk covers it for units that import from this unit.
            if (!exporter.source.empty()) continue;
            for (const ExportSpecifierPtr& specifier : exporter.specifiers) {
                if (specifier == nullptr || specifier->local != name) continue;
                SourceSpan carve;
                if (carveName(specifier->span, specifier->local, &carve)) {
                    Occurrence occurrence;
                    occurrence.file = carve.file;
                    occurrence.begin = carve.begin.offset;
                    occurrence.end = carve.end.offset;
                    occurrence.isDeclaration = false;
                    addOccurrence(forms.localOccurrences, occurrence);
                }
                const bool implicitSingle =
                    specifier->exported == specifier->local &&
                    specifier->span.length() == specifier->local.size();
                if (implicitSingle) {
                    forms.follows.push_back(specifier->exported);
                } else {
                    forms.stable.push_back(specifier->exported);
                }
            }
            continue;
        }
        if ((exporter.kind == ExportKind::Declaration ||
             exporter.kind == ExportKind::Default) &&
            exporter.declaration != nullptr && declaration != nullptr &&
            spanCovers(exporter.declaration->span, declaration->span)) {
            if (exporter.kind == ExportKind::Declaration) {
                forms.follows.push_back(name);
            }
            // A default export is nameless: importing units keep any alias.
        }
    }
    std::set<std::string> seen;
    forms.follows.erase(
        std::remove_if(forms.follows.begin(), forms.follows.end(),
                       [&seen](const std::string& value) {
                           return !seen.insert(value).second;
                       }),
        forms.follows.end());
    seen.clear();
    forms.stable.erase(
        std::remove_if(forms.stable.begin(), forms.stable.end(),
                       [&seen](const std::string& value) {
                           return !seen.insert(value).second;
                       }),
        forms.stable.end());
}

// The import declaration whose span contains `symbol` (when `symbol` was
// published from an import statement), or nullptr.
const ImportDeclaration* containingImport(const Program& program,
                                          const SymbolInfo& symbol,
                                          const SourceFile& file) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr ||
            statement->kind != NodeKind::ImportDeclaration) {
            continue;
        }
        if (!statement->span.valid() || statement->span.file != &file) continue;
        const auto& import = static_cast<const ImportDeclaration&>(*statement);
        if (!symbol.span.valid() || symbol.span.file != import.span.file) continue;
        if (symbol.span.begin.offset >= import.span.begin.offset &&
            symbol.span.begin.offset < import.span.end.offset) {
            return &import;
        }
    }
    return nullptr;
}

// How the import the cursor landed on binds its name.
struct ImportBindingSite {
    const ImportDeclaration* import = nullptr;
    const ImportSpecifier* specifier = nullptr;  // named bindings only
    bool isNamed = false;
    bool isAliased = false;    // `import { combine as c }` (local != imported)
    bool isDefault = false;    // `import def from "..."`
    bool isNamespace = false;  // `import * as ns from "..."`
    bool isAlias = false;      // `import "..." as x` / `import native "..." as x`
};

ImportBindingSite classifyImportBinding(const Program& program,
                                        const SymbolInfo& symbol,
                                        std::uint32_t declNodeId,
                                        const SourceFile& file) {
    ImportBindingSite site;
    const ImportDeclaration* import = containingImport(program, symbol, file);
    if (import == nullptr) return site;
    for (const ImportSpecifierPtr& specifier : import->specifiers) {
        if (specifier == nullptr) continue;
        const std::string& local =
            specifier->local.empty() ? specifier->imported : specifier->local;
        if (local == symbol.name && specifier->nodeId == declNodeId) {
            site.import = import;
            site.specifier = specifier.get();
            site.isNamed = true;
            site.isAliased = !specifier->local.empty() &&
                             specifier->local != specifier->imported;
            return site;
        }
    }
    if (import->defaultBinding == symbol.name &&
        declNodeId == import->nodeId) {
        site.import = import;
        site.isDefault = true;
        return site;
    }
    if (import->namespaceBinding == symbol.name &&
        declNodeId == import->nodeId) {
        site.import = import;
        site.isNamespace = true;
        return site;
    }
    if (import->alias == symbol.name && declNodeId == import->nodeId) {
        site.import = import;
        site.isAlias = true;
        return site;
    }
    return ImportBindingSite{};
}

// The target the cursor resolved to, or nothing.
struct ResolvedTarget {
    std::string name;
    CompilationUnit* declaringUnit = nullptr;
    std::uint32_t declNodeId = 0;        // in the declaring unit's sema space
    const Node* declaration = nullptr;   // declaration node in that unit
    bool isBindingLocal = false;         // aliased/default/namespace binding
    bool isNonLocal = false;             // through-import into a non-Local module
    std::vector<std::string> follows;    // export names importers must rewrite
    std::vector<Occurrence> occurrences; // found while resolving
};

// Whether the cursor sits on an identifier that is the KEY of a member or an
// object-literal property: those are not bindings, sema never resolves them,
// and the nearest same-name symbol fallback would rename the wrong thing.
void collectKeySpans(const Program& program, std::vector<SourceSpan>& spans) {
    forEachNode(program, [&](const Node& node) {
        const Expr* key = nullptr;
        bool computed = false;
        switch (node.kind) {
            case NodeKind::PropertyDeclaration: {
                const auto& member = static_cast<const PropertyDeclaration&>(node);
                key = member.key.get();
                computed = member.computed;
                break;
            }
            case NodeKind::MethodDeclaration: {
                const auto& member = static_cast<const MethodDeclaration&>(node);
                key = member.key.get();
                computed = member.computed;
                break;
            }
            case NodeKind::AccessorDeclaration: {
                const auto& member = static_cast<const AccessorDeclaration&>(node);
                key = member.key.get();
                computed = member.computed;
                break;
            }
            case NodeKind::PropertySignature: {
                const auto& member = static_cast<const PropertySignature&>(node);
                key = member.key.get();
                computed = member.computed;
                break;
            }
            case NodeKind::MethodSignature: {
                const auto& member = static_cast<const MethodSignature&>(node);
                key = member.key.get();
                computed = member.computed;
                break;
            }
            case NodeKind::ObjectProperty: {
                const auto& member = static_cast<const ObjectProperty&>(node);
                key = member.key.get();
                computed = member.computed;
                if (member.shorthand) return;  // `{ combine }` binds `combine`
                break;
            }
            case NodeKind::ObjectPatternProperty: {
                const auto& member =
                    static_cast<const ObjectPatternProperty&>(node);
                key = member.key.get();
                computed = member.computed;
                if (member.shorthand) return;  // `{ combine }` declares `combine`
                break;
            }
            default:
                return;
        }
        if (!computed && key != nullptr && key->kind == NodeKind::Identifier) {
            spans.push_back(key->span);
        }
    });
}

bool spanEquals(const SourceSpan& span, const SourceSpan& other) {
    return span.file == other.file && span.begin.offset == other.begin.offset &&
           span.end.offset == other.end.offset;
}

bool isKeySpan(const std::vector<SourceSpan>& spans, const SourceSpan& span) {
    for (const SourceSpan& key : spans) {
        if (spanEquals(key, span)) return true;
    }
    return false;
}

// One unit to answer occurrences from. The entry unit borrows the analysis
// analyzeDocument already produced; dependencies are analyzed fresh with
// nullptr imports (per-unit nodeIds).
struct UnitView {
    CompilationUnit* unit = nullptr;
    const SourceFile* file = nullptr;
    const SemaResult* sema = nullptr;
    std::unique_ptr<SemaResult> owned;
};

UnitView analyzeUnitFor(AnalyzedDocument& document, CompilationUnit* unit,
                        const CompileOptions& compile,
                        const std::unordered_map<std::uint32_t, SemaResult>*
                            unitSemas) {
    UnitView view;
    view.unit = unit;
    view.file = unit != nullptr ? unit->file : nullptr;
    if (unit != nullptr && unit->isEntry) {
        view.sema = &document.sema;
        return view;
    }
    // The graph analysis resolved the unit's imports, so the names it imports
    // are bound: a per-unit analysis without them cannot see the uses of an
    // imported binding at all.
    if (unitSemas != nullptr && unit != nullptr) {
        const auto it = unitSemas->find(unit->id);
        if (it != unitSemas->end()) {
            view.owned = std::make_unique<SemaResult>(it->second);
            view.sema = view.owned.get();
            return view;
        }
    }
    view.owned = std::make_unique<SemaResult>();
    CollectingDiagnosticSink sink;
    if (unit != nullptr && unit->ast != nullptr) {
        analyzeProgram(*unit->ast, *view.owned, sink, compile, nullptr);
    }
    view.sema = view.owned.get();
    return view;
}

// The sema of every unit of the graph, analyzed in dependency order with the
// imports resolved.
std::unordered_map<std::uint32_t, SemaResult> analyzeGraphUnits(
    const AnalyzedDocument& document, const CompileOptions& compile) {
    std::unordered_map<std::uint32_t, SemaResult> unitSemas;
    if (document.graph == nullptr) return unitSemas;
    SemaResult entrySema;
    CollectingDiagnosticSink sink;
    analyzeProgramGraph(*document.graph, entrySema, unitSemas, sink, compile);
    return unitSemas;
}

CompileOptions compileOptionsFor(const AnalyzedDocument& document,
                                 const std::string& path) {
    CompileOptions compile;
    std::filesystem::path root;
    if (findPackageRoot(std::filesystem::path(path).parent_path(), root)) {
        applyProjectTypeSettings(root, compile);
    }
    if (document.graph != nullptr) {
        compile.nativeBindings = document.graph->nativeBindings();
    }
    return compile;
}

// Every identifier in `view` that resolved to `declNodeId`.
void collectBoundUses(const Program& program, const SourceFile& file,
                      const SemaResult& sema, std::uint32_t declNodeId,
                      std::vector<Occurrence>& out) {
    forEachNode(program, [&](const Node& node) {
        if (node.kind != NodeKind::Identifier) return;
        if (!node.span.valid() || node.span.file != &file) return;
        const auto it = sema.resolvedBindings.find(node.nodeId);
        if (it == sema.resolvedBindings.end() || it->second != declNodeId) return;
        addOccurrence(out, node.span, /*isDeclaration=*/false);
    });
}

// Destructured binding sites can reference no unique binding when a pattern
// declares several binders (sema keys the symbol table by the declarator id
// and the last published binder wins). The spans of every multi-or-single
// pattern we cannot prove are collected here; `resolveTarget` verifies a
// single-binder pattern explicitly instead of trusting the nearest match.
void collectPatternBindingSpans(const Program& program,
                                std::vector<SourceSpan>& spans) {
    forEachNode(program, [&](const Node& node) {
        const Node* pattern = nullptr;
        if (node.kind == NodeKind::VariableDeclarator) {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            if (declarator.id != nullptr &&
                declarator.id->kind != NodeKind::Identifier) {
                pattern = declarator.id.get();
            }
        } else if (node.kind == NodeKind::Parameter) {
            const auto& parameter = static_cast<const Parameter&>(node);
            if (parameter.name != nullptr &&
                parameter.name->kind != NodeKind::Identifier) {
                pattern = parameter.name.get();
            }
        } else if (node.kind == NodeKind::ForStatement) {
            const auto& statement = static_cast<const ForStatement&>(node);
            if (statement.init != nullptr &&
                statement.init->kind != NodeKind::VariableDeclaration &&
                statement.init->kind != NodeKind::expressionstatement &&
                statement.init->kind != NodeKind::Identifier) {
                pattern = statement.init.get();
            }
        } else if (node.kind == NodeKind::ForOfStatement) {
            const auto& statement = static_cast<const ForOfStatement&>(node);
            if (statement.left != nullptr &&
                statement.left->kind != NodeKind::VariableDeclaration &&
                statement.left->kind != NodeKind::Identifier) {
                pattern = statement.left.get();
            }
        } else if (node.kind == NodeKind::ForInStatement) {
            const auto& statement = static_cast<const ForInStatement&>(node);
            if (statement.left != nullptr &&
                statement.left->kind != NodeKind::VariableDeclaration &&
                statement.left->kind != NodeKind::Identifier) {
                pattern = statement.left.get();
            }
        }
        if (pattern != nullptr) spans.push_back(pattern->span);
    });
}

bool insidePatternBinding(const std::vector<SourceSpan>& spans,
                          const SourceSpan& span) {
    for (const SourceSpan& pattern : spans) {
        if (spanCovers(pattern, span)) return true;
    }
    return false;
}

// The reserved words of the language (src/lexer/src/keywords.cpp owns the
// authoritative table): a rename can never produce one of these names. The
// cli cannot see the lexer's private header, and the list is closed, so it is
// mirrored here for `resolveQuery`'s use.
bool isCp3ReservedWord(const std::string& text) {
    static const std::unordered_set<std::string_view> kReserved = {
        "let", "var", "const", "function", "return", "if", "else", "while",
        "do", "for", "of", "in", "switch", "case", "default", "break",
        "continue", "throw", "try", "catch", "finally", "class", "interface",
        "extends", "implements", "abstract", "static", "public", "private",
        "protected", "enum", "new", "this", "import", "export", "from",
        "as", "native", "async", "await", "weak", "ref", "instanceof",
        "typeof", "get", "set", "true", "false", "null", "undefined",
        "void", "unknown", "type", "any", "delete", "yield",
    };
    return kReserved.find(text) != kReserved.end();
}

// The declarator/parameter node whose id/name subtree encloses `span`, or
// nullptr.
const Node* enclosingBinding(const Program& program, const SourceSpan& span) {
    const Node* found = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (found != nullptr) return;
        const Node* binderRoot = nullptr;
        if (node.kind == NodeKind::VariableDeclarator) {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            binderRoot = declarator.id.get();
        } else if (node.kind == NodeKind::Parameter) {
            const auto& parameter = static_cast<const Parameter&>(node);
            binderRoot = parameter.name.get();
        }
        if (binderRoot != nullptr && spanCovers(binderRoot->span, span)) {
            found = &node;
        }
    });
    return found;
}

// The spans of every Identifier inside `root`, deduplicated by span. A
// shorthand prop contributes one entry (key and value share a span).
std::vector<SourceSpan> identifierSpansIn(const Program& program,
                                          const SourceSpan& root) {
    std::vector<SourceSpan> spans;
    forEachNode(program, [&](const Node& node) {
        if (node.kind != NodeKind::Identifier || !node.span.valid()) return;
        if (!spanCovers(root, node.span)) return;
        for (const SourceSpan& existing : spans) {
            if (spanEquals(existing, node.span)) return;
        }
        spans.push_back(node.span);
    });
    return spans;
}

// The declaration node for `declNodeId` inside `program`, or nullptr. Per-unit
// nodeIds make the id unique in its own unit.
const Node* findNodeById(const Program& program, std::uint32_t declNodeId) {
    const Node* found = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (node.nodeId == declNodeId) found = &node;
    });
    return found;
}

// Complete `target` for a declaration resolved inside its unit (`declaration`,
// `program`): carve the declaration occurrence and collect the unit's own
// export forms. Returns false when the declaration's name cannot be carved;
// the query must refuse in that case.
bool finishDeclaringUnit(ResolvedTarget& target, const Node& declaration,
                         const Program& program) {
    SourceSpan declarationSpan;
    if (!carveName(declaration.span, target.name, &declarationSpan)) {
        target.declaration = nullptr;
        return false;
    }
    addOccurrence(target.occurrences, declarationSpan,
                  /*isDeclaration=*/true);
    target.declaration = &declaration;
    ExportForms forms;
    collectExportForms(program, &declaration, target.name, forms);
    for (Occurrence& occurrence : forms.localOccurrences) {
        addOccurrence(target.occurrences, occurrence);
    }
    target.follows = std::move(forms.follows);
    return true;
}

std::optional<ResolvedTarget> resolveTarget(AnalyzedDocument& document,
                                            std::uint32_t line,
                                            std::uint32_t column) {
    CompilationUnit* entry = document.entry;
    if (entry == nullptr || entry->ast == nullptr) return std::nullopt;
    const SourceFile& file = *document.file;
    const Program& program = *entry->ast;
    const std::uint32_t offset = offsetOf(file, line, column);
    std::vector<SourceSpan> keySpans;
    collectKeySpans(program, keySpans);
    std::vector<SourceSpan> patternSpans;
    collectPatternBindingSpans(program, patternSpans);

    const Node* node = innermostNodeAt(program, file, line, column);
    if (node == nullptr) return std::nullopt;

    ResolvedTarget target;
    if (node->kind != NodeKind::Identifier) {
        // A declaration node whose name is a plain string: the cursor is on
        // the declared name when the carved occurrence covers the position.
        const std::string name = declarationNameOf(*node);
        if (name.empty()) return std::nullopt;
        SourceSpan nameSpan;
        if (!carveName(node->span, name, &nameSpan)) return std::nullopt;
        if (!spanContains(nameSpan, file, line, column)) return std::nullopt;
        target.name = name;
        target.declaringUnit = entry;
        target.declNodeId = node->nodeId;
        if (!finishDeclaringUnit(target, *node, program)) return std::nullopt;
        return target;
    }

    const Identifier& identifier = static_cast<const Identifier&>(*node);
    if (isKeySpan(keySpans, identifier.span)) return std::nullopt;  // member/key name
    const bool inPattern = insidePatternBinding(patternSpans, identifier.span);
    const std::string& name = identifier.name;

    // Resolve the binding: resolvedBindings for uses (the compiler's own
    // shadowing answer), else the nearest declaration at or before the cursor.
    // Inside a binding pattern the symbol entry is keyed by the declarator id
    // and holds one binder (the last published), so a name+span match is the
    // only way to prove which one the cursor is on.
    const SymbolInfo* symbol = nullptr;
    std::uint32_t declNodeId = 0;
    if (inPattern) {
        const Node* binding = enclosingBinding(program, identifier.span);
        if (binding == nullptr) return std::nullopt;
        const Node* binderRoot = nullptr;
        if (binding->kind == NodeKind::VariableDeclarator) {
            binderRoot = static_cast<const VariableDeclarator&>(*binding).id.get();
        } else if (binding->kind == NodeKind::Parameter) {
            binderRoot = static_cast<const Parameter&>(*binding).name.get();
        }
        if (binderRoot == nullptr) return std::nullopt;
        // Several binders share the declarator/parameter node id in the
        // symbol table (one entry, last published wins), so occurrences
        // bound to that id cannot tell the binders apart. Each binder must
        // have a distinct declaration site; more than one distinct binder
        // means the rename cannot be proven safe.
        const std::vector<SourceSpan> binders =
            identifierSpansIn(program, binderRoot->span);
        if (binders.size() != 1) return std::nullopt;
        const auto it = document.sema.symbols.find(binding->nodeId);
        if (it == document.sema.symbols.end()) return std::nullopt;
        if (it->second.name != name) return std::nullopt;
        if (!spanCovers(it->second.span, identifier.span)) return std::nullopt;
        symbol = &it->second;
        declNodeId = binding->nodeId;
    } else if (const auto it = document.sema.resolvedBindings.find(identifier.nodeId);
        it != document.sema.resolvedBindings.end()) {
        declNodeId = it->second;
        const auto found = document.sema.symbols.find(declNodeId);
        if (found != document.sema.symbols.end()) symbol = &found->second;
    } else {
        symbol = nearestSymbolAt(document.sema, name, file, offset);
        if (symbol != nullptr) declNodeId = symbol->declNodeId;
    }
    if (symbol == nullptr && declNodeId == 0) return std::nullopt;

    // Import binding?
    const ImportBindingSite site =
        symbol != nullptr
            ? classifyImportBinding(program, *symbol, declNodeId, file)
            : ImportBindingSite{};
    if (site.import == nullptr) {
        // An ordinary local declaration in the entry unit. The symbols span is
        // the identifier for variable bindings and the whole declaration for
        // everything else; the declaration node is found by id so duplicate
        // names cannot pick the wrong declarator.
        target.name = symbol != nullptr ? symbol->name : name;
        target.declaringUnit = entry;
        target.declNodeId = declNodeId;
        const Node* declaration = findNodeById(program, declNodeId);
        if (declaration == nullptr) return std::nullopt;
        if (!finishDeclaringUnit(target, *declaration, program)) {
            return std::nullopt;
        }
        return target;
    }

    // Import-bound cursor.
    const ModuleDependency* dependency =
        document.graph != nullptr
            ? findDependency(*document.graph, entry->id, site.import->source)
            : nullptr;
    const bool localModule = dependency != nullptr && dependency->resolved &&
                             dependency->resolution == ModuleResolution::Local;

    if (site.isNamed && !site.isAliased && localModule) {
        // Through-resolution: rename the declaration in the providing unit.
        const std::string imported = site.specifier->imported;
        const std::optional<ChainTarget> chain =
            resolveExportedChain(*document.graph,
                                 document.graph->unit(dependency->imported),
                                 imported);
        if (!chain.has_value()) return std::nullopt;
        target.name = chain->name;
        target.declaringUnit = chain->unit;
        target.declaration = chain->declaration;
        target.declNodeId = chain->declaration->nodeId;
        if (!finishDeclaringUnit(target, *chain->declaration,
                                 *chain->unit->ast)) {
            return std::nullopt;
        }
        return target;
    }

    if (site.isNamed && !site.isAliased && !localModule) {
        // std/package/native/unknown: rename refuses; references answer with
        // the occurrences bound to this import in this unit.
        target.name = symbol->name;
        target.declaringUnit = entry;
        target.declNodeId = declNodeId;
        target.declaration = site.specifier;
        target.isNonLocal = true;
        SourceSpan carved;
        const std::string& local =
            site.specifier->local.empty() ? site.specifier->imported
                                          : site.specifier->local;
        if (carveName(site.specifier->span, local, &carved)) {
            addOccurrence(target.occurrences, carved,
                          /*isDeclaration=*/true);
        }
        return target;
    }

    // Aliased named imports and default/namespace/native aliases bind a
    // unit-local name; renaming it cannot dangle any other file.
    target.name = symbol->name;
    target.declaringUnit = entry;
    target.declNodeId = declNodeId;
    target.isBindingLocal = true;
    SourceSpan bindingSpan;
    bool carved = false;
    if (site.isNamed) {
        const std::string& local =
            site.specifier->local.empty() ? site.specifier->imported
                                          : site.specifier->local;
        carved = carveName(site.specifier->span, local, &bindingSpan);
    } else if (site.isDefault) {
        carved = carveName(site.import->span, site.import->defaultBinding,
                           &bindingSpan);
    } else if (site.isNamespace) {
        carved = carveName(site.import->span, site.import->namespaceBinding,
                           &bindingSpan);
    } else {
        carved = carveName(site.import->span, site.import->alias, &bindingSpan);
    }
    if (!carved) return std::nullopt;
    addOccurrence(target.occurrences, bindingSpan, /*isDeclaration=*/true);
    target.declaration = site.specifier != nullptr
                             ? static_cast<const Node*>(site.specifier)
                             : static_cast<const Node*>(site.import);
    return target;
}

bool fileContainsAny(const SourceFile* file, const std::vector<std::string>& names) {
    if (file == nullptr) return false;
    for (const std::string& name : names) {
        if (!name.empty() &&
            file->text.find(name) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// All occurrences of the target, including cross-unit import/export strings.
// `analyze` provides per-unit sema for units other than the entry.
std::vector<Occurrence> collectOccurrences(AnalyzedDocument& document,
                                           const std::string& path,
                                           const ResolvedTarget& target) {
    std::vector<Occurrence> occurrences = target.occurrences;
    const CompileOptions compile = compileOptionsFor(document, path);
    const std::unordered_map<std::uint32_t, SemaResult> unitSemas =
        analyzeGraphUnits(document, compile);

    // Uses in the declaring unit.
    {
        CompilationUnit* declaring = target.declaringUnit;
        const Program* program =
            declaring != nullptr ? declaring->ast.get() : nullptr;
        if (program != nullptr) {
            UnitView view = analyzeUnitFor(document, declaring, compile, &unitSemas);
            if (view.sema != nullptr) {
                collectBoundUses(*program, *view.file, *view.sema,
                                target.declNodeId, occurrences);
            }
        }
    }

    if (target.isBindingLocal || target.isNonLocal) {
        return occurrences;  // unit-local: no cross-file rewrite
    }

    // Uses and import/export strings in every other unit that could bind the
    // exported names.
    for (CompilationUnit* unit : document.graph->units()) {
        if (unit == nullptr) continue;
        if (unit == target.declaringUnit) continue;
        if (unit->isPrelude) continue;
        if (unit->ast == nullptr) continue;
        // Every importer binding we rewrite is named after an exported name,
        // so a unit that spells none of them cannot match.
        if (!fileContainsAny(unit->file, target.follows)) continue;
        bool hasEdge = false;
        for (const ModuleDependency& dependency : document.graph->dependencies()) {
            if (dependency.importer == unit->id && dependency.resolved &&
                dependency.resolution == ModuleResolution::Local &&
                dependency.imported == target.declaringUnit->id) {
                hasEdge = true;
                break;
            }
        }
        if (!hasEdge) continue;

        UnitView view = analyzeUnitFor(document, unit, compile, &unitSemas);
        if (view.sema == nullptr || unit->file == nullptr) continue;
        const Program& program = *unit->ast;
        const SourceFile& file = *unit->file;
        for (const ModuleDependency& dependency : document.graph->dependencies()) {
            if (dependency.importer != unit->id) continue;
            if (!dependency.resolved) continue;
            if (dependency.resolution != ModuleResolution::Local) continue;
            if (dependency.imported != target.declaringUnit->id) continue;
            for (const StmtPtr& statement : program.body) {
                if (statement == nullptr) continue;
                if (statement->kind == NodeKind::ImportDeclaration) {
                    const auto& import =
                        static_cast<const ImportDeclaration&>(*statement);
                    if (import.source != dependency.specifier) continue;
                    for (const ImportSpecifierPtr& specifier : import.specifiers) {
                        if (specifier == nullptr) continue;
                        const bool importedFollows =
                            std::find(target.follows.begin(), target.follows.end(),
                                      specifier->imported) != target.follows.end();
                        if (!importedFollows) continue;
                        SourceSpan carve;
                        if (carveName(specifier->span, specifier->imported, &carve)) {
                            addOccurrence(occurrences, carve,
                                          /*isDeclaration=*/false);
                        }
                        const bool aliased = !specifier->local.empty() &&
                                             specifier->local != specifier->imported;
                        if (aliased) continue;  // the alias is this unit's binding
                        const std::string local = specifier->local.empty()
                                                      ? specifier->imported
                                                      : specifier->local;
                        forEachNode(program, [&](const Node& node) {
                            if (node.kind != NodeKind::Identifier) return;
                            if (!node.span.valid() || node.span.file != &file) return;
                            const std::string& idName =
                                static_cast<const Identifier&>(node).name;
                            if (idName != local) return;
                            const auto it = view.sema->resolvedBindings.find(node.nodeId);
                            if (it == view.sema->resolvedBindings.end() ||
                                it->second != specifier->nodeId) {
                                return;
                            }
                            addOccurrence(occurrences, node.span,
                                          /*isDeclaration=*/false);
                        });
                    }
                    continue;
                }
                if (statement->kind == NodeKind::ExportDeclaration) {
                    const auto& exporter =
                        static_cast<const ExportDeclaration&>(*statement);
                    if (exporter.source.empty() ||
                        exporter.source != dependency.specifier) {
                        continue;
                    }
                    for (const ExportSpecifierPtr& specifier :
                         exporter.specifiers) {
                        if (specifier == nullptr) continue;
                        const bool localFollows =
                            std::find(target.follows.begin(), target.follows.end(),
                                      specifier->local) != target.follows.end();
                        if (!localFollows) continue;
                        SourceSpan carve;
                        if (carveName(specifier->span, specifier->local, &carve)) {
                            addOccurrence(occurrences, carve,
                                          /*isDeclaration=*/false);
                        }
                    }
                    continue;
                }
            }
        }
    }
    return occurrences;
}

lsp::SourceTarget toTarget(const Occurrence& occurrence) {
    const SourceFile* file = occurrence.file;
    const SourcePos begin = file->positionAt(occurrence.begin);
    const SourcePos end = file->positionAt(occurrence.end);
    lsp::SourceTarget target;
    target.path = file->path;
    target.line = begin.line;
    target.column = begin.column;
    target.endLine = end.line;
    target.endColumn = end.column;
    return target;
}

// Shared front half: analyze the document and resolve the cursor. The
// document must stay alive behind the target.
struct QueryResult {
    std::unique_ptr<AnalyzedDocument> document;
    std::optional<ResolvedTarget> target;
};

QueryResult resolveQuery(const std::string& path, const std::string& text,
                         std::uint32_t line, std::uint32_t column) {
    QueryResult result;
    result.document = analyzeDocument(path, text);
    if (result.document == nullptr || !analysisReady(*result.document)) {
        result.document.reset();
        return result;
    }
    result.target = resolveTarget(*result.document, line, column);
    return result;
}

std::vector<lsp::SourceTarget> toTargets(const std::vector<Occurrence>& occurrences,
                                         bool includeDeclaration) {
    std::vector<lsp::SourceTarget> targets;
    for (const Occurrence& occurrence : occurrences) {
        if (occurrence.file == nullptr) continue;
        if (occurrence.end <= occurrence.begin) continue;
        if (!includeDeclaration && occurrence.isDeclaration) continue;
        targets.push_back(toTarget(occurrence));
    }
    return targets;
}

}  // namespace

std::vector<lsp::SourceTarget> referencesAt(const std::string& path,
                                            const std::string& text,
                                            std::uint32_t line,
                                            std::uint32_t column,
                                            bool includeDeclaration) {
    QueryResult result = resolveQuery(path, text, line, column);
    if (result.document == nullptr || !result.target.has_value()) {
        if (result.document == nullptr) return std::vector<lsp::SourceTarget>();
        // The binding machinery gives no answer for a member position; the
        // member-aware fallback answers instead (empty means refused).
        if (std::optional<MemberRetarget> fallback =
                memberRetarget(*result.document, path, line, column)) {
            return memberOccurrences(*result.document, path, *fallback,
                                     includeDeclaration,
                                     MemberOccurrenceMode::References);
        }
        return std::vector<lsp::SourceTarget>();
    }
    const std::vector<Occurrence> occurrences =
        collectOccurrences(*result.document, path, *result.target);
    return toTargets(occurrences, includeDeclaration);
}

std::optional<lsp::SourceTarget> prepareRenameAt(const std::string& path,
                                                 const std::string& text,
                                                 std::uint32_t line,
                                                 std::uint32_t column) {
    QueryResult result = resolveQuery(path, text, line, column);
    if (result.document == nullptr || !result.target.has_value()) {
        if (result.document == nullptr) return std::nullopt;
        // Member position: prepareRename answers the member's own name span.
        if (std::optional<MemberRetarget> fallback =
                memberRetarget(*result.document, path, line, column)) {
            return fallback->declaration;
        }
        return std::nullopt;
    }
    if (result.target->isNonLocal) return std::nullopt;
    const std::vector<Occurrence> occurrences =
        collectOccurrences(*result.document, path, *result.target);
    for (const Occurrence& occurrence : occurrences) {
        if (occurrence.isDeclaration) return toTarget(occurrence);
    }
    return std::nullopt;
}

std::vector<lsp::SourceTarget> renameAt(const std::string& path,
                                        const std::string& text,
                                        std::uint32_t line,
                                        std::uint32_t column,
                                        const std::string& newName) {
    std::vector<lsp::SourceTarget> targets;
    if (newName.empty()) return targets;
    // The provider refuses to rename anything to a keyword of the language:
    // the token would tokenize as the keyword and the produced edit would
    // not compile. References/prepareRename need no such check.
    if (isCp3ReservedWord(newName)) return targets;
    QueryResult result = resolveQuery(path, text, line, column);
    if (result.document == nullptr || !result.target.has_value()) {
        if (result.document == nullptr) return targets;
        // Member position: the member fallback answers with the same-symbol
        // family; rename refuses (returns empty) when it cannot prove it.
        if (std::optional<MemberRetarget> fallback =
                memberRetarget(*result.document, path, line, column)) {
            return memberOccurrences(*result.document, path, *fallback,
                                     /*includeDeclaration=*/true,
                                     MemberOccurrenceMode::Rename);
        }
        return targets;
    }
    if (result.target->isNonLocal) return targets;
    const std::vector<Occurrence> occurrences =
        collectOccurrences(*result.document, path, *result.target);
    return toTargets(occurrences, /*includeDeclaration=*/true);
}

}  // namespace cp3::cli
