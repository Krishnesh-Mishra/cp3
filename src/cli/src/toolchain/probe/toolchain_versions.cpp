// =============================================================================
// src/cli/src/toolchain/toolchain_versions.cpp - compiler version discovery.
//
// OWNERSHIP: cli module (src/cli/**). Implements the version half of
// detail/toolchain_select.hpp.
//
// Discovery only reads system locations and asks the vendor driver for its
// version. cp3c never copies, extracts or installs a compiler into a cp3
// directory; available versions come from the platform package manager and
// installation is delegated to it.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <system_error>
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
        dirs.push_back(entry.path() / "mingw64" / "bin");
        dirs.push_back(entry.path() / "ucrt64" / "bin");
        dirs.push_back(entry.path() / "clang64" / "bin");
        dirs.push_back(entry.path() / "LLVM" / "bin");
        dirs.push_back(entry.path() / "bin");
    }
    return dirs;
}
#endif

std::vector<fs::path> standardDirectories(const ToolchainCandidate& candidate) {
    std::vector<fs::path> dirs;
#if defined(_WIN32)
    const char* programFiles = std::getenv("ProgramFiles");
    const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
    if (candidate.id == "gcc") {
        dirs.emplace_back("C:\\mingw64\\bin");
        dirs.emplace_back("C:\\msys64\\mingw64\\bin");
        dirs.emplace_back("C:\\msys64\\ucrt64\\bin");
        for (fs::path dir : wingetPackageDirs("WinLibs")) dirs.push_back(dir);
        for (fs::path dir : wingetPackageDirs("MSYS2")) dirs.push_back(dir);
    } else if (candidate.id == "clang") {
        if (programFiles != nullptr) {
            dirs.push_back(fs::path(programFiles) / "LLVM" / "bin");
        }
        if (programFilesX86 != nullptr) {
            dirs.push_back(fs::path(programFilesX86) / "LLVM" / "bin");
        }
        for (fs::path dir : wingetPackageDirs("LLVM")) dirs.push_back(dir);
        dirs.emplace_back("C:\\msys64\\clang64\\bin");
    }
#elif defined(__APPLE__)
    dirs.emplace_back("/usr/bin");
    dirs.emplace_back("/usr/local/bin");
    dirs.emplace_back("/opt/homebrew/bin");
    dirs.emplace_back("/usr/local/opt/llvm/bin");
    dirs.emplace_back("/opt/homebrew/opt/llvm/bin");
    dirs.emplace_back("/usr/local/opt/gcc/bin");
    dirs.emplace_back("/opt/homebrew/opt/gcc/bin");
#else
    dirs.emplace_back("/usr/bin");
    dirs.emplace_back("/usr/local/bin");
    dirs.emplace_back("/snap/bin");
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

std::string versionBanner(const ToolchainCandidate& candidate,
                          const std::string& program) {
    std::vector<std::string> args{program};
    if (!candidate.msvc) args.push_back("--version");
    std::string out;
    if (runProcessCapture(args, out) < 0) return {};
    return firstLine(out);
}

bool familyMatches(const ToolchainCandidate& candidate,
                   const std::string& banner) {
    const std::string lower = lowerCopy(banner);
    if (candidate.id == "gcc" && lower.find("clang") != std::string::npos) {
        return false;
    }
    if (candidate.id == "clang" && lower.find("gcc") != std::string::npos &&
        lower.find("clang") == std::string::npos) {
        return false;
    }
    return true;
}

void sortInstalls(std::vector<ToolchainInstall>& installs) {
    std::sort(installs.begin(), installs.end(),
              [](const ToolchainInstall& a, const ToolchainInstall& b) {
                  const int cmp = compareCompilerVersions(a.version, b.version);
                  if (cmp != 0) return cmp > 0;
                  return a.program < b.program;
              });
}

#if defined(_WIN32)
fs::path vswherePath() {
    const char* x86 = std::getenv("ProgramFiles(x86)");
    const char* native = std::getenv("ProgramFiles");
    if (x86 != nullptr) {
        const fs::path candidate =
            fs::path(x86) / "Microsoft" / "Visual Studio" / "Installer" /
            "vswhere.exe";
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    if (native != nullptr) {
        const fs::path candidate =
            fs::path(native) / "Microsoft" / "Visual Studio" / "Installer" /
            "vswhere.exe";
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

std::vector<fs::path> visualStudioInstalls() {
    std::vector<fs::path> installs;
    const fs::path vswhere = vswherePath();
    if (!vswhere.empty()) {
        std::string out;
        if (runProcessCapture(
                {vswhere.string(), "-all", "-products", "*", "-requires",
                 "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-property", "installationPath"},
                out) >= 0) {
            std::size_t start = 0;
            while (start <= out.size()) {
                const std::size_t end = out.find('\n', start);
                const std::string line = trimCopy(out.substr(
                    start, end == std::string::npos ? std::string::npos
                                                    : end - start));
                if (!line.empty()) installs.emplace_back(line);
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
    }
    return installs;
}

void addMsvcToolsets(const fs::path& install,
                     std::vector<ToolchainInstall>& out) {
    std::error_code ec;
    const fs::path root = install / "VC" / "Tools" / "MSVC";
    if (!fs::is_directory(root, ec)) return;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        const std::string version = entry.path().filename().string();
        const fs::path cl = entry.path() / "bin" / "Hostx64" / "x64" / "cl.exe";
        const fs::path link =
            entry.path() / "bin" / "Hostx64" / "x64" / "link.exe";
        if (!fs::is_regular_file(cl, ec)) continue;
        ToolchainInstall install;
        install.candidate = {"msvc", "MSVC cl", "cl", true, false};
        install.program = cl.string();
        install.linkProgram = fs::is_regular_file(link, ec)
                                  ? link.string()
                                  : std::string();
        install.version = version;
        out.push_back(std::move(install));
    }
}
#endif

std::vector<ToolchainInstall> discoverMsvc() {
    std::vector<ToolchainInstall> installs;
#if defined(_WIN32)
    for (const fs::path& visualStudio : visualStudioInstalls()) {
        addMsvcToolsets(visualStudio, installs);
    }
    if (installs.empty()) {
        std::string cl;
        if (findExecutable("cl", cl)) {
            ToolchainInstall install;
            install.candidate = {"msvc", "MSVC cl", "cl", true, false};
            install.program = cl;
            std::string link;
            if (findExecutable("link", link)) install.linkProgram = link;
            install.banner = versionBanner(install.candidate, cl);
            install.version = normalizeCompilerVersion(install.banner);
            installs.push_back(std::move(install));
        }
    }
    for (ToolchainInstall& install : installs) {
        if (install.banner.empty()) {
            install.banner =
                versionBanner(install.candidate, install.program);
        }
        if (install.version.empty()) {
            install.version = normalizeCompilerVersion(install.banner);
        }
        if (install.linkProgram.empty()) {
            install.problem =
                "link.exe from the same toolset is not present";
        }
    }
    sortInstalls(installs);
#endif
    return installs;
}

}  // namespace

std::string normalizeCompilerVersion(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() &&
               (text[i] < '0' || text[i] > '9')) {
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

std::vector<ToolchainInstall> discoverToolchainVersions(
    const ToolchainCandidate& candidate, bool validate) {
    if (candidate.msvc) return discoverMsvc();
    std::vector<ToolchainInstall> installs;
    for (const fs::path& program : candidatePrograms(candidate)) {
        ToolchainInstall install;
        install.candidate = candidate;
        install.program = program.string();
        install.banner = versionBanner(candidate, install.program);
        if (install.banner.empty() || !familyMatches(candidate, install.banner)) {
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
            const CandidateStatus status = probeToolchainProgram(
                candidate, install.program, install.linkProgram, true);
            install.usable = status.usable;
            install.problem = status.problem;
            install.version = status.version.empty() ? install.version
                                                     : status.version;
        }
    }
    return installs;
}

}  // namespace cp3::cli
