// =============================================================================
// src/cli/src/commands/lsp/intel/lsp_intel_hierarchy.cpp - type hierarchy.
//
// OWNERSHIP: cli module (src/cli/**). Implements typeHierarchyPrepareAt and
// typeHierarchyRelativesAt (detail/lsp_intel.hpp).
//
// Both answers come out of one declaration pipeline:
//   * the cursor must name a class/interface/enum/type-alias declaration.
//     It may sit on the declaration statement itself, on a use bound to one
//     (an identifier the compiler resolved to one - `new Box()`), or on a
//     type reference that names one (`extends Base`, `x: Sized`). A cursor
//     on anything else answers empty;
//   * the item carries the declaration statement span and the carved name
//     span, in compiler coordinates, so the client can round-trip it back
//     on the supertypes/subtypes requests;
//   * supertypes read the declaration's own bases (a class reads its single
//     extends type plus every implements type; an interface reads every
//     extends type; enums and aliases declare no bases) and resolve each
//     base name to a declaration with that literal name - first in the
//     current unit, then in every non-prelude unit of the module graph
//     (units whose text does not contain the name are skipped). Aliased
//     imports are deliberately not followed: a base must be a declaration
//     with the name it was written as;
//   * subtypes scan every non-prelude unit's top-level statements for a
//     declaration whose base list contains the item's name; names compare
//     textually - no sema is run for the scan;
//   * a base name that cannot be resolved is skipped silently, and any
//     failure answers empty - the server turns that into an empty LSP result.
// =============================================================================
#include "detail/lsp/lsp_intel.hpp"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

namespace cp3::cli {
namespace {

// LSP SymbolKind numbers used here: Class 5, Enum 10, Interface 11,
// TypeParameter 26 (type aliases).
constexpr int kKindClass = 5;
constexpr int kKindEnum = 10;
constexpr int kKindInterface = 11;
constexpr int kKindTypeAlias = 26;

bool isStringNameDeclaration(NodeKind kind) {
    switch (kind) {
        case NodeKind::ClassDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::TypeAliasDeclaration:
            return true;
        default:
            return false;
    }
}

// The name a declaration node whose name is a plain string carries (the
// kinds that have no Identifier node at the declaration site).
std::string hierarchyDeclarationName(const Node& node) {
    switch (node.kind) {
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

int hierarchyKindOf(NodeKind kind) {
    switch (kind) {
        case NodeKind::ClassDeclaration:
            return kKindClass;
        case NodeKind::InterfaceDeclaration:
            return kKindInterface;
        case NodeKind::EnumDeclaration:
            return kKindEnum;
        case NodeKind::TypeAliasDeclaration:
            return kKindTypeAlias;
        default:
            return kKindClass;
    }
}

std::string hierarchyDetailOf(NodeKind kind, const std::string& name) {
    switch (kind) {
        case NodeKind::ClassDeclaration:
            return "class " + name;
        case NodeKind::InterfaceDeclaration:
            return "interface " + name;
        case NodeKind::EnumDeclaration:
            return "enum " + name;
        case NodeKind::TypeAliasDeclaration:
            return "type " + name;
        default:
            return name;
    }
}

// True for the type nodes that carry a single head name (a named type or a
// parenthesized one). A union or an object type has no name to resolve.
bool isNamedTypeNode(NodeKind kind) {
    return kind == NodeKind::NamedType || kind == NodeKind::ParenthesizedType;
}

// The head name of a type node, or empty. `Box<number>` resolves to `Box`
// (the outer constructor), and parenthesized types are unwrapped. Union and
// other shape types have no single head name and are skipped. Type nodes
// never contain Identifier nodes, so there is no innermost-identifier
// fallback to fall back to.
std::string typeHeadName(const TypeNode& type) {
    switch (type.kind) {
        case NodeKind::NamedType:
            return static_cast<const NamedType&>(type).name;
        case NodeKind::ParenthesizedType: {
            const TypePtr& inner =
                static_cast<const ParenthesizedType&>(type).inner;
            return inner != nullptr ? typeHeadName(*inner) : std::string();
        }
        default:
            return std::string();
    }
}

void pushTypeHeadName(const TypeNode& type, std::vector<std::string>& out) {
    const std::string name = typeHeadName(type);
    if (name.empty()) return;
    if (std::find(out.begin(), out.end(), name) != out.end()) return;
    out.push_back(name);
}

// The base names a declaration declares. Enums and type aliases have none.
void collectParentNames(const Node& declaration, std::vector<std::string>& out) {
    switch (declaration.kind) {
        case NodeKind::ClassDeclaration: {
            const auto& klass = static_cast<const ClassDeclaration&>(declaration);
            if (klass.extendsType != nullptr) {
                pushTypeHeadName(*klass.extendsType, out);
            }
            for (const TypePtr& implemented : klass.implementsTypes) {
                if (implemented != nullptr) pushTypeHeadName(*implemented, out);
            }
            return;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& iface =
                static_cast<const InterfaceDeclaration&>(declaration);
            for (const TypePtr& extended : iface.extendsTypes) {
                if (extended != nullptr) pushTypeHeadName(*extended, out);
            }
            return;
        }
        default:
            return;
    }
}

// The declaration node for `nodeId` inside `program`, or nullptr. Per-unit
// nodeIds make the id unique in its own unit.
const Node* findNodeById(const Program& program, std::uint32_t nodeId) {
    const Node* found = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (node.nodeId == nodeId) found = &node;
    });
    return found;
}

// Visits the hierarchy declarations that appear directly in `program`'s
// body, including the ones wrapped in `export <declaration>`.
void forEachTopLevelStringNameDeclaration(
    const Program& program,
    const std::function<void(const Node&)>& visit) {
    for (const StmtPtr& statement : program.body) {
        if (statement == nullptr) continue;
        if (isStringNameDeclaration(statement->kind)) {
            visit(*statement);
            continue;
        }
        if (statement->kind != NodeKind::ExportDeclaration) continue;
        const auto& exporter = static_cast<const ExportDeclaration&>(*statement);
        if (exporter.declaration == nullptr) continue;
        if (!isStringNameDeclaration(exporter.declaration->kind)) continue;
        visit(*exporter.declaration);
    }
}

std::vector<const Node*> findStringNameDeclarations(const Program& program,
                                                    const std::string& name) {
    std::vector<const Node*> hits;
    forEachTopLevelStringNameDeclaration(program, [&](const Node& node) {
        if (hierarchyDeclarationName(node) == name) hits.push_back(&node);
    });
    return hits;
}

const Node* findStringNameDeclaration(const Program& program,
                                      const std::string& name) {
    const std::vector<const Node*> hits =
        findStringNameDeclarations(program, name);
    return hits.empty() ? nullptr : hits.front();
}

// The declaration named `name` in the analysed document: first in the entry
// unit, then in every other non-prelude unit of the module graph (units whose
// text does not contain the name are skipped). Aliased imports are
// deliberately not followed: a base must be a declaration with the name it
// was written as.
const Node* declarationNamed(const AnalyzedDocument& document,
                             const std::string& name) {
    if (name.empty()) return nullptr;
    if (document.entry == nullptr || document.entry->ast == nullptr) {
        return nullptr;
    }
    const Node* decl = findStringNameDeclaration(*document.entry->ast, name);
    if (decl != nullptr) return decl;
    if (document.graph == nullptr) return nullptr;
    for (CompilationUnit* candidate : document.graph->units()) {
        if (candidate == nullptr) continue;
        if (candidate == document.entry) continue;  // already searched
        if (candidate->isPrelude) continue;
        if (candidate->ast == nullptr || candidate->file == nullptr) continue;
        // A unit that spells the name nowhere cannot match.
        if (candidate->file->text.find(name) == std::string::npos) continue;
        decl = findStringNameDeclaration(*candidate->ast, name);
        if (decl != nullptr) return decl;
    }
    return nullptr;
}

// The hierarchy declaration a use names. The compiler's binding table is the
// first answer; `new Leaf()` resolves its callee to the class without
// publishing a binding for it, so the class name comes from the inferred
// type instead. A use bound to something else (a local, a parameter) names no
// type and answers nothing.
const Node* declarationForUse(const AnalyzedDocument& document,
                              const Program& program, const Node& node) {
    if (node.nodeId == 0) return nullptr;
    const auto bound = document.sema.resolvedBindings.find(node.nodeId);
    if (bound != document.sema.resolvedBindings.end()) {
        const Node* target = findNodeById(program, bound->second);
        if (target == nullptr) return nullptr;
        if (isStringNameDeclaration(target->kind)) return target;
        // An imported name: the binding points at the import specifier, so the
        // declaration is the one the module exports under that name.
        if (target->kind == NodeKind::ImportSpecifier) {
            const auto& specifier = static_cast<const ImportSpecifier&>(*target);
            return declarationNamed(document, specifier.imported);
        }
        return nullptr;
    }
    const auto typed = document.sema.nodeTypes.find(node.nodeId);
    if (typed == document.sema.nodeTypes.end()) return nullptr;
    if (typed->second.kind != TypeKind::Class) return nullptr;
    return declarationNamed(document, typed->second.name);
}

// The innermost (smallest-span) string-name declaration statement containing
// the position, or nullptr.
const Node* enclosingHierarchyDeclaration(const Program& program,
                                          const SourceFile& file,
                                          std::uint32_t line,
                                          std::uint32_t column) {
    const Node* best = nullptr;
    std::uint32_t bestLength = 0;
    forEachNode(program, [&](const Node& node) {
        if (!isStringNameDeclaration(node.kind)) return;
        if (!spanContains(node.span, file, line, column)) return;
        const std::uint32_t length = node.span.length();
        if (best == nullptr || length < bestLength) {
            best = &node;
            bestLength = length;
        }
    });
    return best;
}

// True when `decl` is the declaration the item describes (the client
// round-tripped our own serialization: the item's path is the file that
// holds the declaration and its position is the declaration span's start).
bool isSelfDeclaration(const Node& decl, const lsp::HierarchyEntry& item) {
    if (decl.span.file == nullptr) return false;
    if (decl.span.file->path != item.path) return false;
    return decl.span.begin.line == item.line &&
           decl.span.begin.column == item.column;
}

// Builds the entry for `decl`, carving the name span out of the declaration
// statement span. Returns nullopt when the name cannot be found inside the
// statement (the position would lie to the client).
std::optional<lsp::HierarchyEntry> hierarchyEntryFor(const Node& decl,
                                                     const std::string& name) {
    if (!decl.span.valid() || decl.span.file == nullptr) return std::nullopt;
    const SourceFile& file = *decl.span.file;
    const std::string_view declText = file.slice(decl.span);
    if (declText.find(name) == std::string_view::npos) return std::nullopt;
    const SourceSpan nameSpan = nameSpanIn(decl.span, name);
    if (!nameSpan.valid()) return std::nullopt;
    lsp::HierarchyEntry entry;
    entry.name = name;
    entry.kind = hierarchyKindOf(decl.kind);
    entry.detail = hierarchyDetailOf(decl.kind, name);
    entry.path = file.path;
    entry.line = decl.span.begin.line;
    entry.column = decl.span.begin.column;
    entry.endLine = decl.span.end.line;
    entry.endColumn = decl.span.end.column;
    entry.nameLine = nameSpan.begin.line;
    entry.nameColumn = nameSpan.begin.column;
    entry.nameEndLine = nameSpan.end.line;
    entry.nameEndColumn = nameSpan.end.column;
    return entry;
}

void dedupEntries(std::vector<lsp::HierarchyEntry>& entries) {
    std::set<std::string> seen;
    std::vector<lsp::HierarchyEntry> kept;
    kept.reserve(entries.size());
    for (lsp::HierarchyEntry& entry : entries) {
        const std::string key = entry.path + "\n" + std::to_string(entry.line) +
                                "\n" + std::to_string(entry.column);
        if (!seen.insert(key).second) continue;
        kept.push_back(std::move(entry));
    }
    entries = std::move(kept);
}

std::vector<lsp::HierarchyEntry> supertypesOf(const std::string& path,
                                              const std::string& text,
                                              const lsp::HierarchyEntry& item) {
    std::vector<lsp::HierarchyEntry> result;
    auto document = analyzeDocument(path, text);
    if (document == nullptr || !analysisReady(*document) ||
        document->entry == nullptr || document->entry->ast == nullptr ||
        item.name.empty()) {
        return result;
    }
    CompilationUnit* selfUnit = document->entry;
    const Program& selfProgram = *selfUnit->ast;
    // Re-find the declaration this item describes: an exact position match
    // wins (the item may be built from a stale snapshot), otherwise the
    // first declaration with the item's name.
    const Node* self = nullptr;
    const std::vector<const Node*> selfHits =
        findStringNameDeclarations(selfProgram, item.name);
    for (const Node* hit : selfHits) {
        if (hit->span.begin.line == item.line &&
            hit->span.begin.column == item.column) {
            self = hit;
            break;
        }
    }
    if (self == nullptr && !selfHits.empty()) self = selfHits.front();
    if (self == nullptr) return result;
    std::vector<std::string> parents;
    collectParentNames(*self, parents);
    for (const std::string& parent : parents) {
        // First in the current unit (any of the four declaration kinds).
        const Node* decl = findStringNameDeclaration(selfProgram, parent);
        if (decl == nullptr && document->graph != nullptr) {
            for (CompilationUnit* candidate : document->graph->units()) {
                if (candidate == nullptr) continue;
                if (candidate == selfUnit) continue;  // already searched
                if (candidate->isPrelude) continue;
                if (candidate->ast == nullptr || candidate->file == nullptr) {
                    continue;
                }
                // A unit that spells the name nowhere cannot match.
                if (candidate->file->text.find(parent) == std::string::npos) {
                    continue;
                }
                decl = findStringNameDeclaration(*candidate->ast, parent);
                if (decl != nullptr) break;
            }
        }
        if (decl == nullptr) continue;  // genuinely not found: skip silently
        if (isSelfDeclaration(*decl, item)) continue;
        const std::string name = hierarchyDeclarationName(*decl);
        std::optional<lsp::HierarchyEntry> built = hierarchyEntryFor(*decl, name);
        if (built.has_value()) result.push_back(std::move(*built));
    }
    dedupEntries(result);
    return result;
}

std::vector<lsp::HierarchyEntry> subtypesOf(const std::string& path,
                                            const std::string& text,
                                            const lsp::HierarchyEntry& item) {
    std::vector<lsp::HierarchyEntry> result;
    auto document = analyzeDocument(path, text);
    if (document == nullptr || !analysisReady(*document) ||
        document->graph == nullptr) {
        return result;
    }
    // Every unit, walked directly at the top level: no sema is needed - names
    // compare textually to the item's name.
    for (CompilationUnit* unit : document->graph->units()) {
        if (unit == nullptr || unit->isPrelude) continue;
        if (unit->ast == nullptr || unit->file == nullptr) continue;
        if (unit->file->text.find(item.name) == std::string::npos) continue;
        forEachTopLevelStringNameDeclaration(*unit->ast, [&](const Node& decl) {
            std::vector<std::string> parents;
            collectParentNames(decl, parents);
            if (std::find(parents.begin(), parents.end(), item.name) ==
                parents.end()) {
                return;
            }
            if (isSelfDeclaration(decl, item)) return;
            const std::string name = hierarchyDeclarationName(decl);
            std::optional<lsp::HierarchyEntry> built =
                hierarchyEntryFor(decl, name);
            if (built.has_value()) result.push_back(std::move(*built));
        });
    }
    dedupEntries(result);
    return result;
}

}  // namespace

std::vector<lsp::HierarchyEntry> typeHierarchyPrepareAt(
    const std::string& path, const std::string& text, std::uint32_t line,
    std::uint32_t column) {
    std::vector<lsp::HierarchyEntry> result;
    try {
        auto document = analyzeDocument(path, text);
        if (document == nullptr || !analysisReady(*document)) return result;
        CompilationUnit* entry = document->entry;
        if (entry == nullptr || entry->ast == nullptr) return result;
        const SourceFile& file = *document->file;
        const Program& program = *entry->ast;
        const Node* node = innermostNodeAt(program, file, line, column);
        if (node == nullptr) return result;
        const Node* decl = nullptr;
        // (i) the cursor node is the declaration statement itself
        // (a symbol published for the declaration would agree; the statement
        // being the smallest node covering the position is proof enough).
        if (isStringNameDeclaration(node->kind)) {
            decl = node;
        }
        // (ii) a use bound to a hierarchy declaration (e.g. `new Box()`):
        // the compiler's own binding answer decides, and the inferred type
        // of a class-valued use names the class when the binding table has
        // no entry for it.
        if (decl == nullptr && node->kind == NodeKind::Identifier) {
            decl = declarationForUse(*document, program, *node);
        }
        // (iii) a type reference naming a declaration (`extends Base`,
        // `x: Sized`, `Pair<A, B>`): type nodes hold no identifier, so the
        // head name is resolved against the declarations themselves.
        if (decl == nullptr && isNamedTypeNode(node->kind)) {
            decl = declarationNamed(
                *document, typeHeadName(static_cast<const TypeNode&>(*node)));
        }
        // (iv) an identifier or member expression sitting inside a hierarchy
        // declaration prepares the enclosing declaration.
        if (decl == nullptr && (node->kind == NodeKind::Identifier ||
                                 node->kind == NodeKind::MemberExpr)) {
            decl = enclosingHierarchyDeclaration(program, file, line, column);
        }
        if (decl == nullptr) return result;
        const std::string name = hierarchyDeclarationName(*decl);
        if (name.empty()) return result;
        const std::optional<lsp::HierarchyEntry> built =
            hierarchyEntryFor(*decl, name);
        if (built.has_value()) result.push_back(*built);
        return result;
    } catch (...) {
        return std::vector<lsp::HierarchyEntry>{};
    }
}

std::vector<lsp::HierarchyEntry> typeHierarchyRelativesAt(
    const std::string& path, const std::string& text,
    const lsp::HierarchyEntry& entry, bool supertypes) {
    try {
        if (supertypes) return supertypesOf(path, text, entry);
        return subtypesOf(path, text, entry);
    } catch (...) {
        return std::vector<lsp::HierarchyEntry>{};
    }
}

}  // namespace cp3::cli
