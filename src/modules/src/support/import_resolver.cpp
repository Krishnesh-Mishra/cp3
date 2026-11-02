// =============================================================================
// src/modules/src/support/import_resolver.cpp - import classification, resolution, edges.
//
// OWNERSHIP: modules module (src/modules/**). Implements the import_resolver
// concern declared in detail/loader.h.
//
// Classification follows DECISIONS D4 (+D27): `./x`, `../x` or anything ending
// in `.cp3` is local; `std/...` is a std module; `cpp/...` is C++ interop;
// `import native` is native; any other bare specifier is a package. M1
// validates local and std imports; package/native/cpp imports only get an
// edge (their surfaces are deferred). Import statements injected from a
// prelude resolve relative to the prelude's own folder.
// =============================================================================
#include "detail/loader.h"

#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic_codes.hpp"

#include "detail/file_paths.h"
#include "detail/specifier.h"
#include "detail/std_registry.h"

namespace cp3::modules {

void ProgramLoader::processReferences(CompilationUnit& unit) {
    if (!unit.ast) return;
    for (const StmtPtr& stmt : unit.ast->body) {
        if (!stmt) continue;
        if (stmt->kind == NodeKind::ImportDeclaration) {
            processImport(unit, static_cast<const ImportDeclaration&>(*stmt));
        } else if (stmt->kind == NodeKind::ExportDeclaration) {
            processReExport(unit, static_cast<const ExportDeclaration&>(*stmt));
        }
    }
}

void ProgramLoader::processImport(CompilationUnit& unit, const ImportDeclaration& decl) {
    if (decl.source.empty()) return;

    ModuleDependency dep;
    dep.importer = unit.id;
    dep.imported = UINT32_MAX;
    dep.specifier = decl.source;
    dep.span = decl.span;
    dep.resolution = decl.kind == ImportKind::Native ? ModuleResolution::Native
                                                     : classifyModuleSpecifier(decl.source);

    if (dep.resolution == ModuleResolution::Native) {
        // The native import kind: the binding is loaded from
        // `<project>/.cp3/native/<id>` and parked in the graph, so sema and
        // codegen can type-check and lower its calls.
        dep.resolved = loadNativeBinding(decl);
        graph_->addDependency(std::move(dep));
        return;
    }

    const fs::path from = folderOfSource(decl.span.file ? *decl.span.file : *unit.file);
    if (dep.resolution == ModuleResolution::Local) {
        CompilationUnit* target = loadLocal(decl.source, from, decl.span);
        if (target != nullptr) {
            dep.imported = target->id;
            dep.resolved = true;
            validateImport(decl, surfaceOf(*target));
        }
    } else if (dep.resolution == ModuleResolution::Std) {
        const ExportSurface* surface = stdSurface(decl.source, decl.span);
        if (surface != nullptr) {
            dep.resolved = true;
            validateImport(decl, *surface);
        }
    } else if (dep.resolution == ModuleResolution::Package) {
        CompilationUnit* target = loadPackage(decl.source, decl.span);
        if (target != nullptr) {
            dep.imported = target->id;
            dep.resolved = true;
            validateImport(decl, surfaceOf(*target));
        }
    }
    graph_->addDependency(std::move(dep));
}

void ProgramLoader::processReExport(CompilationUnit& unit, const ExportDeclaration& decl) {
    if (decl.source.empty()) return;

    ModuleDependency dep;
    dep.importer = unit.id;
    dep.imported = UINT32_MAX;
    dep.specifier = decl.source;
    dep.span = decl.span;
    dep.resolution = classifyModuleSpecifier(decl.source);

    const fs::path from = folderOfSource(decl.span.file ? *decl.span.file : *unit.file);
    if (dep.resolution == ModuleResolution::Local) {
        CompilationUnit* target = loadLocal(decl.source, from, decl.span);
        if (target != nullptr) {
            dep.imported = target->id;
            dep.resolved = true;
        }
    } else if (dep.resolution == ModuleResolution::Std) {
        if (stdSurface(decl.source, decl.span) != nullptr) dep.resolved = true;
    } else if (dep.resolution == ModuleResolution::Package) {
        CompilationUnit* target = loadPackage(decl.source, decl.span);
        if (target != nullptr) {
            dep.imported = target->id;
            dep.resolved = true;
        }
    }
    graph_->addDependency(std::move(dep));
}

CompilationUnit* ProgramLoader::loadPackage(const std::string& specifier,
                                            const SourceSpan& span) {
    if (!packageResolver_) {
        reportOnce(diagcode::kModuleNotFound, span,
                   "cannot resolve package '" + specifier +
                       "' (no package resolver configured)");
        return nullptr;
    }
    const std::optional<PackageEntry> entry =
        packageResolver_(specifier, projectRoot_);
    if (!entry) {
        reportOnce(diagcode::kModuleNotFound, span,
                   "cannot find package '" + specifier + "'");
        return nullptr;
    }
    CompilationUnit* unit = ensureUnit(entry->path);
    if (unit == nullptr) {
        reportOnce(diagcode::kModuleNotFound, span,
                   "cannot read package entry for '" + specifier + "'");
        return nullptr;
    }
    unit->isPackage = true;
    unit->packageRoot = entry->root;
    if (!entry->moduleName.empty()) unit->moduleName = entry->moduleName;
    return unit;
}

CompilationUnit* ProgramLoader::loadLocal(const std::string& specifier,
                                          const fs::path& fromFolder,
                                          const SourceSpan& span) {
    fs::path candidate = fromFolder / fs::path(specifier);
    std::error_code ec;
    if (!fs::is_regular_file(candidate, ec) && !endsWith(specifier, ".cp3")) {
        fs::path withExtension = candidate;
        withExtension += ".cp3";
        candidate = std::move(withExtension);
    }
    if (!fs::is_regular_file(candidate, ec)) {
        reportOnce(diagcode::kModuleNotFound, span,
                   "cannot find module '" + specifier + "'");
        return nullptr;
    }
    CompilationUnit* unit = ensureUnit(candidate);
    if (unit == nullptr) {
        reportOnce(diagcode::kModuleNotFound, span,
                   "cannot read module '" + specifier + "'");
    }
    return unit;
}

}  // namespace cp3::modules
