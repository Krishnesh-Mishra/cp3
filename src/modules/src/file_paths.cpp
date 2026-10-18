// =============================================================================
// modules/file_paths.cpp - filesystem path helpers.
//
// OWNERSHIP: modules module (src/modules/**). Implements file_paths.h.
//
// The project-root and module-name rules deliberately mirror the CLI's
// (src/cli/src/cache.cpp findProjectRoot / moduleNameFor) so the graph's
// metadata names the same module the CLI build layout uses.
// =============================================================================
#include "file_paths.h"

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace cp3::modules {

std::string canonicalKey(const fs::path& path) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec) {
        resolved = fs::absolute(path, ec);
        if (ec) resolved = path;
    }
    std::string key = resolved.generic_string();
#if defined(_WIN32)
    // Windows paths are case-insensitive; fold ASCII so `C:\X` and `c:\x`
    // dedupe to one unit.
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
#endif
    return key;
}

fs::path folderOf(const fs::path& path) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec) {
        resolved = fs::absolute(path, ec);
        if (ec) resolved = path;
    }
    return resolved.parent_path();
}

fs::path projectRootFor(const fs::path& file) {
    std::error_code ec;
    fs::path dir = file.has_parent_path() ? file.parent_path() : fs::current_path(ec);
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

std::string moduleNameFor(const fs::path& file) {
    std::error_code ec;
    fs::path absolute = fs::absolute(file, ec);
    if (ec) absolute = file;
    const fs::path root = projectRootFor(absolute);
    const fs::path rel = fs::relative(absolute, root, ec);
    fs::path use = (!ec && !rel.empty()) ? rel : absolute.filename();
    use.replace_extension();
    std::string name = use.generic_string();
    if (name.empty()) name = "module";
    return name;
}

bool readWholeFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool startsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace cp3::modules
