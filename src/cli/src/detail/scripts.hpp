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

#include <string>

namespace cp3::cli {

// True for the commands whose pre/post hooks run automatically: the ones that
// operate on a project (run, build, check, dev, fmt, clean, install).
bool hookableCommand(const std::string& command);

// True when this process was started by cp3c to run a hook (nested
// invocations never run hooks).
bool hooksSuppressed();

// Runs package.json's `"scripts.<name>"` from the project root at (or above)
// the current directory, through the platform shell.
//   0   the hook ran and succeeded
//  -1   no such hook (or no readable package.json): nothing ran
//  -2   the shell could not be started at all
//  >0   the hook's own exit code
int runScriptHook(const std::string& name);

}  // namespace cp3::cli
