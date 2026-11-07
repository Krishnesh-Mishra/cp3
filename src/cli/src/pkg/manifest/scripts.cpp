// =============================================================================
// src/cli/src/pkg/scripts.cpp - package.json lifecycle hooks DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/scripts.hpp.
//
// Hook commands are authored for a shell (`cp3 exec cp3ui transform`, chains,
// redirects), so they run through std::system exactly like npm's lifecycle
// scripts run through `sh`/`cmd.exe` - not through an argv splitter, which
// would silently break every shell construct an author expects to work.
// =============================================================================
#include "detail/archive/scripts.hpp"

#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "detail/pkg/json.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/core/util.hpp"

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace cp3::cli {

namespace {

constexpr const char* kGuardVariable = "CP3_SCRIPT_GUARD";

void setGuard() {
#ifdef _WIN32
    _putenv_s(kGuardVariable, "1");
#else
    setenv(kGuardVariable, "1", 1);
#endif
}

void clearGuard() {
#ifdef _WIN32
    _putenv_s(kGuardVariable, "");
#else
    unsetenv(kGuardVariable);
#endif
}

// std::system's return value carries the platform wait status on POSIX and
// the child's exit code directly on Windows.
int normalizeStatus(int status) {
    if (status == -1) return -2;  // the shell could not be started
#ifdef _WIN32
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128;  // terminated by a signal
#endif
}

int runShellCommand(const fs::path& root, const std::string& command) {
    std::error_code ec;
    const fs::path previous = fs::current_path(ec);
    if (ec) return -2;

    fs::current_path(root, ec);
    if (ec) return -2;

    setGuard();
    const int status = std::system(command.c_str());
    clearGuard();

    std::error_code restoreError;
    fs::current_path(previous, restoreError);
    return normalizeStatus(status);
}

}  // namespace

bool hookableCommand(const std::string& command) {
    return command == "run" || command == "build" || command == "check" ||
           command == "dev" || command == "fmt" || command == "clean" ||
           command == "install";
}

bool hooksSuppressed() {
    const char* guard = std::getenv(kGuardVariable);
    return guard != nullptr && guard[0] != '\0';
}

PackageScript findPackageScript(const std::string& name) {
    PackageScript result;
    result.name = name;
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec) {
        result.error = "cannot determine the current directory: " + ec.message();
        return result;
    }
    if (!findPackageRoot(cwd, result.root)) return result;

    JsonValue manifest;
    if (!loadPackageJson(result.root, manifest, result.error)) return result;

    const JsonValue* scripts = manifest.find("scripts");
    if (scripts == nullptr) return result;
    if (!scripts->isObject()) {
        result.error = (result.root / "package.json").string() +
                       ": 'scripts' must be an object";
        return result;
    }

    for (std::size_t i = 0; i < scripts->keys.size(); ++i) {
        if (scripts->keys[i] != name) continue;
        const JsonValue& value = scripts->values[i];
        if (!value.isString() || value.string.empty()) {
            result.error = (result.root / "package.json").string() +
                           ": 'scripts." + name +
                           "' must be a non-empty command string";
            return result;
        }
        result.command = value.string;
        result.found = true;
        return result;
    }
    return result;
}

int runPackageScript(const PackageScript& script) {
    if (!script.found || script.command.empty()) return -1;
    std::fprintf(stderr, "cp3c: running script '%s': %s\n",
                 script.name.c_str(), script.command.c_str());
    std::fflush(stderr);
    return runShellCommand(script.root, script.command);
}

int runScriptHook(const std::string& name) {
    const PackageScript script = findPackageScript(name);
    if (!script.error.empty() || !script.found) return -1;

    std::fprintf(stderr, "cp3c: running script '%s': %s\n", name.c_str(),
                 script.command.c_str());
    std::fflush(stderr);
    return runShellCommand(script.root, script.command);
}

}  // namespace cp3::cli
