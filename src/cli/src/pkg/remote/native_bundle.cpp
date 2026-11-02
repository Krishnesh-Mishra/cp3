// =============================================================================
// src/cli/src/pkg/remote/native_bundle.cpp - companion native-asset DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/pkg/native_bundle.hpp.
//
// The package's generated `index.cp3` names the native binding it imports. The
// release's platform bundle is downloaded from the same tag and extracted at
// the project root, matching the layout the bundle documents:
//
//   native/include/...   C++ headers
//   native/lib/...       import libraries
//   native/runtime/...   runtime DLLs
//   .cp3/native/<id>/    native.json + index.cp3
//
// Extraction refuses absolute paths and `..` components, so a release asset
// cannot write outside the project.
// =============================================================================
#include "detail/pkg/native_bundle.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "detail/archive/gzip.hpp"
#include "detail/archive/platform.hpp"
#include "detail/archive/tar.hpp"
#include "detail/archive/zip.hpp"
#include "detail/core/util.hpp"
#include "detail/pkg/github.hpp"

namespace cp3::cli {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool endsWithIgnoreCase(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return lower(text.substr(text.size() - suffix.size())) == lower(suffix);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool isArchiveAsset(const std::string& name) {
    return endsWithIgnoreCase(name, ".zip") ||
           endsWithIgnoreCase(name, ".tar.gz") ||
           endsWithIgnoreCase(name, ".tgz");
}

std::vector<std::string> familyTokens(const PlatformId& id) {
    if (id.family == "windows") return {"windows", "win"};
    if (id.family == "linux") return {"linux"};
    if (id.family == "mac") return {"macos", "mac", "darwin"};
    return {};
}

std::vector<std::string> archTokens(const PlatformId& id) {
    if (id.arch == "x64") return {"x64", "x86_64", "amd64"};
    if (id.arch == "x86") return {"x86", "i386", "i686"};
    if (id.arch == "arm64") return {"arm64", "aarch64"};
    return {};
}

std::vector<std::string> combinedTokens(const PlatformId& id) {
    if (id.family == "windows" && id.arch == "x64") {
        return {"win64", "windows-x64", "windows64", "win-x64"};
    }
    if (id.family == "windows" && id.arch == "x86") {
        return {"win32", "windows-x86", "windows32", "win-x86"};
    }
    if (id.family == "windows" && id.arch == "arm64") {
        return {"winarm64", "windows-arm64", "win-arm64"};
    }
    if (id.family == "linux" && id.arch == "x64") {
        return {"linux-x64", "linux64", "linux-amd64", "linux-x86_64"};
    }
    if (id.family == "linux" && id.arch == "arm64") {
        return {"linux-arm64", "linux-aarch64"};
    }
    if (id.family == "mac" && id.arch == "arm64") {
        return {"mac-arm64", "macos-arm64", "darwin-arm64", "macos-aarch64",
                "mac-aarch64"};
    }
    if (id.family == "mac" && id.arch == "x64") {
        return {"mac-x64", "macos-x64", "darwin-x64"};
    }
    return {};
}

bool containsAny(const std::string& text,
                 const std::vector<std::string>& tokens) {
    for (const std::string& token : tokens) {
        if (contains(text, token)) return true;
    }
    return false;
}

std::string safeAssetName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                        c == '.';
        out.push_back(ok ? c : '_');
    }
    return out.empty() ? "native-bundle.zip" : out;
}

// Finds the native binding ids named by the package's generated surface. The
// generated entry is text, so this is a scan for the exact import grammar, not
// a full parse: the compiler still validates the binding itself.
bool collectNativeIds(const fs::path& packageDir, std::vector<std::string>& ids,
                      std::string& err) {
    std::error_code ec;
    if (!fs::is_directory(packageDir, ec)) {
        err = "package directory does not exist: " + packageDir.string();
        return false;
    }
    std::vector<fs::path> sources;
    for (const auto& entry :
         fs::recursive_directory_iterator(packageDir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".cp3") {
            sources.push_back(entry.path());
        }
    }
    for (const fs::path& source : sources) {
        std::string text;
        if (!readTextFile(source, text)) continue;
        std::size_t at = 0;
        const std::string needle = "import native";
        while ((at = text.find(needle, at)) != std::string::npos) {
            std::size_t pos = at + needle.size();
            while (pos < text.size() &&
                   (text[pos] == ' ' || text[pos] == '\t')) {
                ++pos;
            }
            if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
                at = pos;
                continue;
            }
            const char quote = text[pos++];
            const std::size_t end = text.find(quote, pos);
            if (end == std::string::npos) break;
            const std::string id = text.substr(pos, end - pos);
            if (!id.empty() &&
                std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(id);
            }
            at = end + 1;
        }
    }
    return true;
}

bool extractBundle(const fs::path& archive, const fs::path& projectRoot,
                   std::string& err) {
    std::string bytes;
    if (!readTextFile(archive, bytes)) {
        err = "cannot read native bundle " + archive.string();
        return false;
    }
    std::vector<TarEntry> entries;
    if (endsWithIgnoreCase(archive.string(), ".zip")) {
        if (!zipRead(bytes, entries, err)) {
            err = archive.filename().string() + ": " + err;
            return false;
        }
    } else {
        std::string tarBytes;
        if (!gzipDecompress(bytes, tarBytes, err)) {
            err = archive.filename().string() + ": " + err;
            return false;
        }
        if (!tarRead(tarBytes, entries, err)) {
            err = archive.filename().string() + ": " + err;
            return false;
        }
    }
    for (const TarEntry& entry : entries) {
        const fs::path dest = projectRoot / fs::path(entry.name);
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        if (ec) {
            err = "cannot create " + dest.parent_path().string() + " (" +
                  ec.message() + ")";
            return false;
        }
        if (!writeTextFile(dest, entry.data, nullptr)) {
            err = "cannot write " + dest.string();
            return false;
        }
    }
    return true;
}

}  // namespace

bool packageNeedsNativeBinding(const fs::path& packageDir,
                               std::vector<std::string>& ids, std::string& err) {
    ids.clear();
    if (!collectNativeIds(packageDir, ids, err)) return false;
    return !ids.empty();
}

bool isNativeBundleForCurrentPlatform(const std::string& assetName) {
    if (!isArchiveAsset(assetName)) return false;
    const std::string name = lower(assetName);
    const PlatformId& here = currentPlatform();
    if (containsAny(name, combinedTokens(here))) return true;
    return containsAny(name, familyTokens(here)) &&
           containsAny(name, archTokens(here));
}

bool installCompanionNativeBundles(const fs::path& projectRoot,
                                   const fs::path& packageDir,
                                   const std::vector<GithubAsset>& assets,
                                   const fs::path& tempRoot, std::string& err) {
    std::vector<std::string> ids;
    if (!packageNeedsNativeBinding(packageDir, ids, err)) {
        // A missing native import is the normal case; a real scan failure
        // leaves a message in `err`.
        return err.empty();
    }
    // v0.2 archives embed their native binding under `.cp3/native/<id>`. Only
    // reach for a companion platform asset for bindings the archive did not
    // already install (beta-one packages published source-only).
    std::vector<std::string> missing;
    for (const std::string& id : ids) {
        std::error_code ec;
        const fs::path binding =
            projectRoot / ".cp3" / "native" / id / "native.json";
        if (!fs::is_regular_file(binding, ec)) missing.push_back(id);
    }
    if (missing.empty()) return true;
    std::vector<GithubAsset> matching;
    for (const GithubAsset& asset : assets) {
        if (isNativeBundleForCurrentPlatform(asset.name)) {
            matching.push_back(asset);
        }
    }
    if (matching.empty()) {
        std::fprintf(stderr,
                     "cp3c: warning: package imports native binding '%s', but "
                     "this release has no native bundle for %s; build it with "
                     "'cp3 native-install' or install the bundle manually\n",
                     missing.front().c_str(),
                     platformIdText(currentPlatform()).c_str());
        return true;
    }
    for (const GithubAsset& asset : matching) {
        const fs::path file = tempRoot / safeAssetName(asset.name);
        if (!downloadGithubAsset(asset.url, file, err)) return false;
        if (!extractBundle(file, projectRoot, err)) return false;
        std::fprintf(stdout, "cp3c: installed native bundle %s\n",
                     asset.name.c_str());
    }
    for (const std::string& id : ids) {
        const fs::path binding =
            projectRoot / ".cp3" / "native" / id / "native.json";
        std::error_code ec;
        if (!fs::is_regular_file(binding, ec)) {
            std::fprintf(stderr,
                         "cp3c: warning: native binding '%s' is still missing "
                         "at %s\n",
                         id.c_str(), binding.string().c_str());
        }
    }
    return true;
}

}  // namespace cp3::cli
