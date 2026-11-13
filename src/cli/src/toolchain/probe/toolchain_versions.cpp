// =============================================================================
// src/cli/src/toolchain/toolchain_versions.cpp - Clang version discovery.
//
// OWNERSHIP: cli module (src/cli/**). Implements the version half of
// detail/toolchain_select.hpp.
//
// Discovery only reads system locations and asks clang++ for its version.
// cp3c never copies, extracts or installs a compiler into a cp3 directory;
// available versions come from the platform package manager and installation
// is delegated to it. Beta one is Clang-only.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"
#include "detail/toolchain/probe_cache.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string trimCopy(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string firstLine(const std::string& text) {
    const std::size_t end = text.find('\n');
    std::string line = text.substr(0, end);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
        line.pop_back();
    }
    return line;
}

std::string lowerFileName(const fs::path& path) {
    return lowerCopy(path.filename().string());
}

bool matchesDriverName(const std::string& file, const std::string& base) {
    const std::string name = lowerCopy(file);
    const std::string wanted = lowerCopy(base);
    if (name == wanted || name == wanted + ".exe") return true;
    if (name.rfind(wanted + "-", 0) == 0) return true;
    if (name.rfind(wanted + ".exe-", 0) == 0) return true;
    return false;
}

void addDirectoryMatches(const ToolchainCandidate& candidate,
                         const fs::path& dir, std::vector<fs::path>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (matchesDriverName(lowerFileName(entry.path()), candidate.exe)) {
            out.push_back(entry.path());
        }
    }
}

std::vector<fs::path> pathDirectories() {
    std::vector<fs::path> dirs;
    const char* env = std::getenv("PATH");
    if (env == nullptr) return dirs;
    const char separator =
#if defined(_WIN32)
        ';';
#else
        ':';
#endif
    const std::string text(env);
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(separator, start);
        const std::string raw = text.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        const std::string clean = stripSurroundingQuotes(raw);
        if (!clean.empty()) dirs.emplace_back(clean);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return dirs;
}

#if defined(_WIN32)
std::vector<fs::path> wingetPackageDirs(const char* needle) {
    std::vector<fs::path> dirs;
    const char* local = std::getenv("LOCALAPPDATA");
    if (local == nullptr) return dirs;
    std::error_code ec;
    const fs::path root = fs::path(local) / "Microsoft" / "WinGet" / "Packages";
    if (!fs::is_directory(root, ec)) return dirs;
    const std::string wanted = lowerCopy(needle);
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        if (lowerCopy(entry.path().filename().string()).find(wanted) ==
            std::string::npos) {
            continue;
        }
        dirs.push_back(entry.path() / "LLVM" / "bin");
        dirs.push_back(entry.path() / "bin");
        dirs.push_back(entry.path() / "mingw64" / "bin");
        dirs.push_back(entry.path() / "ucrt64" / "bin");
        dirs.push_back(entry.path() / "clang64" / "bin");
    }
    return dirs;
}
#endif

std::vector<fs::path> standardDirectories(const ToolchainCandidate& candidate) {
    std::vector<fs::path> dirs;
    (void)candidate;
#if defined(_WIN32)
    const char* programFiles = std::getenv("ProgramFiles");
    const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
    if (programFiles != nullptr) {
        dirs.push_back(fs::path(programFiles) / "LLVM" / "bin");
    }
    if (programFilesX86 != nullptr) {
        dirs.push_back(fs::path(programFilesX86) / "LLVM" / "bin");
    }
    for (const fs::path& dir : wingetPackageDirs("LLVM")) dirs.push_back(dir);
    for (const fs::path& dir : wingetPackageDirs("WinLibs")) dirs.push_back(dir);
    for (const fs::path& dir : wingetPackageDirs("MSYS2")) dirs.push_back(dir);
    dirs.emplace_back("C:\\msys64\\clang64\\bin");
    dirs.emplace_back("C:\\msys64\\mingw64\\bin");
    dirs.emplace_back("C:\\msys64\\ucrt64\\bin");
#elif defined(__APPLE__)
    dirs.emplace_back("/usr/bin");
    dirs.emplace_back("/usr/local/bin");
    dirs.emplace_back("/opt/homebrew/bin");
    dirs.emplace_back("/usr/local/opt/llvm/bin");
    dirs.emplace_back("/opt/homebrew/opt/llvm/bin");
#else
    dirs.emplace_back("/usr/bin");
    dirs.emplace_back("/usr/local/bin");
    dirs.emplace_back("/snap/bin");
    dirs.emplace_back("/opt/llvm/bin");
    std::error_code ec;
    const fs::path llvmRoot = "/usr/lib";
    if (fs::is_directory(llvmRoot, ec)) {
        for (const fs::directory_entry& entry :
             fs::directory_iterator(llvmRoot, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("llvm-", 0) == 0) {
                dirs.push_back(entry.path() / "bin");
            }
        }
    }
#endif
    return dirs;
}

std::vector<fs::path> candidatePrograms(const ToolchainCandidate& candidate) {
    std::vector<fs::path> found;
    std::vector<fs::path> dirs = pathDirectories();
    const std::vector<fs::path> standard = standardDirectories(candidate);
    dirs.insert(dirs.end(), standard.begin(), standard.end());
    std::vector<std::string> seen;
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        const std::string key = lowerCopy(
            fs::weakly_canonical(dir, ec).string());
        if (key.empty()) continue;
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        addDirectoryMatches(candidate, dir, found);
    }
    return found;
}

std::string versionBanner(const std::string& program) {
    // Discovery probes the same driver on every command; reuse the probe
    // cache (detail/toolchain/probe_cache.hpp) keyed by binary identity.
    ProbeCacheEntry cached;
    if (probeCacheLookup(program, cached) && !cached.banner.empty()) {
        return cached.banner;
    }
    std::string out;
    if (runProcessCapture({program, "--version"}, out) < 0) return {};
    const std::string banner = firstLine(out);
    const std::string version = normalizeCompilerVersion(banner);
    if (!version.empty()) {
        probeCacheStore(program, ProbeCacheEntry{banner, version});
    }
    return banner;
}

bool isClangBanner(const std::string& banner) {
    return lowerCopy(banner).find("clang") != std::string::npos;
}

void sortInstalls(std::vector<ToolchainInstall>& installs) {
    std::sort(installs.begin(), installs.end(),
              [](const ToolchainInstall& a, const ToolchainInstall& b) {
                  const int cmp = compareCompilerVersions(a.version, b.version);
                  if (cmp != 0) return cmp > 0;
                  return a.program < b.program;
              });
}

std::vector<long> numericParts(const std::string& text, std::size_t& count) {
    std::size_t i = 0;
    while (i < text.size() && (text[i] < '0' || text[i] > '9')) ++i;
    std::vector<long> parts;
    while (i < text.size()) {
        if (text[i] < '0' || text[i] > '9') break;
        const std::size_t start = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        parts.push_back(std::strtol(text.substr(start, i - start).c_str(),
                                    nullptr, 10));
        if (i < text.size() && text[i] == '.') {
            ++i;
            continue;
        }
        break;
    }
    count = parts.size();
    while (parts.size() < 3) parts.push_back(0);
    return parts;
}

}  // namespace

std::string normalizeCompilerVersion(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] < '0' || text[i] > '9')) {
            ++i;
        }
        const std::size_t start = i;
        bool dot = false;
        while (i < text.size() &&
               ((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) {
            if (text[i] == '.') dot = true;
            ++i;
        }
        std::string token(text.substr(start, i - start));
        while (!token.empty() && token.back() == '.') token.pop_back();
        if (dot && token.find('.') != std::string::npos) {
            std::vector<std::string> parts;
            std::size_t begin = 0;
            while (begin <= token.size()) {
                const std::size_t end = token.find('.', begin);
                parts.push_back(token.substr(
                    begin, end == std::string::npos ? std::string::npos
                                                    : end - begin));
                if (end == std::string::npos) break;
                begin = end + 1;
            }
            while (parts.size() < 3) parts.push_back("0");
            return parts[0] + "." + parts[1] + "." + parts[2];
        }
    }
    return {};
}

int compareCompilerVersions(const std::string& left, const std::string& right) {
    const std::string a = normalizeCompilerVersion(left);
    const std::string b = normalizeCompilerVersion(right);
    if (a.empty() || b.empty()) {
        if (a == b) return 0;
        return a.empty() ? -1 : 1;
    }
    std::size_t ia = 0;
    std::size_t ib = 0;
    for (int part = 0; part < 3; ++part) {
        const std::size_t ea = a.find('.', ia);
        const std::size_t eb = b.find('.', ib);
        const long va = std::strtol(a.substr(ia, ea - ia).c_str(), nullptr, 10);
        const long vb = std::strtol(b.substr(ib, eb - ib).c_str(), nullptr, 10);
        if (va != vb) return va < vb ? -1 : 1;
        ia = ea == std::string::npos ? a.size() : ea + 1;
        ib = eb == std::string::npos ? b.size() : eb + 1;
    }
    return 0;
}

bool validVersionSpec(const std::string& version) {
    const std::string text = trimCopy(version);
    if (text.empty()) return false;
    bool haveDigit = false;
    bool lastDot = true;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            haveDigit = true;
            lastDot = false;
        } else if (c == '.') {
            if (!haveDigit || lastDot) return false;
            lastDot = true;
        } else {
            return false;
        }
    }
    return haveDigit && !lastDot;
}

bool versionSatisfies(const std::string& installed,
                      const std::string& requested) {
    if (!validVersionSpec(requested)) return false;
    std::size_t requestedCount = 0;
    const std::vector<long> requestedParts =
        numericParts(trimCopy(requested), requestedCount);
    std::size_t installedCount = 0;
    const std::vector<long> installedParts =
        numericParts(installed, installedCount);
    if (installedCount == 0 || requestedCount == 0) return false;
    for (std::size_t i = 0; i < requestedCount; ++i) {
        if (installedParts[i] != requestedParts[i]) return false;
    }
    return true;
}

int minimumClangMajor() { return 16; }

std::vector<ToolchainInstall> discoverToolchainVersions(
    const ToolchainCandidate& candidate, bool validate) {
    if (candidate.id != "clang") return {};
    std::vector<ToolchainInstall> installs;
    for (const fs::path& program : candidatePrograms(candidate)) {
        ToolchainInstall install;
        install.candidate = candidate;
        install.program = program.string();
        install.banner = versionBanner(install.program);
        if (install.banner.empty() || !isClangBanner(install.banner)) {
            continue;
        }
        install.version = normalizeCompilerVersion(install.banner);
        if (install.version.empty()) continue;
        bool duplicate = false;
        for (const ToolchainInstall& existing : installs) {
            if (compareCompilerVersions(existing.version, install.version) ==
                0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) installs.push_back(std::move(install));
    }
    sortInstalls(installs);
    if (validate) {
        for (ToolchainInstall& install : installs) {
            const CandidateStatus status =
                probeToolchainProgram(candidate, install.program, true);
            install.usable = status.usable;
            install.problem = status.problem;
            install.version = status.version.empty() ? install.version
                                                     : status.version;
        }
    }
    return installs;
}

}  // namespace cp3::cli
