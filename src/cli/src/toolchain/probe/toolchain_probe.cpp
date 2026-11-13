// =============================================================================
// src/cli/src/toolchain/toolchain_probe.cpp - Clang candidate probing.
//
// OWNERSHIP: cli module (src/cli/**). Implements the candidate half of
// detail/toolchain_select.hpp.
//
// A candidate is only "usable" after a tiny C++20 source compiles, links and
// runs in a temporary directory. That is deliberately stricter than simply
// finding a driver: `cp3c toolchain list` must not call a compiler installed
// when it cannot produce a binary. Beta one has one candidate family: Clang.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"
#include "detail/toolchain/probe_cache.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <io.h>
#else
#  include <unistd.h>
#endif

#include "detail/core/util.hpp"

namespace cp3::cli {

// ---------------------------------------------------------------------------
// Probe cache (see detail/toolchain/probe_cache.hpp)
// ---------------------------------------------------------------------------
namespace {

constexpr const char* kProbeCacheFile = "toolchain-probe-cache";

fs::path probeCacheConfigDir() {
    if (const char* override = std::getenv("CP3_CONFIG_DIR")) {
        if (*override != '\0') return fs::path(override);
    }
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) {
        if (*appdata != '\0') return fs::path(appdata) / "cp3";
    }
    if (const char* profile = std::getenv("USERPROFILE")) {
        if (*profile != '\0') return fs::path(profile) / ".config" / "cp3";
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg != '\0') return fs::path(xdg) / "cp3";
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') return fs::path(home) / ".config" / "cp3";
    }
#endif
    std::error_code ec;
    return fs::current_path(ec) / ".cp3-config";
}

std::string cacheField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back((c == '\t' || c == '\r' || c == '\n') ? ' ' : c);
    }
    return out;
}

// path + size + last-write-time identity of the compiler binary.
struct ProbeBinaryIdentity {
    bool valid = false;
    std::uintmax_t size = 0;
    long long mtime = 0;
};

ProbeBinaryIdentity probeBinaryIdentity(const std::string& program) {
    ProbeBinaryIdentity id;
    std::error_code ec;
    const fs::path path(program);
    id.size = fs::file_size(path, ec);
    if (ec) return id;
    const auto time = fs::last_write_time(path, ec);
    if (ec) return id;
    id.mtime = static_cast<long long>(time.time_since_epoch().count());
    id.valid = true;
    return id;
}

}  // namespace

bool probeCacheLookup(const std::string& program, ProbeCacheEntry& out) {
    const ProbeBinaryIdentity identity = probeBinaryIdentity(program);
    if (!identity.valid) return false;
    std::error_code ec;
    std::ifstream in(probeCacheConfigDir() / kProbeCacheFile);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') continue;
        const std::size_t tab1 = line.find('\t');
        const std::size_t tab2 = tab1 == std::string::npos ? std::string::npos
                                                           : line.find('\t', tab1 + 1);
        const std::size_t tab3 = tab2 == std::string::npos ? std::string::npos
                                                           : line.find('\t', tab2 + 1);
        const std::size_t tab4 = tab3 == std::string::npos ? std::string::npos
                                                           : line.find('\t', tab3 + 1);
        if (tab4 == std::string::npos) continue;
        const std::string path = line.substr(0, tab1);
        if (path != program) continue;
        std::uintmax_t size = 0;
        long long mtime = 0;
        try {
            size = static_cast<std::uintmax_t>(
                std::stoull(line.substr(tab1 + 1, tab2 - tab1 - 1)));
            mtime = std::stoll(line.substr(tab2 + 1, tab3 - tab2 - 1));
        } catch (const std::exception&) {
            continue;
        }
        if (size != identity.size || mtime != identity.mtime) return false;
        out.banner = line.substr(tab3 + 1, tab4 - tab3 - 1);
        out.version = line.substr(tab4 + 1);
        return !out.version.empty();
    }
    return false;
}

bool probeCacheStore(const std::string& program, const ProbeCacheEntry& entry) {
    const ProbeBinaryIdentity identity = probeBinaryIdentity(program);
    if (!identity.valid) return false;
    const fs::path dir = probeCacheConfigDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;
    // Rewrite the file without this program's stale entry, capped at 64 lines.
    std::vector<std::string> kept;
    {
        std::ifstream in(dir / kProbeCacheFile);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line.front() == '#') continue;
            if (line.compare(0, program.size() + 1, program + "\t") == 0) continue;
            kept.push_back(line);
        }
    }
    if (kept.size() >= 64) kept.erase(kept.begin(), kept.begin() + (kept.size() - 63));
    std::ofstream outFile(dir / kProbeCacheFile, std::ios::trunc);
    if (!outFile) return false;
    outFile << "# cp3 toolchain probe cache (v1) - see probe_cache.hpp\n";
    for (const std::string& line : kept) outFile << line << '\n';
    outFile << cacheField(program) << '\t' << identity.size << '\t' << identity.mtime
            << '\t' << cacheField(entry.banner) << '\t' << cacheField(entry.version)
            << '\n';
    return static_cast<bool>(outFile);
}

namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string firstLine(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        std::string line = text.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty()) return line;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

std::string uniqueToken(const char* prefix) {
    static unsigned counter = 0;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    ++counter;
#if defined(_WIN32)
    const long pid = _getpid();
#else
    const long pid = static_cast<long>(getpid());
#endif
    return std::string(prefix) + std::to_string(pid) + "_" +
           std::to_string(stamp) + "_" + std::to_string(counter);
}

// Removes its directory on scope exit, including early returns.
struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / uniqueToken("cp3-toolchain-");
        fs::create_directories(path, ec);
        if (ec) path.clear();
    }
    ~TempDir() {
        if (!path.empty()) removeTree(path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

bool isClangBanner(const std::string& banner) {
    return lowerCopy(banner).find("clang") != std::string::npos;
}

int majorVersionOf(const std::string& version) {
    const std::string normalized = normalizeCompilerVersion(version);
    if (normalized.empty()) return -1;
    return std::atoi(normalized.c_str());
}

// Compiles, links and runs a one-line C++20 program.
bool validateProgram(const std::string& program, std::string& reason) {
    TempDir temp;
    if (temp.path.empty()) {
        reason = "could not create a temporary directory for the probe";
        return false;
    }
    const fs::path cpp = temp.path / "cp3_toolchain_probe.cpp";
    const fs::path exe = temp.path / "cp3_toolchain_probe.exe";
    const std::string source =
        "#include <cstdio>\n"
        "int main() { std::printf(\"cp3-toolchain-probe\\n\"); return 0; }\n";
    if (!writeTextFile(cpp, source, nullptr)) {
        reason = "could not write the probe source in " + temp.path.string();
        return false;
    }
    std::vector<std::string> args{program};
    // The same target flags the real build uses, so a Clang that probes clean
    // is a Clang that builds (detail/toolchain_target.cpp).
    for (const std::string& flag : toolchainTargetFlags(program)) {
        args.push_back(flag);
    }
    args.push_back("-std=c++20");
    args.push_back(cpp.string());
    args.push_back("-o");
    args.push_back(exe.string());
    std::string log;
    const int buildRc = runProcessCapture(args, log);
    if (buildRc != 0) {
        const std::string detail = firstLine(log);
        reason = "test compile failed";
        if (!detail.empty()) reason += ": " + detail;
        return false;
    }
    std::error_code exeEc;
    if (!fs::is_regular_file(exe, exeEc)) {
        reason = "test compile produced no executable";
        return false;
    }
    std::string runLog;
    const int runRc = runProcessCapture({exe.string()}, runLog);
    if (runRc != 0) {
        const std::string detail = firstLine(runLog);
        reason = "test program did not run";
        if (!detail.empty()) reason += ": " + detail;
        return false;
    }
    return true;
}

}  // namespace

std::vector<ToolchainCandidate> toolchainCandidates() {
    // Beta one supports exactly one target toolchain. Windows keeps the
    // MinGW-w64 GNU target; the candidate itself is the same Clang driver on
    // every platform.
    return {{"clang", "Clang", "clang++"}};
}

bool isClangId(std::string_view id) {
    const std::string lower = lowerCopy(std::string(id));
    return lower == "clang" || lower == "clang++" || lower == "apple-clang";
}

const ToolchainCandidate* findToolchainCandidate(const std::string& id) {
    static const std::vector<ToolchainCandidate> candidates =
        toolchainCandidates();
    if (!isClangId(id)) return nullptr;
    return &candidates.front();
}

CandidateStatus probeToolchainProgram(const ToolchainCandidate& candidate,
                                      const std::string& program,
                                      bool validate) {
    CandidateStatus status;
    status.candidate = candidate;
    status.program = program;
    std::error_code ec;
    if (program.empty() || !fs::is_regular_file(program, ec)) {
        status.problem = "the compiler program does not exist";
        return status;
    }
    status.found = true;
    // The validation result depends only on the compiler binary itself, so it
    // is cached across processes (see probe_cache.hpp): a swapped, patched or
    // rebuilt driver is detected by path + size + mtime and re-probed; every
    // other read of the cache is a miss and the live probe runs as before.
    if (validate) {
        ProbeCacheEntry cached;
        if (probeCacheLookup(program, cached)) {
            status.banner = cached.banner;
            status.version = cached.version;
            status.usable = true;
            return status;
        }
    }
    std::string versionOut;
    if (runProcessCapture({program, "--version"}, versionOut) < 0) {
        status.problem = "could not run " + program + " --version";
        return status;
    }
    status.banner = firstLine(versionOut);
    if (!isClangBanner(status.banner)) {
        status.problem =
            "not a Clang compiler; beta one supports Clang only";
        return status;
    }
    status.version = normalizeCompilerVersion(status.banner);
    if (status.version.empty()) {
        status.problem = "could not parse a version from " +
                         (status.banner.empty() ? program : status.banner);
        return status;
    }
    if (majorVersionOf(status.version) < minimumClangMajor()) {
        status.problem = "Clang " + status.version + " is older than the minimum supported Clang " +
                         std::to_string(minimumClangMajor());
        return status;
    }
    if (!validate) {
        status.usable = true;
        return status;
    }
    std::string reason;
    status.usable = validateProgram(status.program, reason);
    if (!status.usable) status.problem = reason;
    if (status.usable) {
        ProbeCacheEntry entry;
        entry.banner = status.banner;
        entry.version = status.version;
        probeCacheStore(program, entry);
    }
    return status;
}

CandidateStatus probeToolchainCandidate(const ToolchainCandidate& candidate,
                                        bool validate) {
    CandidateStatus status;
    status.candidate = candidate;
    const std::vector<ToolchainInstall> installs =
        discoverToolchainVersions(candidate, false);
    if (installs.empty()) {
        status.problem = "not installed (looked for " + candidate.exe +
                         " in PATH and standard system locations)";
        return status;
    }
    if (!validate) {
        return probeToolchainProgram(candidate, installs.front().program, false);
    }
    // Newest first. A newer install that cannot produce a binary must not hide
    // an older usable one: auto-detection picks the newest *usable* Clang.
    CandidateStatus firstProblem;
    for (const ToolchainInstall& install : installs) {
        CandidateStatus current =
            probeToolchainProgram(candidate, install.program, true);
        if (current.usable) return current;
        if (firstProblem.problem.empty()) firstProblem = std::move(current);
    }
    return firstProblem;
}

}  // namespace cp3::cli
