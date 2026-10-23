// =============================================================================
// src/sema/src/detail/docs.h - PRIVATE doc-comment reader.
//
// Declares the doc-comment surface of the sema module: the reader that finds
// the comment documenting a declaration, and the parser that turns its text
// into a `DocComment` (cp3/analysis.hpp).
//
// Everything here is implementation detail: this header lives under
// `src/sema/src/detail/` and is never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/source.hpp"

namespace cp3 {

// The doc comment that documents the declaration starting at `offset` in
// `file`, or an empty comment when there is none.
//
// Two spellings are recognised, both JSDoc-style:
//   * a block comment `/** ... */` that ends on an earlier line than the
//     declaration, with no blank line in between;
//   * a run of `///` lines directly above the declaration.
//
// A doc comment never changes what a program does: this reader only reads.
DocComment readDocComment(const SourceFile& file, std::uint32_t offset);

// The parsed form of a doc comment's text: the free-text description plus the
// `@param <name> <text>` and `@returns` / `@return <text>` tags. Any other
// `@tag` is kept as plain text instead of failing.
DocComment parseDocComment(const std::vector<std::string>& lines);

}  // namespace cp3
