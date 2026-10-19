// =============================================================================
// src/cli/src/toolchain/util.cpp - platform/IO/string helper DEFINITIONS for cp3c.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/util.hpp.
//
// Standard C++20 only. No third-party libraries. The repository-relative
// include-root discovery is anchored on `__FILE__` so no build-time define is
// needed (this module's CMakeLists is the only build file it owns).
// =============================================================================
#include "detail/util.hpp"

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
// Hashing (FNV-1a 64) - used only for change detection, never for security.
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
// Cp3 root discovery and include-root resolution
// -----------------------------------------------------------------------------

namespace {

// Directory of the running executable (empty when it cannot be determined).
// The installer's layout is <root>/bin/cp3c.exe next to the shipped trees, so
// the executable is the one anchor that survives being copied to a user machine.
fs::path executableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
        if (len == 0) return {};
        if (len < buffer.size()) {
            buffer.resize(len);
            break;
        }
        buffer.resize(buffer.size() * 2);  // executable path longer than MAX_PATH
    }
    return fs::path(buffer).parent_path();
#else
    std::vector<char> buffer(4096, '\0');
    const ssize_t len =
        readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (len <= 0) return {};
    return fs::path(std::string(buffer.data(), static_cast<std::size_t>(len)))
        .parent_path();
#endif
}

// True when `dir` owns the trees every cp3 distribution ships: the runtime
// translation units plus the std families. A source checkout additionally has
// src/cli, which installed trees deliberately lack; requiring it would reject
// them. The nested and flat runtime layouts both count (runtimeSourcePaths
// accepts either).
bool ownsCp3Trees(const fs::path& dir) {
    std::error_code ec;
    const bool runtime = fs::is_directory(dir / "src" / "runtime" / "src", ec) ||
                         fs::is_directory(dir / "src" / "runtime", ec);
    return runtime && fs::is_directory(dir / "src" / "std", ec);
}

// Candidate roots in priority order:
//   1. CP3_ROOT, set by the cp3.cmd shim - a shim on PATH can point at a
//      distribution whose bin/ is not the shim's own directory;
//   2. the running executable's ancestors - an installed tree resolves as
//      <root>/bin/cp3c.exe at depth 1;
//   3. this file's compile-time path - an in-checkout compiler.
fs::path findCp3Root() {
    if (const char* env = std::getenv("CP3_ROOT")) {
        const fs::path fromEnv = stripSurroundingQuotes(env);
        if (!fromEnv.empty() && ownsCp3Trees(fromEnv)) return fromEnv;
    }
    fs::path probe = executableDirectory();
    for (int depth = 0; depth < 4 && !probe.empty(); ++depth) {
        if (ownsCp3Trees(probe)) return probe;
        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    probe = fs::path(__FILE__).parent_path();
    while (!probe.empty()) {
        if (ownsCp3Trees(probe)) return probe;
        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    return {};
}

}  // namespace

// Public to the cli module because the native-backend rows
// (detail/native_packages.hpp) resolve third_party/** against it.
fs::path repositoryRoot() { return findCp3Root(); }

fs::path resolveIncludeRoot(const fs::path& root, const std::string& rel) {
    std::error_code ec;
    const fs::path direct = root / rel;
    if (fs::is_directory(direct, ec)) return direct;
    // "src/<x>/include" -> "<root>/include/<x>": the installer stages every
    // shipped header tree into one include directory (PKG-CONTRACTS section 4)
    // while runtime sources stay under src/**.
    const std::string prefix = "src/";
    const std::string suffix = "/include";
    if (rel.rfind(prefix, 0) == 0 && rel.size() > prefix.size() + suffix.size() &&
        rel.compare(rel.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const fs::path mapped =
            root / "include" /
            rel.substr(prefix.size(), rel.size() - prefix.size() - suffix.size());
        if (fs::is_directory(mapped, ec)) return mapped;
    }
    return direct;
}

std::vector<fs::path> compilerIncludeRoots() {
    std::vector<fs::path> roots;
    const fs::path repo = repositoryRoot();
    // D72: the always-available std families' ABI trees (string methods and
    // typed arrays need no import); every other family's tree is added by its
    // native row when the program imports it (native_packages.cpp).
    for (const char* rel : {"src/runtime/include", "src/common/include",
                            "src/std/string/include",
                            "src/std/typed-arrays/include"}) {
        const fs::path candidate = resolveIncludeRoot(repo, rel);
        std::error_code ec;
        if (fs::is_directory(candidate, ec)) roots.push_back(candidate);
    }
    return roots;
}

std::vector<fs::path> runtimeSourcePaths() {
    const fs::path repo = repositoryRoot();

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

    // What the tree actually contains, so discovery tracks the layout. Since
    // the structure sweep the sources live in per-family subdirectories
    // (core/, async/, parallel/), so the walk is recursive; the legacy flat
    // directory still works.
    std::vector<fs::path> present;
    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() == ".cpp") {
            present.push_back(entry.path());
        }
    }

    // Keep only the required, documented set, in a stable order.
    //
    //   runtime.cpp   - toString / print / println / Undefined (cp3/core/runtime.hpp);
    //                   M1 `console.log` lowers onto `println`.
    //   error.cpp     - `Thrown` (cp3/core/error.hpp): `throw` boxing and the
    //                   unhandled-rejection report.
    //   eventloop.cpp - the single-threaded loop + the timer builtins
    //                   (cp3/async/eventloop.hpp): `setTimeout`/`setInterval`/... and
    //                   the `runEventLoop()` every generated `main()` ends with
    //                   when the unit uses async (§14).
    //   promise.cpp   - `Promise<T>` + `await`/`sleep` (cp3/async/promise.hpp).
    //
    // Since D72 `console.cpp` is the console family's runtime TU
    // (src/std/console/runtime.cpp), selected by the console native row when a
    // program imports "std/console" (native_packages.cpp), so it is not
    // discovered here.
    //
    // parallel.cpp is the `parallel.*` / `worker(...)` engine (spec 13);
    // worker.hpp is header-only, so it contributes no TU.
    static const char* const kRequired[] = {"runtime.cpp", "error.cpp",
                                            "eventloop.cpp", "promise.cpp",
                                            "parallel.cpp"};
    std::vector<fs::path> sources;
    for (const char* name : kRequired) {
        for (const fs::path& file : present) {
            if (file.filename() == name) {
                sources.push_back(file);
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
