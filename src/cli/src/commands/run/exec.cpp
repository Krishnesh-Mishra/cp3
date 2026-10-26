// =============================================================================
// src/cli/src/commands/run/exec.cpp - `cp3c exec <tool> [args...]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdExec.
//
// Resolves a `bin` command declared by an already-installed package
// (.cp3/packages/<name>/package.json, or a `cp3 link` target recorded in the
// lockfile), builds that one .cp3 file through the normal build pipeline, and
// caches the executable in the package's .bin/ directory (CLI-README §3).
// Later invocations reuse the cached binary until the tool source changes.
// Nothing is installed or fetched here; the package must already be a project
// dependency.
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "detail/cache/cache.hpp"
#include "detail/core/commands.hpp"
#include "detail/core/util.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/lockfile.hpp"
#include "detail/pkg/package_json.hpp"

namespace cp3::cli {

namespace {

// One resolved `bin` command: the package that declares it and the .cp3 file
// the package maps the command to.
struct ToolTarget {
    std::string package;
    std::string version;
    fs::path packageDir;
    fs::path source;
};

// Every directory that can hold an installed package: the normal install
// location plus each directory link recorded in the lockfile. Artifact links
// are extracted under .cp3/link/<name>/, which the lockfile records as their
// target too.
std::vector<fs::path> installedPackageDirs(const fs::path& projectRoot,
                                           std::string& err) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    const fs::path packages = projectRoot / ".cp3" / "packages";
    if (fs::is_directory(packages, ec)) {
        for (const auto& entry : fs::directory_iterator(packages, ec)) {
            if (entry.is_directory(ec) &&
                fs::is_regular_file(entry.path() / "package.json", ec)) {
                dirs.push_back(entry.path());
            }
        }
    }
    std::vector<LockEntry> lock;
    if (!readLockfile(projectRoot, lock, err)) return {};
    for (const LockEntry& entry : lock) {
        if (!isLinkEntry(entry) || entry.target.empty()) continue;
        fs::path target(entry.target);
        if (!target.is_absolute()) target = projectRoot / target;
        if (fs::is_regular_file(target / "package.json", ec)) {
            dirs.push_back(target);
        }
    }
    return dirs;
}

// The package's `bin` object: command name -> package-relative .cp3 path.
const JsonValue* binTable(const JsonValue& manifest) {
    const JsonValue* bin = manifest.find("bin");
    return bin != nullptr && bin->isObject() ? bin : nullptr;
}

// Finds the installed package that declares `command` in its `bin` object.
// `exec` is named by the tool command, not necessarily by the package name:
// a package may expose more than one tool. Returns kExitSuccess on a match,
// kExitUsageError for an unknown tool, and kExitCompileError for a malformed
// lockfile.
int resolveTool(const fs::path& projectRoot, const std::string& command,
                ToolTarget& out, std::string& err) {
    std::string lockError;
    const std::vector<fs::path> dirs = installedPackageDirs(projectRoot, lockError);
    if (!lockError.empty()) {
        err = lockError;
        return kExitCompileError;
    }
    for (const fs::path& dir : dirs) {
        JsonValue manifest;
        std::string loadError;
        if (!loadPackageJson(dir, manifest, loadError)) continue;
        const JsonValue* bin = binTable(manifest);
        if (bin == nullptr) continue;
        const std::string* relative = bin->stringField(command);
        if (relative == nullptr) continue;
        out.package = packageName(manifest, dir);
        out.version = packageVersion(manifest);
        out.packageDir = dir;
        out.source = dir / fs::path(*relative);
        return kExitSuccess;
    }
    err = "no installed package provides a 'bin' command named '" + command +
          "'; run 'cp3c install <path-or-url>' first";
    return kExitUsageError;
}

// True when the cached executable still matches the tool source it was built
// from. The stamp is the source content hash plus the package version, so an
// edited tool or an updated package rebuilds on the next `exec`.
bool toolCacheValid(const fs::path& cacheExe, const fs::path& stampPath,
                    const std::string& stamp) {
    std::error_code ec;
    std::string previous;
    return fs::is_regular_file(cacheExe, ec) &&
           readTextFile(stampPath, previous) && previous == stamp;
}

// The build places bundled native runtime libraries next to the linked
// executable; the cached tool needs them beside it too.
void copyRuntimeLibraries(const fs::path& fromDir, const fs::path& toDir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fromDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string extension = entry.path().extension().string();
        if (extension != ".dll" && extension != ".so" && extension != ".dylib") {
            continue;
        }
        fs::copy_file(entry.path(), toDir / entry.path().filename(),
                      fs::copy_options::overwrite_existing, ec);
    }
}

}  // namespace

int cmdExec(const CliOptions& opt) {
    if (opt.positionals.empty()) {
        std::fprintf(stderr, "cp3c: exec needs a package tool name\n");
        std::fputs("usage: cp3c exec <tool> [args...]\n", stderr);
        return kExitUsageError;
    }
    const std::string tool = opt.positionals.front();
    const std::vector<std::string> args(opt.positionals.begin() + 1,
                                        opt.positionals.end());

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    fs::path projectRoot;
    if (!findPackageRoot(cwd, projectRoot)) {
        std::fprintf(stderr,
                     "cp3c: no package.json found in %s or any parent; run "
                     "'cp3c init' first\n",
                     cwd.string().c_str());
        return kExitUsageError;
    }

    ToolTarget target;
    std::string err;
    const int resolveExit = resolveTool(projectRoot, tool, target, err);
    if (resolveExit != kExitSuccess) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return resolveExit;
    }
    if (!fs::is_regular_file(target.source, ec)) {
        std::fprintf(stderr,
                     "cp3c: package '%s' declares bin '%s' as '%s', but that "
                     "file does not exist\n",
                     target.package.c_str(), tool.c_str(),
                     target.source.string().c_str());
        return kExitCompileError;
    }

    std::string sourceText;
    if (!readTextFile(target.source, sourceText)) {
        std::fprintf(stderr, "cp3c: cannot read '%s'\n",
                     target.source.string().c_str());
        return kExitCompileError;
    }
    const std::string stamp = std::to_string(fnv1a64(sourceText)) + "\n" +
                              target.version;
    const fs::path cacheDir = target.packageDir / ".bin";
    const fs::path stampPath = cacheDir / (tool + ".stamp");
#if defined(_WIN32)
    const std::string suffix = ".exe";
#else
    const std::string suffix = "";
#endif
    const fs::path cacheExe = cacheDir / (tool + suffix);

    if (!toolCacheValid(cacheExe, stampPath, stamp)) {
        CliOptions build = opt;
        build.file = target.source.string();
        build.positionals.clear();
        build.programArgs.clear();
        build.execTool = true;
        const int rc = cmdBuildOrRun(build, false);
        if (rc != kExitSuccess) return rc;

        const fs::path built = makeBuildLayout(target.source).exePath;
        if (!fs::is_regular_file(built, ec)) {
            std::fprintf(stderr,
                         "cp3c: internal error: build produced no executable "
                         "for '%s'\n",
                         target.source.string().c_str());
            return kExitCompileError;
        }
        fs::create_directories(cacheDir, ec);
        fs::copy_file(built, cacheExe, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "cp3c: cannot cache '%s': %s\n",
                         cacheExe.string().c_str(), ec.message().c_str());
            return kExitCompileError;
        }
        copyRuntimeLibraries(built.parent_path(), cacheDir);
        writeTextFile(stampPath, stamp, nullptr);
    }

    std::vector<std::string> runArgs;
    runArgs.push_back(cacheExe.string());
    runArgs.insert(runArgs.end(), args.begin(), args.end());
    const int rc = runProcess(runArgs);
    if (rc < 0) {
        std::fprintf(stderr, "cp3c: could not execute %s\n",
                     cacheExe.string().c_str());
        return kExitCompileError;
    }
    return rc;
}

}  // namespace cp3::cli
