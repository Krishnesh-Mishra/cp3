// =============================================================================
// src/cli/src/native/libclang.cpp - libclang discovery and dynamic loading.
//
// OWNERSHIP: cli module (native-install). Implements native/libclang.hpp.
// =============================================================================
#include "native/libclang/libclang.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "detail/core/util.hpp"

namespace cp3::cli::native {

namespace fs = std::filesystem;

namespace {

std::string envValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

void addCandidate(std::vector<std::string>& out, const std::string& value) {
    if (value.empty()) return;
    for (const std::string& existing : out) {
        if (existing == value) return;
    }
    out.push_back(value);
}

#if defined(_WIN32)
// Every directory on PATH, in order, looking for libclang.dll.
void addPathCandidates(std::vector<std::string>& out) {
    const std::string path = envValue("PATH");
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(';', start);
        const std::string dir =
            path.substr(start, end == std::string::npos ? std::string::npos
                                                        : end - start);
        if (!dir.empty()) addCandidate(out, (fs::path(dir) / "libclang.dll").string());
        if (end == std::string::npos) break;
        start = end + 1;
    }
}
#else
void addGlobCandidates(std::vector<std::string>& out, const fs::path& dir,
                       const std::string& prefix) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(prefix, 0) == 0) addCandidate(out, it->path().string());
    }
}
#endif

std::vector<std::string> candidatePaths() {
    std::vector<std::string> out;
    // An explicit override is authoritative so tests (and users with a
    // non-standard installation) get a predictable library or a clear error
    // instead of silently picking up a different system copy.
    const std::string overridePath = envValue("CP3_LIBCLANG");
    if (!overridePath.empty()) {
        out.push_back(overridePath);
        return out;
    }
#if defined(_WIN32)
    const std::string programFiles = envValue("ProgramFiles");
    const std::string programFilesX86 = envValue("ProgramFiles(x86)");
    if (!programFiles.empty()) {
        addCandidate(out, (fs::path(programFiles) / "LLVM" / "bin" / "libclang.dll").string());
    }
    if (!programFilesX86.empty()) {
        addCandidate(out, (fs::path(programFilesX86) / "LLVM" / "bin" / "libclang.dll").string());
    }
    addCandidate(out, "C:\\Program Files\\LLVM\\bin\\libclang.dll");
    addPathCandidates(out);
#elif defined(__APPLE__)
    addCandidate(out, "/opt/homebrew/opt/llvm/lib/libclang.dylib");
    addCandidate(out, "/usr/local/opt/llvm/lib/libclang.dylib");
    addCandidate(out, "/Library/Developer/CommandLineTools/usr/lib/libclang.dylib");
    addCandidate(out, "libclang.dylib");
#else
    addCandidate(out, "libclang.so");
    addCandidate(out, "libclang.so.1");
    addCandidate(out, "/usr/lib/llvm/lib/libclang.so");
    addCandidate(out, "/usr/lib/x86_64-linux-gnu/libclang.so.1");
    addCandidate(out, "/usr/local/lib/libclang.so");
    addGlobCandidates(out, "/usr/lib", "llvm-");
    std::error_code ec;
    for (fs::directory_iterator it("/usr/lib", ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("llvm-", 0) != 0) continue;
        addCandidate(out, (it->path() / "lib" / "libclang.so").string());
    }
#endif
    return out;
}

void* openLibrary(const std::string& path, std::string& error) {
#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path.c_str());
    if (handle == nullptr) {
        error = "cannot load '" + path + "' (Windows error " +
                std::to_string(GetLastError()) + ")";
    }
    return reinterpret_cast<void*>(handle);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* reason = dlerror();
        error = "cannot load '" + path + "': " +
                (reason == nullptr ? std::string("unknown dlopen error") : reason);
    }
    return handle;
#endif
}

void* findSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

}  // namespace

std::string libclangInstallHint() {
#if defined(_WIN32)
    return "winget install --id LLVM.LLVM -e";
#elif defined(__APPLE__)
    return "brew install llvm";
#elif defined(__linux__)
    // Both families are shown because one command cannot cover every distro.
    return "apt install libclang-dev  (Debian/Ubuntu) or "
           "dnf install clang-devel  (Fedora)";
#else
    return "install the libclang development package for your platform";
#endif
}

LibClang LibClang::load(std::string& error) {
    LibClang lib;
    lib.searched = candidatePaths();

    // CP3_LIBCLANG may name a library that is not on a standard path; it is
    // tried first, but a broken override does not stop the normal search.
    std::string lastError;
    for (const std::string& candidate : lib.searched) {
        if (!fileExists(candidate)) {
            // A bare soname ("libclang.so") is resolved through the loader's
            // own search path, so it gets an attempt even without a file check.
            const bool bare = candidate.find('/') == std::string::npos &&
                              candidate.find('\\') == std::string::npos;
            if (!bare) continue;
        }
        std::string openError;
        void* handle = openLibrary(candidate, openError);
        if (handle == nullptr) {
            lastError = openError;
            continue;
        }
        bool complete = true;
        std::string missing;
#define CP3_LOAD_FN(name)                                              \
    lib.name = reinterpret_cast<decltype(lib.name)>(                   \
        findSymbol(handle, #name));                                    \
    if (lib.name == nullptr) {                                         \
        complete = false;                                              \
        if (missing.empty()) missing = #name;                          \
    }
        CP3_LIBCLANG_FUNCTIONS(CP3_LOAD_FN)
#undef CP3_LOAD_FN
        if (!complete) {
            lastError = "'" + candidate + "' is missing symbol " + missing;
            continue;
        }
        lib.loaded = true;
        lib.handle = handle;
        lib.path = candidate;
        CXString version = lib.clang_getClangVersion();
        const char* text = lib.clang_getCString(version);
        lib.version = text == nullptr ? std::string() : std::string(text);
        lib.clang_disposeString(version);
        return lib;
    }

    error = "libclang was not found (it is required to parse C++ headers).\n";
    error += "Install LLVM system-wide: " + libclangInstallHint() + "\n";
    error += "Or set CP3_LIBCLANG to an existing libclang library.\n";
    error += "Searched:";
    for (const std::string& candidate : lib.searched) {
        error += "\n  " + candidate;
    }
    if (!lastError.empty()) error += "\nLast loader error: " + lastError;
    return LibClang{};
}

}  // namespace cp3::cli::native
