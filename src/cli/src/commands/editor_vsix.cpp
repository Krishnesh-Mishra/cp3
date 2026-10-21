// =============================================================================
// src/cli/src/commands/editor_vsix.cpp - bundled VSIX discovery, hashing, export.
//
// OWNERSHIP: cli module (src/cli/**). Implements the VSIX half of
// detail/editor.hpp for `cp3c editor-plugin`. Never downloads or builds the
// extension; a package that was built without one reports that and stops.
// =============================================================================
#include "detail/editor.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <vector>

#include "detail/checksum.hpp"
#include "detail/semver.hpp"

namespace cp3::cli {

namespace {

std::string fileNameOf(const fs::path& path) { return path.filename().string(); }

bool isVsixName(const std::string& name) {
    return name.rfind("cp3-vscode-", 0) == 0 && name.size() > 11 &&
           name.compare(name.size() - 5, 5, ".vsix") == 0;
}

std::string versionFromVsixName(const std::string& name) {
    const std::string prefix = "cp3-vscode-";
    if (name.rfind(prefix, 0) == 0 && name.size() > prefix.size() + 5) {
        return name.substr(prefix.size(), name.size() - prefix.size() - 5);
    }
    return {};
}

bool versionNewer(const std::string& candidate, const std::string& current) {
    const SemVer a = parseSemVer(candidate);
    const SemVer b = parseSemVer(current);
    if (a.valid && b.valid) return compareSemVer(a, b) > 0;
    if (a.valid != b.valid) return a.valid;
    return candidate > current;
}

void addUnique(std::vector<fs::path>& dirs, const fs::path& dir) {
    if (dir.empty()) return;
    for (const fs::path& existing : dirs) {
        if (existing == dir) return;
    }
    dirs.push_back(dir);
}

std::vector<fs::path> vsixDirectories() {
    std::vector<fs::path> dirs;
    if (const char* env = std::getenv("CP3_ROOT")) {
        const std::string root = stripSurroundingQuotes(env);
        if (!root.empty()) addUnique(dirs, fs::path(root) / "share/vscode");
    }
    const fs::path repo = repositoryRoot();
    if (!repo.empty()) {
        addUnique(dirs, repo / "share" / "vscode");
        addUnique(dirs, repo / "extensions" / "cp3-vscode" / "dist");
    }
    return dirs;
}

fs::path pickHighest(const std::vector<fs::path>& candidates) {
    fs::path best;
    std::string bestVersion;
    for (const fs::path& candidate : candidates) {
        const std::string version = versionFromVsixName(fileNameOf(candidate));
        if (best.empty() || versionNewer(version, bestVersion)) {
            best = candidate;
            bestVersion = version;
        }
    }
    return best;
}

}  // namespace

bool hashFileSha256(const fs::path& path, std::string& hex, std::string& err) {
    std::string bytes;
    if (!readTextFile(path, bytes)) {
        err = "cannot read " + path.string() + " for hashing";
        return false;
    }
    hex = sha256Hex(bytes);
    return true;
}

bool inspectVsix(const fs::path& path, VsixInfo& out, std::string& err) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        err = "VSIX '" + path.string() + "' does not exist";
        return false;
    }
    out = VsixInfo{};
    fs::path absolute = fs::absolute(path, ec);
    out.path = ec ? path : absolute.lexically_normal();
    out.version = versionFromVsixName(fileNameOf(out.path));
    out.found = true;
    return hashFileSha256(out.path, out.sha256, err);
}

VsixInfo findBundledVsix() {
    VsixInfo info;
    std::vector<fs::path> candidates;
    for (const fs::path& dir : vsixDirectories()) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        const fs::directory_iterator end;
        for (fs::directory_iterator it(dir, ec); it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc)) continue;
            if (!isVsixName(fileNameOf(it->path()))) continue;
            candidates.push_back(it->path());
        }
    }
    if (candidates.empty()) return info;

    const fs::path best = pickHighest(candidates);
    std::string err;
    if (!inspectVsix(best, info, err)) {
        info = VsixInfo{};
        info.path = best;
        info.version = versionFromVsixName(fileNameOf(best));
        info.found = true;
    }
    return info;
}

bool exportVsix(const VsixInfo& vsix, const fs::path& out, bool force,
                fs::path& destination, std::string& err) {
    if (!vsix.found) {
        err = "no bundled cp3 extension package was found";
        return false;
    }
    std::error_code ec;
    const fs::path baseName = vsix.path.filename();
    fs::path target = out;
    if (target.empty()) {
        target = fs::current_path(ec) / baseName;
        if (ec) {
            err = "cannot determine the current directory";
            return false;
        }
    } else if (fs::is_directory(target, ec)) {
        target /= baseName;
    } else {
        const std::string text = target.string();
        if (!text.empty() && (text.back() == '/' || text.back() == '\\')) {
            fs::create_directories(target, ec);
            if (ec) {
                err = "cannot create " + target.string() + " (" + ec.message() + ")";
                return false;
            }
            target /= baseName;
        }
    }

    fs::path absolute = fs::absolute(target, ec);
    if (ec) {
        err = "cannot resolve the export destination";
        return false;
    }
    destination = absolute.lexically_normal();

    bool exists = fs::exists(destination, ec);
    if (exists && !force) {
        err = "destination already exists: " + destination.string() +
              " (use --force to overwrite)";
        return false;
    }
    if (exists) {
        std::error_code sameEc;
        if (fs::equivalent(vsix.path, destination, sameEc)) {
            err = "the source and destination are the same file";
            return false;
        }
    }

    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        err = "cannot create " + destination.parent_path().string() + " (" +
              ec.message() + ")";
        return false;
    }
    fs::copy_file(vsix.path, destination,
                  force ? fs::copy_options::overwrite_existing
                        : fs::copy_options::none, ec);
    if (ec) {
        err = "cannot copy the VSIX to " + destination.string() + " (" +
              ec.message() + ")";
        return false;
    }

    std::string hash;
    if (!hashFileSha256(destination, hash, err)) return false;
    (void)hash;
    return true;
}

}  // namespace cp3::cli
