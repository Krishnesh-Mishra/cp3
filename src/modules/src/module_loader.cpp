// =============================================================================
// src/modules/src/module_loader.cpp - ProgramLoader orchestration + public boundary.
//
// OWNERSHIP: modules module (src/modules/**). Defines cp3::ModuleLoader
// (cp3/modules.hpp) and the ProgramLoader state machine (detail/loader.h):
// unit registry, dedupe by canonical path, and diagnostic dedupe for the
// same source location (a prelude injected into N units repeats its imports,
// but must report each problem once).
// =============================================================================
#include "detail/loader.h"

#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/modules.hpp"

#include "detail/file_paths.h"
#include "detail/prelude_scan.h"

namespace cp3 {

std::unique_ptr<ModuleGraph> ModuleLoader::load(SourceManager& sm,
                                                const SourceFile& entry) {
    modules::ProgramLoader loader(sink_, packageResolver_);
    return loader.load(sm, entry);
}

}  // namespace cp3

namespace cp3::modules {

std::unique_ptr<ModuleGraph> ProgramLoader::load(SourceManager& sm,
                                                 const SourceFile& entry) {
    sm_ = &sm;
    auto graph = std::make_unique<ModuleGraph>();
    graph_ = graph.get();

    // The native binding root: `<project>/.cp3/native` (the project root is
    // the nearest ancestor with a package.json, as everywhere else).
    projectRoot_ = projectRootFor(fs::path(entry.path));
    nativeRoot_ = projectRoot_ / ".cp3" / "native";

    CompilationUnit* entryUnit = addUnitFor(entry, true);
    const std::string key = canonicalKey(fs::path(entry.path));
    byKey_.emplace(key, entryUnit);
    info_.emplace(entryUnit->id, UnitInfo{key, folderOf(fs::path(entry.path))});
    graph_->setEntry(entryUnit);

    parseUnit(*entryUnit, !entryUnit->isPrelude);
    processReferences(*entryUnit);

    graph_ = nullptr;
    sm_ = nullptr;
    return graph;
}

CompilationUnit* ProgramLoader::addUnitFor(const SourceFile& file, bool isEntry) {
    CompilationUnit* unit = graph_->addUnit(&file);
    unit->isEntry = isEntry;
    const fs::path path(file.path);
    unit->moduleName = moduleNameFor(path);
    PreludeKind kind = PreludeKind::None;
    if (isPreludeFileName(path.filename().string(), kind)) {
        unit->isPrelude = true;
        unit->preludeKind = kind;
    }
    return unit;
}

CompilationUnit* ProgramLoader::ensureUnit(const fs::path& path) {
    const std::string key = canonicalKey(path);
    const auto it = byKey_.find(key);
    if (it != byKey_.end()) return it->second;

    std::string text;
    if (!readWholeFile(path, text)) return nullptr;
    const fs::path absolute = fs::absolute(path);
    SourceFile* file = sm_->addFile(absolute.generic_string(), std::move(text));
    if (file == nullptr) return nullptr;

    CompilationUnit* unit = addUnitFor(*file, false);
    byKey_.emplace(key, unit);
    info_.emplace(unit->id, UnitInfo{key, folderOf(absolute)});

    // Prelude files are injected as raw tokens and never parsed standalone
    // (unless one is the entry, which load() handles); everything else is
    // parsed once with its applicable preludes injected.
    if (!unit->isPrelude) {
        parseUnit(*unit, true);
        processReferences(*unit);
    }
    return unit;
}

void ProgramLoader::reportOnce(const std::string& code, const SourceSpan& span,
                               const std::string& message,
                               const std::vector<std::string>& suggestions) {
    std::string key = code;
    key += '|';
    key += span.file != nullptr ? span.file->canonicalPath : std::string();
    key += '|';
    key += std::to_string(span.begin.offset);
    key += '|';
    key += message;
    if (!reported_.insert(std::move(key)).second) return;
    Diagnostic d(code, Severity::Error, span, message);
    d.suggestions = suggestions;
    sink_.report(std::move(d));
}

fs::path ProgramLoader::folderOfSource(const SourceFile& file) {
    const auto it = folderCache_.find(&file);
    if (it != folderCache_.end()) return it->second;
    const fs::path dir = folderOf(fs::path(file.path));
    folderCache_.emplace(&file, dir);
    return dir;
}

}  // namespace cp3::modules
