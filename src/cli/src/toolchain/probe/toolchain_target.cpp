// =============================================================================
// src/cli/src/toolchain/toolchain_target.cpp - Clang target selection.
//
// OWNERSHIP: cli module (src/cli/**). Implements the target half of
// detail/toolchain_select.hpp.
//
// A stock Windows LLVM install defaults to the MSVC target
// (`x86_64-pc-windows-msvc`). Beta one targets MinGW-w64 GNU instead, so this
// unit asks the driver for its default triple, rewrites a Windows MSVC triple
// to the matching GNU one, locates a MinGW-w64 sysroot when Clang cannot find
// one by itself, and verifies the result by really compiling and linking a
// probe program. The flags that survive are used for the probe AND for every
// real build, so a Clang that probes clean is a Clang that builds.
//
// cp3c never invokes another compiler here. MinGW-w64 provides the target's
// headers and libraries; Clang remains the only compiler driver.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"

#include <chrono>
#include <cstdlib>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string trimCopy(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// The driver prints one triple per line; take the first non-empty one.
std::string defaultTriple(const std::string& program) {
    std::string out;
    if (runProcessCapture({program, "-print-target-triple"}, out) < 0) {
        return {};
    }
    std::size_t begin = 0;
    while (begin < out.size()) {
        const std::size_t end = out.find('\n', begin);
        const std::string line = trimCopy(out.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin));
        if (!line.empty()) return line;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

// `x86_64-pc-windows-msvc` -> `x86_64-w64-windows-gnu`.
//
// Only a Windows MSVC triple is rewritten. A GNU target already, or a
// macOS/Linux target, is left exactly as the driver reports it.
std::string gnuTripleFor(const std::string& triple) {
    if (triple.find("windows-msvc") == std::string::npos) return {};
    const std::size_t dash = triple.find('-');
    if (dash == std::string::npos || dash == 0) return {};
    const std::string arch = triple.substr(0, dash);
    return arch + "-w64-windows-gnu";
}

std::string architectureOf(const std::string& triple) {
    const std::size_t dash = triple.find('-');
    if (dash == std::string::npos || dash == 0) return {};
    return triple.substr(0, dash);
}

#if defined(_WIN32)
std::vector<fs::path> pathDirectories() {
    std::vector<fs::path> dirs;
    const char* env = std::getenv("PATH");
    if (env == nullptr) return dirs;
    const std::string text(env);
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string raw = text.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        const std::string clean = stripSurroundingQuotes(raw);
        if (!clean.empty()) dirs.emplace_back(clean);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return dirs;
}

std::vector<fs::path> wingetRuntimeRoots() {
    std::vector<fs::path> roots;
    const char* local = std::getenv("LOCALAPPDATA");
    if (local == nullptr) return roots;
    std::error_code ec;
    const fs::path packages =
        fs::path(local) / "Microsoft" / "WinGet" / "Packages";
    if (!fs::is_directory(packages, ec)) return roots;
    for (const fs::directory_entry& entry :
         fs::directory_iterator(packages, ec)) {
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().string();
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (name.find("winlibs") == std::string::npos &&
            name.find("msys2") == std::string::npos) {
            continue;
        }
        roots.push_back(entry.path());
        roots.push_back(entry.path() / "mingw64");
        roots.push_back(entry.path() / "ucrt64");
        roots.push_back(entry.path() / "clang64");
    }
    return roots;
}

bool looksLikeMingwRoot(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return false;
    if (fs::is_directory(root / "include" / "c++", ec)) return true;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (name.find("-w64-mingw32") != std::string::npos &&
            fs::is_directory(entry.path() / "include", ec)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> mingwPrefixes(const std::string& program) {
    std::vector<fs::path> candidates;
    if (const char* env = std::getenv("CP3_MINGW_ROOT")) {
        if (*env != '\0') candidates.emplace_back(env);
    }
    candidates.emplace_back("C:\\mingw64");
    candidates.emplace_back("C:\\msys64\\mingw64");
    candidates.emplace_back("C:\\msys64\\ucrt64");
    candidates.emplace_back("C:\\msys64\\clang64");
    for (const fs::path& root : wingetRuntimeRoots()) candidates.push_back(root);

    // A Clang living in <prefix>/bin can itself be the sysroot owner (MSYS2
    // clang64, llvm-mingw, WinLibs). The parent of its bin directory is the
    // natural candidate.
    const fs::path bin = fs::path(program).parent_path();
    if (!bin.empty()) candidates.push_back(bin.parent_path());
    // Any PATH entry ending in bin gives the same hint without invoking any
    // other compiler that may live there.
    for (const fs::path& dir : pathDirectories()) {
        if (dir.filename() == "bin") candidates.push_back(dir.parent_path());
    }

    std::vector<std::string> prefixes;
    for (const fs::path& root : candidates) {
        if (root.empty() || !looksLikeMingwRoot(root)) continue;
        std::error_code ec;
        const std::string text = root.lexically_normal().string();
        bool duplicate = false;
        for (const std::string& existing : prefixes) {
            if (existing == text) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) prefixes.push_back(text);
    }
    return prefixes;
}

struct ProbePaths {
    fs::path dir;
    fs::path cpp;
    fs::path exe;
    ~ProbePaths() {
        if (!dir.empty()) removeTree(dir);
    }
};
#endif

#if defined(_WIN32)
ProbePaths makeProbePaths() {
    static unsigned counter = 0;
    ProbePaths paths;
    std::error_code ec;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    paths.dir = fs::temp_directory_path(ec) /
                ("cp3-target-probe-" + std::to_string(stamp) + "_" +
                 std::to_string(++counter));
    fs::create_directories(paths.dir, ec);
    if (ec) {
        paths.dir.clear();
        return paths;
    }
    paths.cpp = paths.dir / "probe.cpp";
    paths.exe = paths.dir / "probe.exe";
    return paths;
}

bool compilesWith(const std::string& program,
                  const std::vector<std::string>& targetFlags) {
    ProbePaths paths = makeProbePaths();
    if (paths.dir.empty()) return false;
    const std::string source =
        "#include <cstdio>\n"
        "int main() { std::printf(\"ok\\n\"); return 0; }\n";
    if (!writeTextFile(paths.cpp, source, nullptr)) return false;
    std::vector<std::string> args{program};
    for (const std::string& flag : targetFlags) args.push_back(flag);
    args.push_back("-std=c++20");
    args.push_back(paths.cpp.string());
    args.push_back("-o");
    args.push_back(paths.exe.string());
    std::string log;
    return runProcessCapture(args, log) == 0;
}
#endif

// Flag sets are probed once per driver and remembered: the probe and every
// later build must agree, and re-probing on each call would be slow.
std::map<std::string, std::vector<std::string>>& flagCache() {
    static std::map<std::string, std::vector<std::string>> cache;
    return cache;
}

}  // namespace

std::vector<std::string> toolchainTargetFlags(const std::string& program) {
    if (program.empty()) return {};
#if !defined(_WIN32)
    // POSIX Clang finds its own standard library; nothing to rewrite.
    return {};
#else
    auto& cache = flagCache();
    const auto it = cache.find(program);
    if (it != cache.end()) return it->second;

    const std::string triple = defaultTriple(program);
    const std::string gnu = gnuTripleFor(triple);
    std::vector<std::string> flags;

    if (gnu.empty()) {
        // The driver already targets GNU (or another native target). Test its
        // own defaults first; naming a sysroot is only needed when that fails.
        if (compilesWith(program, {})) {
            cache[program] = flags;
            return flags;
        }
        const std::string arch = architectureOf(triple);
        if (!arch.empty()) {
            const std::string fallback = arch + "-w64-windows-gnu";
            const std::vector<std::string> plain{"--target=" + fallback};
            if (compilesWith(program, plain)) {
                flags = plain;
                cache[program] = flags;
                return flags;
            }
        }
    }

    std::string target = gnu;
    if (target.empty()) {
        const std::string arch = architectureOf(triple);
        target = (arch.empty() ? "x86_64" : arch) + "-w64-windows-gnu";
    }
    const std::vector<std::string> plain{"--target=" + target};
    if (compilesWith(program, plain)) {
        flags = plain;
        cache[program] = flags;
        return flags;
    }
    for (const std::string& prefix : mingwPrefixes(program)) {
        const std::vector<std::string> explicitFlags{
            "--target=" + target, "--gcc-toolchain=" + prefix};
        if (compilesWith(program, explicitFlags)) {
            flags = explicitFlags;
            break;
        }
    }
    cache[program] = flags;
    return flags;
#endif
}

void resetToolchainTargetFlagsCache() { flagCache().clear(); }

}  // namespace cp3::cli
