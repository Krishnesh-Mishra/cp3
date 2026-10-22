// =============================================================================
// src/cli/src/native/sdk_libs.cpp - library discovery and process capture.
//
// OWNERSHIP: cli module (native-install). Part of native/sdk.hpp.
// =============================================================================
#include "native/sdk_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <system_error>

#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "detail/util.hpp"

namespace cp3::cli::native {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isDirectory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

fs::path absolutePath(const fs::path& path) {
    std::error_code ec;
    const fs::path full = fs::absolute(path, ec);
    return (ec ? path : full).lexically_normal();
}

void addUnique(std::vector<std::string>& out, const std::string& value) {
    if (value.empty()) return;
    if (std::find(out.begin(), out.end(), value) == out.end()) out.push_back(value);
}

#if !defined(_WIN32)
std::string quoteArg(const std::string& arg) {
    std::string out = "\"";
    for (const char c : arg) {
        if (c == '"') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string joinCommand(const std::vector<std::string>& args) {
    std::string command;
    for (const std::string& arg : args) {
        if (!command.empty()) command += ' ';
        command += quoteArg(arg);
    }
    return command;
}
#endif

struct LibraryFiles {
    std::string importPath;
    std::string staticPath;
    std::string sharedPath;
    std::string runtimePath;
};

bool isSharedSo(const std::string& name) {
    return name.find(".so") != std::string::npos;
}

bool isImportLib(const std::string& name) {
    return name.size() > 6 && name.rfind(".dll.a") == name.size() - 6;
}

void classifyFile(const std::string& name, LibraryFiles& files) {
    if (name.empty()) return;
    if (name.size() > 4 && name.rfind(".dll") == name.size() - 4) {
        if (files.runtimePath.empty()) files.runtimePath = name;
        if (files.sharedPath.empty()) files.sharedPath = name;
        return;
    }
    if (name.size() > 4 && name.rfind(".lib") == name.size() - 4) {
        if (files.staticPath.empty()) files.staticPath = name;
        return;
    }
    if (isImportLib(name)) {
        if (files.importPath.empty()) files.importPath = name;
        return;
    }
    if (name.size() > 2 && name.rfind(".a", name.size() - 2) != std::string::npos) {
        if (files.staticPath.empty()) files.staticPath = name;
        return;
    }
    if (name.size() > 6 && name.rfind(".dylib") == name.size() - 6) {
        if (files.sharedPath.empty()) files.sharedPath = name;
        return;
    }
    if (isSharedSo(name)) {
        if (files.sharedPath.empty()) files.sharedPath = name;
    }
}

bool fileExists(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

// Absolute path of `dir/<name>` when it exists.
bool found(const fs::path& dir, const std::string& name, std::string& out) {
    if (name.empty()) return false;
    const fs::path path = dir / name;
    if (!fileExists(path)) return false;
    out = absolutePath(path).string();
    return true;
}

void searchNameInDir(const fs::path& dir, const std::string& base,
                     LibraryFiles& files) {
    const std::vector<std::string> candidates = {
        "lib" + base + ".a",      base + ".lib",      "lib" + base + ".dll.a",
        base + ".dll",            "lib" + base + ".dll", "lib" + base + ".so",
        base + ".so",             "lib" + base + ".dylib", base + ".dylib",
    };
    for (const std::string& candidate : candidates) {
        std::string path;
        if (!found(dir, candidate, path)) continue;
        classifyFile(candidate, files);
        if (candidate == "lib" + base + ".a" ||
            candidate == base + ".lib") {
            files.staticPath = path;
        } else if (candidate == "lib" + base + ".dll.a") {
            files.importPath = path;
        } else if (candidate.size() > 4 &&
                   candidate.rfind(".dll") == candidate.size() - 4) {
            files.runtimePath = path;
            files.sharedPath = path;
        } else if (candidate.find(".so") != std::string::npos ||
                   candidate.find(".dylib") != std::string::npos) {
            files.sharedPath = path;
        }
    }
    // Versioned shared objects: libfoo.so.1.
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("lib" + base + ".so.", 0) != 0) continue;
        files.sharedPath = absolutePath(it->path()).string();
        break;
    }
}

std::string baseName(std::string name) {
    const std::string lowered = lower(name);
    if (lowered.rfind("lib", 0) == 0) name = name.substr(3);
    const std::size_t dot = name.find('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

std::vector<std::string> librarySearchDirs(const SdkRequest& request,
                                           const SdkResolution& out) {
    std::vector<std::string> dirs;
    for (const fs::path& dir : request.extraLibDirs) {
        addUnique(dirs, absolutePath(dir).string());
    }
    for (const char* rel : {"lib", "lib64", "bin"}) {
        const fs::path dir = out.root / rel;
        if (isDirectory(dir)) addUnique(dirs, absolutePath(dir).string());
    }
    if (isDirectory(out.root)) addUnique(dirs, absolutePath(out.root).string());
    return dirs;
}

// Header stems are the best name hint when the user did not pass --link.
std::set<std::string> headerStems(const SdkResolution& out) {
    std::set<std::string> stems;
    for (const std::string& header : out.headers) {
        stems.insert(lower(fs::path(header).stem().string()));
    }
    return stems;
}

void appendFound(const std::string& base, const LibraryFiles& files,
                 SdkResolution& out) {
    addUnique(out.linkNames, base);
    addUnique(out.staticLibraries, files.staticPath);
    addUnique(out.sharedLibraries, files.sharedPath);
    if (files.importPath.empty()) {
        addUnique(out.sharedLibraries, files.sharedPath);
    } else {
        addUnique(out.sharedLibraries, files.importPath);
    }
#if defined(_WIN32)
    addUnique(out.runtimeDlls, files.runtimePath);
#else
    (void)files;
#endif
}

bool resolveLinks(const SdkRequest& request, SdkResolution& out,
                  const std::vector<std::string>& dirs, std::string& error) {
    for (const std::string& link : request.links) {
        const fs::path asPath(link);
        std::string base = link;
        LibraryFiles files;
        std::error_code ec;
        if (fs::is_regular_file(asPath, ec)) {
            const std::string path = absolutePath(asPath).string();
            base = baseName(asPath.filename().string());
            classifyFile(asPath.filename().string(), files);
            if (!files.staticPath.empty()) files.staticPath = path;
            if (!files.importPath.empty()) files.importPath = path;
            if (!files.sharedPath.empty()) files.sharedPath = path;
            if (!files.runtimePath.empty()) files.runtimePath = path;
        } else {
            base = baseName(link);
            for (const std::string& dir : dirs) {
                searchNameInDir(dir, base, files);
            }
        }
        if (files.staticPath.empty() && files.sharedPath.empty() &&
            files.importPath.empty() && files.runtimePath.empty()) {
            error = "library '" + link + "' was not found";
            error += "\nSearched:";
            for (const std::string& dir : dirs) error += "\n  " + dir;
            error += "\nPass --lib-dir <dir> or check that the SDK was built.";
            return false;
        }
        appendFound(base, files, out);
    }
    return true;
}

void autoDetect(const SdkResolution& out, const std::vector<std::string>& dirs,
                SdkResolution& result) {
    const std::set<std::string> stems = headerStems(out);
    for (const std::string& dir : dirs) {
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string name = it->path().filename().string();
            const std::string ext = lower(it->path().extension().string());
            if (ext != ".a" && ext != ".lib" && ext != ".dll" &&
                ext != ".so" && ext != ".dylib") {
                continue;
            }
            if (isImportLib(name)) continue;
            const std::string stem = lower(baseName(name));
            if (stems.count(stem) == 0) continue;
            LibraryFiles files;
            searchNameInDir(dir, stem, files);
            appendFound(stem, files, result);
        }
    }
}

}  // namespace

#if defined(_WIN32)
namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     text.data(), size, nullptr, 0);
    UINT page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (length <= 0) {
        page = CP_ACP;
        flags = 0;
        length = MultiByteToWideChar(page, flags, text.data(), size, nullptr, 0);
    }
    if (length <= 0) return {};
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(page, flags, text.data(), size, out.data(), length);
    return out;
}

// Quotes one argument using the CommandLineToArgvW/CreateProcess rules.
std::wstring quoteWindowsArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0; i < arg.size(); ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

bool captureWindows(const std::vector<std::string>& args, std::string& output,
                    int& code) {
    std::wstring commandLine;
    for (const std::string& arg : args) {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += quoteWindowsArg(widen(arg));
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return false;
    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempFileNameW(tempDir, L"cp3", 0, tempPath) == 0) return false;

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE file = CreateFileW(tempPath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempPath);
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = file;
    startup.hStdError = file;
    PROCESS_INFORMATION process{};
    const BOOL started =
        CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!started) {
        CloseHandle(file);
        DeleteFileW(tempPath);
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD status = 1;
    GetExitCodeProcess(process.hProcess, &status);
    code = static_cast<int>(status);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(file);

    FILE* capture = _wfopen(tempPath, L"rb");
    if (capture != nullptr) {
        char buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), capture)) > 0) {
            output.append(buffer, read);
        }
        std::fclose(capture);
    }
    DeleteFileW(tempPath);
    return true;
}

}  // namespace
#endif

bool runCapture(const std::vector<std::string>& args, std::string& output,
                int& code) {
    if (args.empty()) return false;
#if defined(_WIN32)
    return captureWindows(args, output, code);
#else
    std::string command = joinCommand(args) + " 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return false;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int status = pclose(pipe);
    code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
#endif
}

bool detectLibraries(const SdkRequest& request, SdkResolution& out,
                     std::string& error) {
    const std::vector<std::string> dirs = librarySearchDirs(request, out);
    for (const std::string& dir : dirs) addUnique(out.libDirs, dir);
    if (!request.links.empty()) {
        return resolveLinks(request, out, dirs, error);
    }
    autoDetect(out, dirs, out);
    return true;
}

}  // namespace cp3::cli::native
