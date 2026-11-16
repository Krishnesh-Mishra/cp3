// =============================================================================
// src/sema/src/checks/unused_import_check.cpp - unused imports (CP3307).
//
// A module-level import binding that is never referenced anywhere in the
// module body is diagnosed. The imports that come from expressions are
// counted as used by the analysis pass itself (via `usedNames_`), and
// re-exports and `export default` an imported name both reference the binding.
// =============================================================================
#include "detail/analyzer.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

void Analyzer::checkUnusedImports(Program& program) {
    if (imports_ == nullptr) return;
    // Re-export specifiers (`export { join }` / `export { join as j }`) and
    // `export default <ident>` both reference an import binding without going
    // through any expression inference.
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr || stmtPtr->kind != NodeKind::ExportDeclaration) continue;
        const auto& ed =
            static_cast<const ExportDeclaration&>(*stmtPtr);
        for (const auto& specPtr : ed.specifiers) {
            const auto& spec = static_cast<const ExportSpecifier&>(*specPtr);
            if (!spec.local.empty()) usedNames_.insert(spec.local);
        }
        if (ed.defaultExpr && ed.defaultExpr->kind == NodeKind::Identifier) {
            usedNames_.insert(
                static_cast<const Identifier&>(*ed.defaultExpr).name);
        }
    }

    const auto checkOne = [&](const std::string& local, const SourceSpan& span) {
        if (local.empty()) return;
        if (usedNames_.count(local) != 0) return;
        report(diagcode::kUnusedImport, span,
               "the import '" + local + "' is never used");
    };

    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr || stmtPtr->kind != NodeKind::ImportDeclaration) continue;
        const auto& imp = static_cast<const ImportDeclaration&>(*stmtPtr);
        checkOne(imp.defaultBinding, imp.span);
        checkOne(imp.namespaceBinding, imp.span);
        checkOne(imp.alias, imp.span);
        for (const auto& specPtr : imp.specifiers) {
            const auto& spec = static_cast<const ImportSpecifier&>(*specPtr);
            checkOne(spec.local, spec.span);
        }
    }
}

}  // namespace cp3
