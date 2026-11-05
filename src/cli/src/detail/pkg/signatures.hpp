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
#include "cp3/host/options.hpp"

namespace cp3::cli {

// True when the module has an `export default ...` at top level. Mandatory for
// `"type": "package"` projects (CLI-README §5.2): a package build refuses to
// produce a `.cp3pak` without it.
bool hasDefaultExport(const Program& program);

// True when the module carries any usable package export surface: a default
// export, an `export <declaration>` (e.g. `export class Counter`), or a
// non-empty `export { ... }` list (including re-exports). A package entry may
// export classes through named exports without an object-literal default
// export; the pack/install validation accepts either form.
bool hasPackageExportSurface(const Program& program);

// Rewrites `file`'s module down to declarations: function signatures keep
// their parameters and return type with the body dropped, class method and
// constructor bodies are elided, and top-level statements that are not
// declarations are omitted. Imports, exports, variables and type declarations
// are preserved verbatim. Native type names are rewritten to prefixed
// spellings (see signatures.cpp), so the exported surface resolves
// identically in every consumer project, regardless of its `typeSystem`.
std::string stripToSignatures(const SourceFile& file, const Program& program,
                              TypeSystem typeSystem);

}  // namespace cp3::cli
