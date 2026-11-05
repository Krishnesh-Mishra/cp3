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

// The first occurrence of `name` inside `span`, as a span of its own; the whole
// span when the name cannot be found (a safe, if imprecise, fallback).
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

// Definition (lsp_intel_define.cpp): same-file declarations and local-module
// imports; std imports answer no location instead of a wrong one.
std::vector<lsp::SourceTarget> definitionAt(const std::string& path,
                                            const std::string& text,
                                            std::uint32_t line,
                                            std::uint32_t column);

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
