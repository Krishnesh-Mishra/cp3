// =============================================================================
// cli/commands_exec.cpp — `cp3c exec <pkg> [args]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdExec.
//
// Installed packages exist (cp3c install -> .cp3/packages/<name>, §4), but
// running a package's own bundled tool needs its `bin` declaration resolved,
// the package entry built with the `run` pipeline, and the result cached under
// .cp3/packages/<name>/.bin (CLI-README §3) - none of which this build
// provides yet, so exec stays a named not-implemented path.
// =============================================================================
#include <cstdio>

#include "private/commands.hpp"

namespace cp3::cli {

int cmdExec(const CliOptions& /*opt*/) {
    std::fprintf(stderr,
                 "cp3c: 'exec' needs package 'bin' resolution and the .bin/ "
                 "tool cache (CLI-README §3), which this build does not "
                 "provide yet\n");
    return notImplemented("exec");
}

}  // namespace cp3::cli
