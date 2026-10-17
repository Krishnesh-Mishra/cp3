// =============================================================================
// cli/cache.cpp — incremental build layout + manifest DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/cache.hpp.
//
// See private/cache.hpp for the on-disk layout this file reads and writes.
// =============================================================================
#include "private/cache.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace cp3::cli {

fs::path findProjectRoot(const fs::path& entryFile) {
    std::error_code ec;
    fs::path dir = entryFile.has_parent_path() ? entryFile.parent_path()
                                               : fs::current_path(ec);
    if (dir.empty()) dir = ".";
    fs::path current = fs::absolute(dir, ec);
    if (ec) current = dir;
    for (fs::path probe = current; !probe.empty();) {
        if (fs::is_regular_file(probe / "package.json", ec)) return probe;
        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    return current;
}

BuildLayout makeBuildLayout(const fs::path& entryFile) {
    BuildLayout layout;
    layout.projectRoot = findProjectRoot(entryFile);
    layout.buildDir = layout.projectRoot / ".cp3" / "build";
    layout.outDir = layout.projectRoot / ".cp3" / "out";
    layout.manifestPath = layout.buildDir / "build-manifest.txt";
#if defined(_WIN32)
    const std::string suffix = ".exe";
#else
    const std::string suffix = "";
#endif
    // D61: .cp3/build/ holds intermediates only; the executable lives in
    // .cp3/out/<entry-stem>.
    layout.exePath = layout.outDir / (entryFile.stem().string() + suffix);
    layout.includeDirs = compilerIncludeRoots();
    layout.runtimeSources = runtimeSourcePaths();
    return layout;
}

std::string moduleNameFor(const fs::path& entryFile,
                          const fs::path& projectRoot) {
    std::error_code ec;
    const fs::path rel = fs::relative(entryFile, projectRoot, ec);
    fs::path use = (!ec && !rel.empty()) ? rel : entryFile.filename();
    use.replace_extension();
    std::string name = use.generic_string();
    if (name.empty()) name = "module";
    return name;
}

const char* profileNameOf(BuildProfile profile) noexcept {
    switch (profile) {
        case BuildProfile::Release:
            return "release";
        case BuildProfile::Debug:
            return "debug";
        case BuildProfile::Incremental:
        default:
            return "incremental";
    }
}

std::vector<CacheEntry> loadManifest(const fs::path& manifestPath) {
    std::vector<CacheEntry> entries;
    std::ifstream in(manifestPath);
    if (!in) return entries;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        CacheEntry e;
        std::istringstream ss(line);
        if (!std::getline(ss, e.profile, '\t')) continue;
        if (!std::getline(ss, e.source, '\t')) continue;
        if (!std::getline(ss, e.hash, '\t')) continue;
        if (!std::getline(ss, e.cpp, '\t')) continue;
        if (!std::getline(ss, e.obj, '\t')) continue;
        entries.push_back(std::move(e));
    }
    return entries;
}

void saveManifest(const fs::path& manifestPath,
                  const std::vector<CacheEntry>& entries) {
    std::ostringstream out;
    out << "cp3c-manifest-v1\n";
    for (const CacheEntry& e : entries) {
        out << e.profile << '\t' << e.source << '\t' << e.hash << '\t' << e.cpp
            << '\t' << e.obj << '\n';
    }
    writeTextFile(manifestPath, out.str(), nullptr);
}

}  // namespace cp3::cli
