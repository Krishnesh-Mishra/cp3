// =============================================================================
// src/cli/src/native/sdk.cpp - SDK path/name resolution and header discovery.
//
// OWNERSHIP: cli module (native-install). Implements native/sdk.hpp.
// Library discovery and process capture live in sdk_libs.cpp.
// =============================================================================
#include "native/sdk/sdk.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

#include "detail/core/util.hpp"
#include "detail/toolchain/toolchain.hpp"
#include "detail/toolchain/toolchain_select.hpp"
#include "native/sdk/sdk_internal.hpp"

namespace cp3::cli::native {

namespace {

std::string envValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

bool isDirectory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool isRegularFile(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

fs::path absolutePath(const fs::path& path) {
    std::error_code ec;
    const fs::path full = fs::absolute(path, ec);
    return (ec ? path : full).lexically_normal();
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Header stems and SDK directory names are compared ignoring case and the
// `-`/`_` spelling difference (`static-sdk` binds `staticsdk.h`).
std::string normalizedName(const std::string& text) {
    std::string out = lower(text);
    out.erase(std::remove(out.begin(), out.end(), '-'), out.end());
    out.erase(std::remove(out.begin(), out.end(), '_'), out.end());
    return out;
}

bool isHeader(const fs::path& path) {
    const std::string ext = lower(path.extension().string());
    return ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" ||
           ext == ".h++";
}

void addUnique(std::vector<std::string>& out, const std::string& value) {
    if (value.empty()) return;
    if (std::find(out.begin(), out.end(), value) == out.end()) {
        out.push_back(value);
    }
}

void addHeader(std::vector<std::string>& out, const fs::path& path) {
    const std::string text = absolutePath(path).string();
    if (std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
}

void collectHeaders(const fs::path& root, std::vector<std::string>& out) {
    std::error_code ec;
    if (!isDirectory(root)) return;
    for (fs::recursive_directory_iterator it(root, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !isHeader(it->path())) continue;
        addHeader(out, it->path());
        if (out.size() >= 2000) return;
    }
}

// Headers directly below `root` whose stem matches `name` (case-insensitive).
void collectNamedHeaders(const fs::path& root, const std::string& name,
                         std::vector<std::string>& out) {
    std::error_code ec;
    if (!isDirectory(root)) return;
    const std::string wanted = normalizedName(name);
    for (fs::recursive_directory_iterator it(root, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !isHeader(it->path())) continue;
        const std::string stem = normalizedName(it->path().stem().string());
        if (stem == wanted || stem.rfind(wanted, 0) == 0) {
            addHeader(out, it->path());
        }
        if (out.size() >= 256) return;
    }
}

void splitPathList(const std::string& text, std::vector<std::string>& out) {
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(separator, start);
        const std::string part =
            text.substr(start, end == std::string::npos ? std::string::npos
                                                        : end - start);
        if (!part.empty()) addUnique(out, part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

std::string joinPaths(const std::vector<std::string>& paths) {
    std::string out;
    for (const std::string& path : paths) {
        if (out.empty()) {
            out = path;
        } else {
            out += ", " + path;
        }
    }
    return out.empty() ? "(none)" : out;
}

std::string installHint() {
    return "Install the SDK with your platform package manager, set "
           "CP3_NATIVE_SDK_PATH to the directory that contains it, or pass the "
           "SDK directory to `cp3 native-install`.";
}

bool configureIncludeRoot(const fs::path& candidate, SdkResolution& out,
                          std::string& error, bool named,
                          const std::string& name) {
    if (!isDirectory(candidate)) return false;
    fs::path includeDir = candidate;
    const fs::path nested = candidate / "include";
    if (isDirectory(nested)) includeDir = nested;

    std::vector<std::string> headers;
    const fs::path namedDir = includeDir / name;
    if (named && isDirectory(namedDir)) {
        collectHeaders(namedDir, headers);
    } else if (named) {
        collectNamedHeaders(includeDir, name, headers);
    } else {
        collectHeaders(includeDir, headers);
    }
    if (headers.empty()) {
        error = "no C or C++ headers found under '" + includeDir.string() + "'";
        return false;
    }
    out.root = absolutePath(candidate);
    out.includeDir = absolutePath(includeDir);
    out.source = out.root.string();
    out.includeDirs.push_back(out.includeDir.string());
    out.publicRoots.push_back(out.includeDir.string());
    out.headers = std::move(headers);
    return true;
}

bool configureInputFile(const fs::path& file, SdkResolution& out,
                        std::string& error) {
    if (!isRegularFile(file) || !isHeader(file)) return false;
    out.root = absolutePath(file.parent_path());
    out.includeDir = out.root;
    out.source = absolutePath(file).string();
    out.includeDirs.push_back(out.root.string());
    out.publicRoots.push_back(out.root.string());
    out.headers.push_back(absolutePath(file).string());
    (void)error;
    return true;
}

void parsePkgConfigFlags(const std::string& flags, SdkResolution& out) {
    std::size_t i = 0;
    while (i < flags.size()) {
        while (i < flags.size() && std::isspace(static_cast<unsigned char>(flags[i]))) {
            ++i;
        }
        if (i >= flags.size()) break;
        std::size_t end = i;
        while (end < flags.size() &&
               !std::isspace(static_cast<unsigned char>(flags[end]))) {
            ++end;
        }
        const std::string token = flags.substr(i, end - i);
        i = end;
        if (token.size() > 2 && token.rfind("-I", 0) == 0) {
            addUnique(out.includeDirs, absolutePath(token.substr(2)).string());
        } else if (token.size() > 2 && token.rfind("-L", 0) == 0) {
            addUnique(out.libDirs, absolutePath(token.substr(2)).string());
        } else if (token.size() > 2 && token.rfind("-l", 0) == 0) {
            addUnique(out.linkNames, token.substr(2));
        }
    }
}

bool resolvePkgConfig(const std::string& name, SdkResolution& out) {
    std::string pkgConfig;
    if (!findExecutable("pkg-config", pkgConfig)) return false;
    std::string flags;
    int code = -1;
    if (!runCapture({pkgConfig, "--cflags", "--libs", name}, flags, code) ||
        code != 0) {
        return false;
    }
    parsePkgConfigFlags(flags, out);
    if (out.includeDirs.empty()) {
        std::string includedir;
        if (runCapture({pkgConfig, "--variable=includedir", name}, includedir,
                       code) &&
            code == 0) {
            while (!includedir.empty() &&
                   std::isspace(static_cast<unsigned char>(includedir.back()))) {
                includedir.pop_back();
            }
            if (isDirectory(includedir)) {
                out.includeDirs.push_back(absolutePath(includedir).string());
            }
        }
    }
    if (out.includeDirs.empty()) return false;

    std::vector<std::string> headers;
    for (const std::string& dir : out.includeDirs) {
        const fs::path includeDir(dir);
        const fs::path named = includeDir / name;
        if (isDirectory(named)) {
            collectHeaders(named, headers);
        } else {
            collectNamedHeaders(includeDir, name, headers);
        }
        if (!headers.empty()) {
            out.includeDir = includeDir;
            break;
        }
    }
    if (headers.empty()) return false;
    out.root = out.includeDir;
    out.headers = std::move(headers);
    out.source = "pkg-config:" + name;
    return true;
}

void addVcpkgRoots(const fs::path& root, const std::string& name,
                   std::vector<std::string>& searched) {
    const fs::path installed = root / "installed";
    std::error_code ec;
    if (!isDirectory(installed)) {
        addUnique(searched, installed.string());
        return;
    }
    for (fs::directory_iterator it(installed, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        addUnique(searched, (it->path() / "include").string());
    }
    (void)name;
}

bool resolveVcpkg(const std::string& name, SdkResolution& out,
                  std::vector<std::string>& searched) {
    std::vector<fs::path> roots;
    const std::string root = envValue("VCPKG_ROOT");
    const std::string installationRoot = envValue("VCPKG_INSTALLATION_ROOT");
    if (!root.empty()) roots.push_back(root);
    if (!installationRoot.empty()) roots.push_back(installationRoot);
    if (roots.empty()) {
        addUnique(searched, "VCPKG_ROOT (not set)");
        return false;
    }

    for (const fs::path& vcpkgRoot : roots) {
        addVcpkgRoots(vcpkgRoot, name, searched);
        const fs::path installed = vcpkgRoot / "installed";
        std::error_code ec;
        if (!isDirectory(installed)) continue;
        for (fs::directory_iterator it(installed, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const fs::path includeDir = it->path() / "include";
            std::vector<std::string> headers;
            const fs::path namedDir = includeDir / name;
            if (isDirectory(namedDir)) {
                collectHeaders(namedDir, headers);
            } else {
                collectNamedHeaders(includeDir, name, headers);
            }
            if (headers.empty()) continue;
            out.root = absolutePath(vcpkgRoot);
            out.includeDir = absolutePath(includeDir);
            out.includeDirs.push_back(out.includeDir.string());
            out.publicRoots.push_back(out.includeDir.string());
            out.headers = std::move(headers);
            const fs::path lib = it->path() / "lib";
            const fs::path bin = it->path() / "bin";
            if (isDirectory(lib)) out.libDirs.push_back(absolutePath(lib).string());
            if (isDirectory(bin)) out.libDirs.push_back(absolutePath(bin).string());
            out.source = "vcpkg:" + name;
            return true;
        }
    }
    return false;
}

bool resolveName(const std::string& name, SdkResolution& out,
                 std::string& error) {
    std::vector<std::string> roots;
    splitPathList(envValue("CP3_NATIVE_SDK_PATH"), roots);
    if (roots.empty()) {
        out.searched.push_back("CP3_NATIVE_SDK_PATH (not set)");
    }
    for (const std::string& root : roots) {
        out.searched.push_back(root);
        const fs::path base(root);
        if (normalizedName(base.filename().string()) == normalizedName(name) &&
            configureIncludeRoot(base, out, error, true, name)) {
            out.source = "CP3_NATIVE_SDK_PATH:" + root;
            return true;
        }
        const fs::path candidate = base / name;
        out.searched.push_back(candidate.string());
        if (configureIncludeRoot(candidate, out, error, true, name)) {
            out.source = "CP3_NATIVE_SDK_PATH:" + candidate.string();
            return true;
        }
    }

    if (resolvePkgConfig(name, out)) return true;

    if (resolveVcpkg(name, out, out.searched)) return true;

    std::string message = "native SDK '" + name + "' was not found.";
    message += "\n" + installHint();
    message += "\nSearched: " + joinPaths(out.searched);
    message += "\nAlso tried pkg-config and vcpkg (VCPKG_ROOT).";
    error = message;
    return false;
}

}  // namespace

std::string deriveAbi(const std::string& target) {
    const std::string text = lower(target);
    if (text.find("mingw") != std::string::npos) return "mingw";
    if (!text.empty()) return "itanium";
    return "c++";
}

std::vector<std::string> clangParseArgs() {
    std::vector<std::string> args;
    const Toolchain& tc = detectToolchain();
    std::string clang = tc.found() ? tc.program : std::string();
    if (clang.empty() && !findExecutable("clang++", clang)) return args;
    std::string triple;
    int code = -1;
    if (runCapture({clang, "-print-target-triple"}, triple, code) && code == 0) {
        while (!triple.empty() &&
               std::isspace(static_cast<unsigned char>(triple.back()))) {
            triple.pop_back();
        }
    }
    if (tc.found()) {
        args = tc.targetFlags;
    }
    if (args.empty() && !triple.empty()) {
        args.push_back("--target=" + triple);
    }
#if defined(_WIN32)
    const std::string emptyInput = "nul";
#else
    const std::string emptyInput = "/dev/null";
#endif
    std::vector<std::string> command{clang};
    for (const std::string& arg : args) command.push_back(arg);
    command.push_back("-x");
    command.push_back("c++");
    command.push_back("-E");
    command.push_back("-v");
    command.push_back(emptyInput);
    std::string output;
    if (!runCapture(command, output, code) || code != 0) {
        return args;
    }
    const std::string startMarker = "#include <...> search starts here:";
    const std::string endMarker = "End of search list.";
    const std::size_t start = output.find(startMarker);
    if (start == std::string::npos) return args;
    const std::size_t end = output.find(endMarker, start);
    if (end == std::string::npos) return args;
    std::size_t pos = start + startMarker.size();
    while (pos < end) {
        const std::size_t lineEnd = output.find('\n', pos);
        std::string line = output.substr(
            pos, lineEnd == std::string::npos ? std::string::npos
                                              : lineEnd - pos);
        pos = lineEnd == std::string::npos ? end : lineEnd + 1;
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line = line.substr(first);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t')) {
            line.pop_back();
        }
        const std::string suffix = " (framework directory)";
        if (line.size() > suffix.size() &&
            line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0) {
            line.erase(line.size() - suffix.size());
        }
        if (isDirectory(line)) args.push_back("-I" + line);
    }
    return args;
}

bool resolveSdk(const SdkRequest& request, SdkResolution& out,
                std::string& error) {
    if (request.input.empty()) {
        error = "native-install needs an SDK path or package name";
        return false;
    }
    const fs::path input(request.input);
    std::error_code ec;
    if (fs::exists(input, ec)) {
        if (isDirectory(input)) {
            if (!configureIncludeRoot(input, out, error, false, {})) {
                error = "SDK directory '" + input.string() + "': " + error;
                return false;
            }
        } else if (!configureInputFile(input, out, error)) {
            error = "'" + input.string() +
                    "' is not a directory or a C/C++ header file";
            return false;
        }
    } else if (!resolveName(request.input, out, error)) {
        return false;
    }

    for (const fs::path& include : request.extraIncludes) {
        std::error_code includeEc;
        if (!fs::is_directory(include, includeEc)) {
            error = "extra include directory '" + include.string() +
                    "' does not exist; fix --include";
            return false;
        }
        const std::string text = absolutePath(include).string();
        addUnique(out.includeDirs, text);
        addUnique(out.publicRoots, text);
    }
    out.defines = request.defines;
    out.clangArgs = clangParseArgs();

    SdkRequest libraries = request;
    if (!detectLibraries(libraries, out, error)) return false;
    return true;
}

}  // namespace cp3::cli::native
