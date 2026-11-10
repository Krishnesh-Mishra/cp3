// =============================================================================
// src/cli/src/detail/lsp_intel_complete.hpp - completion internals
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// completeAt (detail/lsp_intel.hpp) detects the cursor context in
// lsp_intel_complete.cpp; the answer builders live in lsp_intel_members.cpp,
// lsp_intel_names.cpp and lsp_intel_scope.cpp. This header exists so those
// units agree on the context record and nothing else crosses the boundary.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/lsp.hpp"
#include "cp3/core/token.hpp"
#include "detail/lsp/lsp_intel.hpp"

namespace cp3::cli {

// Where the cursor sits, syntactically.
//
// The completion popup answers "what can I write here?", so the position
// decides two things: which keywords may legally appear, and which names are
// in scope. A keyword that cannot appear at the cursor is never offered.
enum class CompletionPosition {
    Unknown,         // the cursor is nowhere the parser could place it
    StatementStart,  // a statement may begin here
    expressionstart, // an expression may begin here
    TypePosition,    // a type name is expected
    MemberPosition,  // after a `receiver.`
    ImportClause,    // inside an import clause
    ClassBody,       // directly inside a class body
    ParameterList,   // inside a declaration's parameter list
    TryBlock,        // inside a `try` block (or its catch clause)
    SwitchBlock,     // inside a `switch` body
    DeclarationName, // a declaration's name is expected (`let `, `function `)
};

// What the raw text and the token stream say about the cursor.
struct CompletionContext {
    bool member = false;           // `receiver.|`
    bool importSpecifier = false;  // inside an `import ... from "|"` string
    bool stringLiteral = false;    // inside a normal string argument
    bool blocked = false;          // inside a comment / non-import string
    bool awaitAllowed = false;     // top level or the nearest function is async
    CompletionPosition position = CompletionPosition::Unknown;
    const Type* expectedType = nullptr;  // known active call-argument type
    std::uint32_t wordBegin = 0;   // start of the partial word
    std::uint32_t dot = 0;         // member: offset of the '.'
    std::uint32_t receiverBegin = 0;
    std::uint32_t stringBegin = 0;  // importSpecifier: offset of the open quote
};

// Last identifier-shaped word in `text` (the member builder needs it too).
std::string lastIdentifier(const std::string& text);

// Member-access candidates: the documented receiver surfaces plus same-file
// shapes, each validated against the compiler by one probe analysis.
std::vector<lsp::CompletionCandidate> memberCandidates(
    const AnalyzedDocument& document, const std::string& text,
    const CompletionContext& context, std::uint32_t offset);

// Import-specifier candidates: the known `std/...` module names, nothing for
// a relative path (no disk scanning).
// Import-named-list candidates: the named exports of the specifier the
// enclosing import statement names, for a cursor inside
// `import { | } from "..."`. Empty when the module is not a known std module.
std::vector<lsp::CompletionCandidate> importNamedCandidates(
    const std::string& text, std::uint32_t offset);

std::vector<lsp::CompletionCandidate> importSpecifierCandidates(
    const std::string& text, const CompletionContext& context,
    std::uint32_t offset);

// Finite values accepted by the active string-literal argument. Returns empty
// for ordinary open-ended strings and strings outside a call argument.
std::vector<lsp::CompletionCandidate> stringArgumentCandidates(
    const AnalyzedDocument& document, std::uint32_t offset);

// The syntactic position of the cursor, from the token stream (what may
// follow the cursor) and the AST (what encloses it).
CompletionPosition classifyPosition(const AnalyzedDocument& document,
                                    const std::vector<Token>& tokens,
                                    const std::string& text,
                                    std::uint32_t offset);

// Whether CP3 permits `await` at this cursor (top-level or async function).
bool awaitAllowedAt(const AnalyzedDocument& document, std::uint32_t offset);

// Whether a context-sensitive control keyword can be inserted here. This
// keeps return/break/continue/else/catch/finally/case suggestions out of
// positions where the parser would reject them.
bool contextualKeywordAllowed(const AnalyzedDocument& document,
                              std::uint32_t offset,
                              const std::string& keyword);

// The names in scope at the cursor, most local first: the parameters of the
// enclosing function, the locals of the enclosing blocks and the bindings of
// the enclosing loop or catch clause. In a type position only the names that
// name a type are returned.
std::vector<lsp::CompletionCandidate> scopeCandidates(
    const AnalyzedDocument& document, CompletionPosition position,
    std::uint32_t offset);

// The keywords that may legally appear at `position`, in popup order.
std::vector<std::string> legalKeywords(CompletionPosition position,
                                      bool awaitAllowed);

// Grammar-only candidates used while a document is incomplete or cannot yet
// be semantically analyzed. These keep keyword/type completion alive during
// normal incremental editing; they intentionally make no claims about names
// whose types depend on semantic analysis.
std::vector<lsp::CompletionCandidate> incompleteCandidates(
    CompletionPosition position, bool awaitAllowed);

// Code candidates: the names in scope, the names the module declares, the
// legal keywords and a few snippets.
std::vector<lsp::CompletionCandidate> generalCandidates(
    const AnalyzedDocument& document, const CompletionContext& context,
    std::uint32_t offset);

}  // namespace cp3::cli
