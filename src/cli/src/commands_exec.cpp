// =============================================================================
// cli/commands_exec.cpp — `cp3c exec <pkg> [args]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdExec.
//
// Packages do not exist yet (D4 Package resolution is unimplemented).
// =============================================================================
#include "private/commands.hpp"

namespace cp3::cli {

int cmdExec(const CliOptions& /*opt*/) {
    return notImplemented("exec");
}

}  // namespace cp3::cli
