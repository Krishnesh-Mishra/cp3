// =============================================================================
// src/cli/src/pkg/cache.cpp - incremental build layout + manifest DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/cache.hpp.
//
// See detail/cache.hpp for the on-disk layout this file reads and writes.
// =============================================================================
#include "detail/cache/cache.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include "detail/core/util.hpp"

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
    layout.cacheDir = layout.projectRoot / ".cp3" / "cache";
    layout.outDir = layout.projectRoot / ".cp3" / "out";
    layout.manifestPath = layout.cacheDir / "build-manifest.txt";
#if defined(_WIN32)
    const std::string suffix = ".exe";
#else
    const std::string suffix = "";
#endif
    // D61: .cp3/build/ holds generated C++ only, .cp3/cache/ holds the cached
    // objects and manifest; the executable lives in .cp3/out/<entry-stem>.
    layout.exePath = layout.outDir / (entryFile.stem().string() + suffix);
    layout.includeDirs = compilerIncludeRoots();
    // runtimeSources is filled by the build driver from the generated program
    // text (runtimeSourcePaths in detail/util.hpp): the set is per program.
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
    if (!std::getline(in, line) || line != "cp3c-manifest-v3") {
        return entries;  // v2/v1 (or corrupt): retire every object once.
    }
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        CacheEntry e;
        std::istringstream ss(line);
        if (!std::getline(ss, e.profile, '\t')) continue;
        if (!std::getline(ss, e.source, '\t')) continue;
        if (!std::getline(ss, e.hash, '\t')) continue;
        if (!std::getline(ss, e.header, '\t')) continue;
        if (!std::getline(ss, e.compiler, '\t')) continue;
        if (!std::getline(ss, e.cpp, '\t')) continue;
        if (!std::getline(ss, e.obj, '\t')) continue;
        entries.push_back(std::move(e));
    }
    return entries;
}

void saveManifest(const fs::path& manifestPath,
                  const std::vector<CacheEntry>& entries) {
    std::ostringstream out;
    out << "cp3c-manifest-v3\n";
    for (const CacheEntry& e : entries) {
        out << e.profile << '\t' << e.source << '\t' << e.hash << '\t'
            << e.header << '\t' << e.compiler << '\t' << e.cpp << '\t'
            << e.obj << '\n';
    }
    writeTextFile(manifestPath, out.str(), nullptr);
}

std::string pchFingerprintOf() {
    return headerFingerprintOf(compilerIncludeRoots());
}

std::string headerFingerprintOf(const std::vector<fs::path>& includeDirs) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const fs::path& root : includeDirs) {
        if (!fs::is_directory(root, ec)) continue;
        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(root, ec)) {
            if (entry.is_regular_file(ec)) files.push_back(entry.path());
        }
    }
    if (files.empty()) return std::string();
    std::sort(files.begin(), files.end());
    std::string material;
    for (const fs::path& file : files) {
        std::string content;
        if (!readTextFile(file, content)) continue;
        material += file.generic_string();
        material += '\n';
        material += content;
        material += '\n';
    }
    return std::to_string(fnv1a64(material));
}

}  // namespace cp3::cli
