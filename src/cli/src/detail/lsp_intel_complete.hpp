// =============================================================================
// src/cli/src/detail/lsp_intel_complete.hpp - completion internals
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// completeAt (detail/lsp_intel.hpp) detects the cursor context in
// lsp_intel_complete.cpp; the three answer builders live in
// lsp_intel_members.cpp and lsp_intel_names.cpp. This header exists so those
// units agree on the context record and nothing else crosses the boundary.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/lsp.hpp"
#include "detail/lsp_intel.hpp"

namespace cp3::cli {

// What the raw text and the token stream say about the cursor.
struct CompletionContext {
    bool member = false;           // `receiver.|`
    bool importSpecifier = false;  // inside an `import ... from "|"` string
    bool blocked = false;          // inside a comment / non-import string
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
std::vector<lsp::CompletionCandidate> importSpecifierCandidates(
    const std::string& text, const CompletionContext& context,
    std::uint32_t offset);

// Code candidates: names declared in the document, language keywords and a
// few snippets.
std::vector<lsp::CompletionCandidate> generalCandidates(
    const AnalyzedDocument& document);

}  // namespace cp3::cli
