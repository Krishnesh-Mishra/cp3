// =============================================================================
// src/cli/src/detail/scripts.hpp - package.json lifecycle hooks DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/scripts.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines).
//
// CLI-README §3: a script named `pre<name>`/`post<name>` in package.json runs
// automatically immediately before/after the built-in command named `<name>`,
// through the platform shell (npm's convention, adopted as-is). This is what
// lets a package like cp3ui wire itself in with one package.json line:
//
//   { "scripts": { "prebuild": "cp3 exec cp3ui transform" } }
//
// A hook that spawns cp3c again must not re-enter the hook it is running, so
// cp3c exports CP3_SCRIPT_GUARD to the shell; a child that sees it set never
// runs hooks itself.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

namespace cp3::cli {

// True for the commands whose pre/post hooks run automatically: the ones that
// operate on a project (run, build, check, dev, fmt, clean, install).
bool hookableCommand(const std::string& command);

// True when this process was started by cp3c to run a hook (nested
// invocations never run hooks).
bool hooksSuppressed();

// Resolved package script. `found` is false when the current directory is not
// inside a project or the requested name is not present in package.json.
struct PackageScript {
    std::filesystem::path root;
    std::string name;
    std::string command;
    bool found = false;
    std::string error;
};

// Looks up `scripts.<name>` in the nearest package.json. A malformed
// package.json or scripts block is reported in `error`.
PackageScript findPackageScript(const std::string& name);

// Runs a resolved project script through the platform shell from the package
// root. The script runs with CP3_SCRIPT_GUARD set so nested cp3c calls execute
// built-in commands without recursively dispatching package scripts/hooks.
// Returns the child exit code, or -2 if the shell could not be started.
int runPackageScript(const PackageScript& script);

// Runs package.json's `"scripts.<name>"` from the project root at (or above)
// the current directory, through the platform shell.
//   0   the hook ran and succeeded
//  -1   no such hook (or no readable package.json): nothing ran
//  -2   the shell could not be started at all
//  >0   the hook's own exit code
int runScriptHook(const std::string& name);

}  // namespace cp3::cli
