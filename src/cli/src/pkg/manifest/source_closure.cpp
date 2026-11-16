// =============================================================================
// src/cli/src/pkg/manifest/source_closure.cpp - the source-package closure DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/pkg/source_closure.hpp.
//
// The module loader already resolved every unit the entry reaches; this walks
// the `Local` dependency edges (the relative imports) transitively, reads each
// unit's file, and names it relative to the package root. std/prelude units
// are skipped (the loader never materializes a unit for a std module, and a
// prelude is injected into its importers, not loaded standalone), so only the
// authored `.cp3` sources of the package ship.
// =============================================================================
#include "detail/pkg/source_closure.hpp"

#include <algorithm>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include "cp3/meta/module.hpp"

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

// A unit's file path, normalized to an absolute, canonical form (the entry's
// path is whatever the CLI resolved; imported units are stored absolute).
// `ec` is set only when even `absolute` fails.
fs::path normalizedFilePath(const std::string& path, std::error_code& ec) {
    fs::path abs(path);
    if (abs.is_relative()) abs = fs::absolute(abs, ec);
    if (ec) return fs::path(path);
    const fs::path canonical = fs::weakly_canonical(abs, ec);
    return ec || canonical.empty() ? abs : canonical;
}

std::filesystem::path normalizedRoot(const std::filesystem::path& root) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(root, ec);
    return ec ? root : canonical;
}

// The member path ('/'-separated, relative to the package root) that `file`
// maps to. Any path that does not stay under `packageRoot` (parent traversal,
// an absolute path, a device boundary) is refused: the archive cannot carry
// it, so the package is built from an invalid tree and must be reported, not
// shipped. The diagnostic names the rule so a scripted build can pin it.
bool closureMemberName(const fs::path& file, const fs::path& packageRoot,
                       std::string& name) {
    std::error_code ec;
    const fs::path rel = fs::relative(file, packageRoot, ec);
    if (ec) return false;
    const std::string generic = rel.generic_string();
    if (generic.empty() || generic == "." || generic == ".." ||
        generic.rfind("../", 0) == 0 || rel.is_absolute()) {
        return false;
    }
    name = generic;
    return true;
}

// A map from importer id to the units its `Local` edges resolve to.
std::unordered_map<std::uint32_t, std::vector<const cp3::CompilationUnit*>>
relativeEdges(const cp3::ModuleGraph& graph) {
    std::unordered_map<std::uint32_t, std::vector<const cp3::CompilationUnit*>> out;
    for (const cp3::ModuleDependency& dep : graph.dependencies()) {
        if (dep.resolution != cp3::ModuleResolution::Local) continue;
        if (dep.imported == UINT32_MAX) continue;
        const cp3::CompilationUnit* target = graph.unit(dep.imported);
        if (target != nullptr) out[dep.importer].push_back(target);
    }
    return out;
}

}  // namespace

bool collectRelativeClosure(const cp3::ModuleGraph& graph,
                            const std::filesystem::path& packageRoot,
                            std::vector<TarEntry>& out, std::string& err) {
    out.clear();
    const fs::path root = normalizedRoot(packageRoot);
    const std::unordered_map<std::uint32_t, std::vector<const cp3::CompilationUnit*>>
        edges = relativeEdges(graph);

    // Depth-first walk from the entry, deduplicated by unit id and tolerant of
    // import cycles: a unit is emitted at most once.
    std::vector<const cp3::CompilationUnit*> work{graph.entry()};
    std::unordered_set<std::uint32_t> seen;
    std::unordered_set<std::string> shipped;
    while (!work.empty()) {
        const cp3::CompilationUnit* unit = work.back();
        work.pop_back();
        if (unit == nullptr) continue;
        if (!seen.insert(unit->id).second) continue;
        if (unit->isPrelude) continue;   // injected token stream, not a unit
        if (unit->file == nullptr || !unit->ast) continue;

        std::error_code pathEc;
        const fs::path file = normalizedFilePath(unit->file->path, pathEc);
        std::string memberName;
        if (!closureMemberName(file, root, memberName)) {
            err = file.generic_string() +
                  " escapes the package root (it cannot be shipped inside a "
                  ".cp3pak)";
            return false;
        }
        if (!shipped.insert(memberName).second) {
            continue;   // same file reached through two paths; ship once
        }
        std::string data;
        if (!readTextFile(file, data)) {
            err = "cannot read " + file.generic_string();
            return false;
        }
        out.push_back(TarEntry{std::move(memberName), std::move(data)});
        const auto it = edges.find(unit->id);
        if (it != edges.end()) {
            for (const cp3::CompilationUnit* next : it->second) {
                work.push_back(next);
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const TarEntry& a, const TarEntry& b) { return a.name < b.name; });
    return true;
}

}  // namespace cp3::cli
