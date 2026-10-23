// =============================================================================
// src/cli/src/detail/signatures.hpp - `index.cp3` generation DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/signatures.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines).
//
// `index.cp3` is the signatures-and-types-only view of a package entry that
// ships inside every `.cp3pak` (CLI-README §5.3); it is what an editor/LSP
// reads for autocomplete. The CLI derives it from the parsed module - it is
// never hand-written or hand-maintained.
// =============================================================================
#pragma once

#include <string>

#include "cp3/core/ast.hpp"
#include "cp3/core/source.hpp"

namespace cp3::cli {

// True when the module has an `export default ...` at top level. Mandatory for
// `"type": "package"` projects (CLI-README §5.2): a package build refuses to
// produce a `.cp3pak` without it.
bool hasDefaultExport(const Program& program);

// Rewrites `file`'s module down to declarations: function signatures keep
// their parameters and return type with the body dropped, class method and
// constructor bodies are elided, and top-level statements that are not
// declarations are omitted. Imports, exports, variables and type declarations
// are preserved verbatim.
std::string stripToSignatures(const SourceFile& file, const Program& program);

}  // namespace cp3::cli
