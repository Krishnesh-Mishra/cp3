// =============================================================================
// src/cli/src/detail/lsp_intel_docs.hpp - the documentation the editor shows.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// The semantic engine answers three questions about documentation:
//   * what does the symbol under the cursor do (its doc comment, or the
//     std surface's own description)?
//   * what does a completion candidate do?
//   * what do the parameters of the active call mean?
//
// The answers come from two places the compiler already has: the doc comments
// sema parsed into `SemaResult::docs` (cp3/analysis.hpp) and the std member
// tables (cp3/std_surface.hpp). Both are rendered as Markdown, so the editor
// formats the description and the parameter list.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/std_surface.hpp"
#include "cp3/types.hpp"

#include "detail/lsp_intel.hpp"

namespace cp3::cli {

// The documentation answer for one symbol.
struct DocAnswer {
    std::string signature;  // the documented signature ("" when there is none)
    std::string body;       // the Markdown description
    // One entry per parameter of the documented signature ("" when the
    // parameter is not documented).
    std::vector<std::string> parameterDocs;

    bool empty() const noexcept {
        return signature.empty() && body.empty();
    }
};

// The Markdown rendering of a parsed doc comment: the description, then the
// `@param` list, then the `@returns` text.
std::string renderDocComment(const DocComment& doc);

// The Markdown rendering of a std member's documentation: the documented
// signature, then the description.
std::string renderStdDoc(const std::string& signature, const std::string& body);

// The documentation of the symbol `node` names: the std surface first (a std
// member knows its own documented signature), then the doc comment of the
// declaration the name refers to.
DocAnswer documentationAt(const AnalyzedDocument& document, const Node& node,
                           const std::string& name, const SymbolInfo* symbol);

// The documentation of the member `member` of the std surface the receiver
// denotes - a namespace (`console.log`), a std value (`Date.now`, `Map.get`)
// or the array / string method surfaces.
DocAnswer stdMemberDocAt(const AnalyzedDocument& document,
                          const MemberExpr& member);

// The doc comment of the declaration `name` refers to at `offset`, or nullptr.
const DocComment* docCommentAt(const AnalyzedDocument& document,
                               const std::string& name, std::uint32_t offset);

// The doc comment of the member `member` of the class `className`, or
// nullptr.
const DocComment* classMemberDocAt(const AnalyzedDocument& document,
                                   const std::string& className,
                                   const std::string& member);

// The rendered doc comment of the declaration `nodeId`, or an empty string.
std::string docCommentFor(const AnalyzedDocument& document,
                           std::uint32_t nodeId);

// The documentation of a completion candidate: the std namespace's own
// documentation (`namespaceName` empty when the receiver is not a std
// namespace), the array / string method documentation, or an empty string
// when the surface has none.
std::string completionMemberDoc(const std::string& namespaceName,
                                const Type* receiverType,
                                const std::string& member);

// The parameter names of the function declaration `nodeId`, in order.
std::vector<std::string> parameterNamesOf(const AnalyzedDocument& document,
                                          std::uint32_t nodeId);

// The per-parameter documentation of a call: the `@param` entries of the
// declaration's doc comment, matched by parameter name where the names are
// known and by position otherwise. One entry per parameter.
std::vector<std::string> parameterDocs(const AnalyzedDocument& document,
                                       const SymbolInfo* symbol,
                                       std::size_t count);

// The type of the expression that ends at `offset` (the receiver of a member
// access), or nullptr when there is none.
const Type* receiverTypeEndingAt(const AnalyzedDocument& document,
                                 std::uint32_t offset);

}  // namespace cp3::cli
