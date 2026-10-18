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
#include "detail/scripts.hpp"

#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "detail/json.hpp"
#include "detail/package_json.hpp"
#include "detail/util.hpp"

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

int runScriptHook(const std::string& name) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    fs::path root;
    if (!findPackageRoot(cwd, root)) return -1;

    JsonValue manifest;
    std::string err;
    if (!loadPackageJson(root, manifest, err)) return -1;
    const JsonValue* scripts = manifest.find("scripts");
    if (scripts == nullptr || !scripts->isObject()) return -1;
    const std::string* command = scripts->stringField(name);
    if (command == nullptr || command->empty()) return -1;

    std::fprintf(stderr, "cp3c: running script '%s': %s\n", name.c_str(),
                 command->c_str());
    std::fflush(stderr);
    setGuard();
    const int status = std::system(command->c_str());
    clearGuard();
    return normalizeStatus(status);
}

}  // namespace cp3::cli
