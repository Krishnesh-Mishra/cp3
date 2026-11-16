// =============================================================================
// src/cli/src/detail/lsp_intel.hpp - the semantic engine behind IntelHooks
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions are split by concern:
//   * commands/lsp_intel.cpp              - analysis + position/span helpers.
//   * commands/lsp_intel_walk*.cpp        - the AST walk (forEachNode).
//   * commands/lsp_intel_query.cpp        - hover, symbols, signature.
//   * commands/lsp_intel_define.cpp       - go-to-definition.
//   * commands/lsp/intel/lsp_intel_references.cpp - references & rename.
//   * commands/lsp/intel/lsp_intel_hierarchy.cpp  - type hierarchy.
//   * commands/lsp/intel/lsp_intel_members.cpp    - completion; member/
//     property/enum-member references & rename fallback.
//   * commands/lsp_intel_complete*.cpp    - completion + the probe analysis.
//
// The engine answers only from the frozen frontend's own results: the AST the
// parser built and the SemaResult side tables (nodeTypes / symbols /
// functions). A position that the compiler cannot resolve answers empty; the
// server turns that into an empty LSP result, never an error.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/host/options.hpp"
#include "cp3/lsp.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

namespace cp3::cli {

// One document run through the `cp3c check` stages. `file` is the entry source
// (the document text); `entry` is its compilation unit inside `graph`. All the
// spans the engine reports belong to files owned by `sources`.
struct AnalyzedDocument {
    SourceManager sources;
    SourceFile* file = nullptr;
    std::unique_ptr<ModuleGraph> graph;
    CompilationUnit* entry = nullptr;
    SemaResult sema;
    // The sema of every analyzed unit of the import closure, keyed by unit id
    // (each parser restarts nodeIds, so the tables cannot be merged). The
    // entry's own result lands in `sema`; this table holds the rest, produced
    // by the same `analyzeProgramGraph` run so cross-module queries never
    // reanalyze the graph. Empty when the analysis did not run.
    std::unordered_map<std::uint32_t, SemaResult> unitSemas;
    std::vector<Diagnostic> diagnostics;
    // The project vocabulary the analysis used; std documentation spells
    // native integer results (`u16`, `usize`, ...) in this vocabulary.
    TypeSystem typeSystem = TypeSystem::TypeScript;
};

// Loads `text` as the file at `path` (preludes, local imports, sema). Never
// throws for malformed source: the parser recovers and sema fills what it can.
std::unique_ptr<AnalyzedDocument> analyzeDocument(const std::string& path,
                                                  const std::string& text);

using NodeVisitor = std::function<void(const Node&)>;

// Pre-order walk over every node in `program`, covering all node kinds.
void forEachNode(const Program& program, const NodeVisitor& visit);

// The 0-based byte offset of a compiler position inside `file`, clamped into
// the document.
std::uint32_t offsetOf(const SourceFile& file, std::uint32_t line,
                       std::uint32_t column);

// True when the half-open span belongs to `file` and covers the position.
bool spanContains(const SourceSpan& span, const SourceFile& file,
                  std::uint32_t line, std::uint32_t column);

// The innermost (smallest-span) node covering the position, or nullptr.
const Node* innermostNodeAt(const Program& program, const SourceFile& file,
                            std::uint32_t line, std::uint32_t column);

// True when an analysis produced a program to answer from.
bool analysisReady(const AnalyzedDocument& document);

// True for the placeholder types the compiler uses for "some ambient value"
// (the shape behind an unresolved import). Their printable label is an
// internal name ("$ambient"), so callers must not surface it.
bool isPlaceholderType(const Type& type);

// The nearest declaration of `name` at or before `offset` in `file`, or
// nullptr. The binding table carries every declaration site; the greatest
// offset not past the cursor is the binding that shadows the earlier ones.
const SymbolInfo* nearestSymbolAt(const SemaResult& sema, const std::string& name,
                                  const SourceFile& file, std::uint32_t offset);

// The first occurrence of `name` inside `span`, as a whole word (bounded by
// non-identifier bytes), as a span of its own; the whole span when the name
// cannot be found (a safe, if imprecise, fallback).
SourceSpan nameSpanIn(const SourceSpan& span, const std::string& name);

// The name of a member key (`m() {}`, `x: number`), or empty.
std::string keyName(const Expr* key);

// Completion (lsp_intel_complete.cpp): candidates for the position, validated
// against the compiler by a probe analysis where the context demands it.
std::vector<lsp::CompletionCandidate> completeAt(const std::string& path,
                                                 const std::string& text,
                                                 std::uint32_t line,
                                                 std::uint32_t column);

// A precise parameter type when the cursor is inside a typed call argument.
// Null means that the call/parameter type cannot be resolved confidently.
const Type* expectedArgumentTypeAt(const AnalyzedDocument& document,
                                   std::uint32_t offset);

// Hover (lsp_intel_query.cpp): the inferred type at the cursor, or null when
// the compiler has nothing to say there.
std::optional<lsp::HoverAnswer> hoverAt(const std::string& path,
                                        const std::string& text,
                                        std::uint32_t line,
                                        std::uint32_t column);

// Definition (lsp_intel_define.cpp): same-file declarations, local-module
// imports, and member positions (`receiver.member`, a member's own key); std
// imports answer no location instead of a wrong one.
std::vector<lsp::SourceTarget> definitionAt(const std::string& path,
                                            const std::string& text,
                                            std::uint32_t line,
                                            std::uint32_t column);

// The declaration an export names, with the name it is exported under.
struct ExportHit {
    const Node* node = nullptr;
    std::string name;
};

// The exported declaration a local import binds (lsp_intel_define.cpp).
ExportHit findExported(const Program& program, const std::string& name,
                       bool wantDefault);

// The import declaration that binds `name` in this unit's file, or nullptr.
const ImportDeclaration* importForName(const Program& program,
                                       const std::string& name,
                                       const SourceFile& file);

// The dependency edge of `specifier` imported by `importer`, or nullptr.
const ModuleDependency* dependencyFor(const ModuleGraph& graph,
                                      std::uint32_t importer,
                                      const std::string& specifier);

// The terminal declaration for `name` as `unit` exports it, following
// re-export chains across Local module edges (`export { x as y } from "./m"`,
// `export * from "./m"`, `export { local as y }`, `export default` chains).
// std/package/native/unknown hops refuse (nullopt). `name` is ignored when
// `wantDefault` is set. On success `name` carries the declaration's own name
// in `unit` (the hops reduce the importing unit's spelling to it).
struct ExportedDeclaration {
    CompilationUnit* unit = nullptr;       // the unit that declares it
    const Node* declaration = nullptr;      // the declaration node in that unit
    std::string name;                      // the declaration's own name
};
std::optional<ExportedDeclaration> resolveExportChain(
    const ModuleGraph& graph, CompilationUnit* unit, const std::string& name,
    bool wantDefault);

// The units a nominal type may come from: the entry first, then the units the
// import graph analyzed for it (an unresolved local import cannot contribute).
// Compiler-owned prelude units are excluded. A receiver whose declaration
// lives in an imported module cannot be found by scanning the entry program
// alone.
std::vector<const CompilationUnit*> reachableUnitsFor(
    const AnalyzedDocument& document);

// References (lsp_intel_references.cpp): every occurrence of the binding the
// cursor resolves to, including the declaration itself when asked. Std /
// package / native imports answer occurrences bound to the import in this
// unit; member/property positions answer through the member fallback.
std::vector<lsp::SourceTarget> referencesAt(const std::string& path,
                                            const std::string& text,
                                            std::uint32_t line,
                                            std::uint32_t column,
                                            bool includeDeclaration);

// Prepare-rename (lsp_intel_references.cpp): the binding's own name span (or
// the member name span at the cursor on a member position), or null when the
// cursor does not sit on a renamable binding/member.
std::optional<lsp::SourceTarget> prepareRenameAt(const std::string& path,
                                                 const std::string& text,
                                                 std::uint32_t line,
                                                 std::uint32_t column);

// Rename (lsp_intel_references.cpp): straight-line replacements over every
// occurrence referencesAt finds, including the declaration. Std / package /
// native imports refuse (an empty vector); rename never rewrites a binding it
// cannot prove. Member positions rewrite through the member fallback (or
// refuse).
std::vector<lsp::SourceTarget> renameAt(const std::string& path,
                                        const std::string& text,
                                        std::uint32_t line,
                                        std::uint32_t column,
                                        const std::string& newName);

// Member/property/enum-member fallback (lsp_intel_members.cpp). When the
// binding pipeline cannot prove a target at the cursor (a class/interface
// member key, an enum member, an object-literal property, or the member side
// of `receiver.member`), these answer references/prepareRename/rename with a
// member-aware account instead of nothing. Nothing here ever throws: an empty
// vector / nullopt is a refusal, never a guess. `memberRetarget` resolves the
// cursor to the member; `memberOccurrences` collects its occurrences,
// honouring only the relationships the compiler can prove (the owner nominal
// type, same-file "interface IMPLEMENTATIONS" and same-file extends chains,
// the synthesized shape label of object literals/interfaces, and direct
// import edges to the owner unit) and nothing else.
enum class MemberOccurrenceMode { References, Rename };

struct MemberRetarget {
    std::string name;            // the member to rewrite
    // The member name span the cursor sits on - the prepareRename range.
    lsp::SourceTarget declaration;
    // Internal recipe (opaque outside lsp_intel_members.cpp).
    CompilationUnit* ownerUnit = nullptr;
    std::uint32_t ownerNodeId = 0;   // owner declaration node in its unit's sema
    NodeKind ownerKind = NodeKind::Program;
    std::string ownerLabel;          // Object/literal owner: the shape label
};

std::optional<MemberRetarget> memberRetarget(AnalyzedDocument& document,
                                             const std::string& path,
                                             std::uint32_t line,
                                             std::uint32_t column);

// Every occurrence of the retargeted member, deduplicated by span.
// `includeDeclaration` drops the declaration occurrences when false. `mode`
// selects the references family (shadowed same-name declarations are excluded
// from the occurrence set, uses are matched by the receiver chain receiver
// rule) instead of the rename family (every same-name declaration in the
// same-file extends/implementation chains rewrites so a drop-in rename stays
// consistent). An empty result means "refused", never an incomplete rename.
std::vector<lsp::SourceTarget> memberOccurrences(
    AnalyzedDocument& document, const std::string& path,
    const MemberRetarget& target, bool includeDeclaration,
    MemberOccurrenceMode mode);

// Prepare/relate a type hierarchy. Same shapes as makeIntelHooks expects.
std::vector<lsp::HierarchyEntry> typeHierarchyPrepareAt(const std::string& path,
                                                        const std::string& text,
                                                        std::uint32_t line,
                                                        std::uint32_t column);
std::vector<lsp::HierarchyEntry> typeHierarchyRelativesAt(const std::string& path,
                                                          const std::string& text,
                                                          const lsp::HierarchyEntry& entry,
                                                          bool supertypes);

// Call hierarchy (lsp_intel_calls.cpp): the callable the cursor names, the
// callers of that callable, and the callees inside its body. The entries use
// the hierarchy item shape: the name, the kind, the detail and the spans of
// the declaration the item points at.
std::vector<lsp::HierarchyEntry> callHierarchyPrepareAt(const std::string& path,
                                                         const std::string& text,
                                                         std::uint32_t line,
                                                         std::uint32_t column);
std::vector<lsp::HierarchyEntry> callHierarchyIncomingAt(
    const std::string& path, const std::string& text,
    const lsp::HierarchyEntry& entry);
std::vector<lsp::HierarchyEntry> callHierarchyOutgoingAt(
    const std::string& path, const std::string& text,
    const lsp::HierarchyEntry& entry);

// Document symbols (lsp_intel_query.cpp): the outline of one document.
std::vector<lsp::SymbolEntry> symbolsAt(const std::string& path,
                                        const std::string& text);

// Signature help (lsp_intel_query.cpp): the active call's parameter list.
std::optional<lsp::SignatureAnswer> signatureAt(const std::string& path,
                                                const std::string& text,
                                                std::uint32_t line,
                                                std::uint32_t column);

// Formatting (lsp_intel.cpp): `formatDocumentText` behind the same lexical
// guard `cp3c fmt` applies; null means "refused, keep the buffer".
std::optional<std::string> formatAt(const std::string& path,
                                    const std::string& text);

// On-disk text for a definition target the server has not opened.
std::optional<std::string> readFileAt(const std::string& path);

// The hook set `cp3c lsp` installs. Every hook catches its own failures and
// answers empty, so a broken document can never take the server down.
lsp::IntelHooks makeIntelHooks();

}  // namespace cp3::cli
