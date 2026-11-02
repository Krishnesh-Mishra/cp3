// =============================================================================
// src/modules/src/detail/loader.h - the ProgramLoader state machine (PRIVATE header).
//
// OWNERSHIP: modules module (src/modules/**). Never included cross-module.
//
// ProgramLoader implements cp3::ModuleLoader (cp3/modules.hpp). The class
// declaration lives here; definitions are split by concern across
//   module_loader.cpp   - orchestration, unit registry, diagnostics
//   unit_loader.cpp     - prelude token injection + one Parser pass per unit
//   import_resolver.cpp - D4 classification, local resolution, graph edges
//   export_surface.cpp  - export surfaces + import validation
// (STYLE.md §3: headers declare, `.cpp` defines; one concern per file).
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/modules.hpp"
#include "cp3/core/source.hpp"
#include "cp3/core/token.hpp"

#include "detail/export_surface.h"
#include "detail/prelude_scan.h"

namespace cp3::modules {

namespace fs = std::filesystem;

// Bookkeeping the frozen CompilationUnit does not carry.
struct UnitInfo {
    std::string key;  // canonicalKey() of the unit's file path
    fs::path dir;     // absolute folder of the unit's file
};

class ProgramLoader {
public:
    explicit ProgramLoader(DiagnosticSink& sink) noexcept : sink_(sink) {}
    ProgramLoader(DiagnosticSink& sink, cp3::PackageEntryResolver packageResolver)
        : sink_(sink), packageResolver_(std::move(packageResolver)) {}

    std::unique_ptr<ModuleGraph> load(SourceManager& sm, const SourceFile& entry);

private:
    // --- module_loader.cpp ---------------------------------------------------
    CompilationUnit* addUnitFor(const SourceFile& file, bool isEntry);
    CompilationUnit* ensureUnit(const fs::path& path);
    void reportOnce(const std::string& code, const SourceSpan& span,
                    const std::string& message,
                    const std::vector<std::string>& suggestions = {});
    fs::path folderOfSource(const SourceFile& file);

    // --- unit_loader.cpp -----------------------------------------------------
    void parseUnit(CompilationUnit& unit, bool withPreludes);
    std::vector<Token> unitTokens(CompilationUnit& unit, bool withPreludes);
    bool appendPrelude(CompilationUnit& unit, const PreludeRef& ref,
                       std::vector<Token>& assembled);
    const std::vector<Token>& preludeTokens(CompilationUnit& prelude);

    // --- import_resolver.cpp -------------------------------------------------
    void processReferences(CompilationUnit& unit);
    void processImport(CompilationUnit& unit, const ImportDeclaration& decl);
    void processReExport(CompilationUnit& unit, const ExportDeclaration& decl);
    CompilationUnit* loadLocal(const std::string& specifier, const fs::path& fromFolder,
                               const SourceSpan& span);
    CompilationUnit* loadPackage(const std::string& specifier,
                                 const SourceSpan& span);

    // --- native_import.cpp ----------------------------------------------------
    // Resolves `import native "id" as alias` to the binding's metadata and
    // parks the loaded model in the graph.
    bool loadNativeBinding(const ImportDeclaration& decl);
    void reportNativeFailure(const std::string& id, const fs::path& dir,
                              const NativeReadResult& result,
                              const SourceSpan& span);
    // A binding's recorded paths are absolute, or relative to the project root.
    void resolveNativePaths(NativeModule& module, const fs::path& projectRoot);

    // --- export_surface.cpp --------------------------------------------------
    const ExportSurface& surfaceOf(CompilationUnit& unit);
    ExportSurface computeSurface(CompilationUnit& unit);
    void applyExport(const ExportDeclaration& decl, CompilationUnit& unit,
                     ExportSurface& out);
    void applyReExportAll(const ExportDeclaration& decl, CompilationUnit& unit,
                          ExportSurface& out);
    const ExportSurface* stdSurface(const std::string& specifier, const SourceSpan& span);
    void validateImport(const ImportDeclaration& decl, const ExportSurface& surface);

    DiagnosticSink& sink_;
    cp3::PackageEntryResolver packageResolver_;
    SourceManager* sm_ = nullptr;
    ModuleGraph* graph_ = nullptr;
    // The project root and the native binding root (`<root>/.cp3/native`).
    fs::path projectRoot_;
    fs::path nativeRoot_;
    std::unordered_map<std::string, CompilationUnit*> byKey_;
    std::unordered_map<std::uint32_t, UnitInfo> info_;
    std::unordered_map<std::string, std::vector<Token>> preludeTokenCache_;
    std::unordered_map<std::uint32_t, ExportSurface> surfaces_;
    std::unordered_set<std::uint32_t> surfaceInProgress_;
    std::unordered_map<const SourceFile*, fs::path> folderCache_;
    std::unordered_set<std::string> reported_;
};

}  // namespace cp3::modules
