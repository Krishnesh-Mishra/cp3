// =============================================================================
// cli/util.cpp — platform/IO/string helper DEFINITIONS for cp3c.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/util.hpp.
//
// Standard C++20 only. No third-party libraries. The repository-relative
// include-root discovery is anchored on `__FILE__` so no build-time define is
// needed (this module's CMakeLists is the only build file it owns).
// =============================================================================
#include "private/util.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <io.h>
#  include <process.h>
#  include <windows.h>
#else
#  include <cerrno>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace cp3::cli {

// -----------------------------------------------------------------------------
// File IO
// -----------------------------------------------------------------------------

bool readTextFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    out = std::move(data);
    return true;
}

bool writeTextFile(const fs::path& path, std::string_view content,
                   bool* changed) {
    std::error_code ec;
    if (changed != nullptr) {
        *changed = false;
        std::string existing;
        if (readTextFile(path, existing) && existing.size() == content.size() &&
            std::memcmp(existing.data(), content.data(), content.size()) == 0) {
            return true;
        }
        *changed = true;
    } else {
        // Still detect a no-op write cheaply to avoid touching mtimes.
        std::string existing;
        if (readTextFile(path, existing) && existing.size() == content.size() &&
            std::memcmp(existing.data(), content.data(), content.size()) == 0) {
            return true;
        }
    }

    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(ofs);
}

// -----------------------------------------------------------------------------
// Hashing (FNV-1a 64) — used only for change detection, never for security.
// -----------------------------------------------------------------------------

std::uint64_t fnv1a64(std::string_view data) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : data) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

// -----------------------------------------------------------------------------
// Executable discovery / process execution
// -----------------------------------------------------------------------------

namespace {

#if defined(_WIN32)
constexpr char kPathSep = ';';
constexpr const char* const kExecExtensions[] = {".exe", ".cmd", ".bat", ""};
#else
constexpr char kPathSep = ':';
constexpr const char* const kExecExtensions[] = {""};
#endif

// Tries every platform extension for `name` inside one PATH directory.
bool findInDirectory(const std::string& dir, const std::string& name,
                     std::string& found) {
    for (const char* ext : kExecExtensions) {
        const fs::path candidate = fs::path(dir) / (name + ext);
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            found = candidate.string();
            return true;
        }
    }
    return false;
}

}  // namespace

std::string stripSurroundingQuotes(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool findExecutable(const std::string& name, std::string& found) {
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) return false;

    const std::string path(pathEnv);
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(kPathSep, start);
        const std::size_t len =
            (end == std::string::npos) ? std::string::npos : end - start;
        const std::string dir = stripSurroundingQuotes(path.substr(start, len));
        if (!dir.empty() && findInDirectory(dir, name, found)) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

int runProcess(const std::vector<std::string>& args) {
    if (args.empty()) return -1;

#if defined(_WIN32)
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    const intptr_t rc = _spawnvp(_P_WAIT, argv[0], argv.data());
    return rc < 0 ? -1 : static_cast<int>(rc);
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

std::string describeCommand(const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out.push_back(' ');
        const bool needsQuote = args[i].find(' ') != std::string::npos;
        if (needsQuote) out.push_back('"');
        out += args[i];
        if (needsQuote) out.push_back('"');
    }
    return out;
}

// -----------------------------------------------------------------------------
// Terminal / memory probing
// -----------------------------------------------------------------------------

bool isTty(std::FILE* f) noexcept {
#if defined(_WIN32)
    return _isatty(_fileno(f)) != 0;
#else
    return isatty(fileno(f)) != 0;
#endif
}

std::uint64_t totalPhysicalMemoryMb() noexcept {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<std::uint64_t>(status.ullTotalPhys /
                                          (1024ULL * 1024ULL));
    }
    return 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) {
        return static_cast<std::uint64_t>(pages) *
               static_cast<std::uint64_t>(pageSize) / (1024ULL * 1024ULL);
    }
    return 0;
#endif
}

// -----------------------------------------------------------------------------
// Repository-relative include roots
// -----------------------------------------------------------------------------

namespace {

// Repo root discovered from this translation unit's compile-time path
// (.../src/cli/util.cpp): walk up until the directory that owns both src/cli
// and src/runtime. This mirrors the historical `__FILE__`-relative computation
// without depending on this file's exact depth under src/.
fs::path repoRootFromHere() {
    std::error_code ec;
    fs::path probe = fs::path(__FILE__).parent_path();
    while (!probe.empty()) {
        if (fs::is_directory(probe / "src" / "cli", ec) &&
            fs::is_directory(probe / "src" / "runtime", ec)) {
            return probe;
        }
        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    return {};
}

}  // namespace

std::vector<fs::path> compilerIncludeRoots() {
    std::vector<fs::path> roots;
    const fs::path repo = repoRootFromHere();
    for (const char* rel : {"src/runtime/include", "src/common/include"}) {
        const fs::path candidate = repo / rel;
        std::error_code ec;
        if (fs::is_directory(candidate, ec)) roots.push_back(candidate);
    }
    return roots;
}

std::vector<fs::path> runtimeSourcePaths() {
    const fs::path repo = repoRootFromHere();

    // Prefer the refactored layout; fall back to the legacy flat directory.
    std::error_code ec;
    fs::path dir;
    for (const char* rel : {"src/runtime/src", "src/runtime"}) {
        const fs::path candidate = repo / rel;
        if (fs::is_directory(candidate, ec)) {
            dir = candidate;
            break;
        }
    }
    if (dir.empty()) return {};

    // What the directory actually contains, so discovery tracks the layout.
    std::vector<std::string> present;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() == ".cpp") {
            present.push_back(entry.path().filename().string());
        }
    }

    // Keep only the required, documented set, in a stable order.
    //
    //   runtime.cpp — toString / print / println / Undefined (cp3/runtime.hpp).
    //                 M1 `console.log` lowers onto `println`, so no
    //                 `cp3::runtime::console::*` symbol is emitted yet and
    //                 console.cpp is deliberately not compiled. Sibling TUs such
    //                 as promise.cpp / parallel.cpp / eventloop.cpp require
    //                 flags (e.g. threads) the driver does not pass, so pulling
    //                 them in unconditionally would break the link.
    static const char* const kRequired[] = {"runtime.cpp"};
    std::vector<fs::path> sources;
    for (const char* name : kRequired) {
        for (const std::string& file : present) {
            if (file == name) {
                sources.push_back(dir / file);
                break;
            }
        }
    }
    return sources;
}

// -----------------------------------------------------------------------------
// Misc
// -----------------------------------------------------------------------------

bool endsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool removeTree(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    return !fs::exists(dir, ec);
}

}  // namespace cp3::cli
