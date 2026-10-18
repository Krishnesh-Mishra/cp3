// =============================================================================
// cp3c - executable entry point.
//
// All behavior lives in cp3::runCli (src/cli/), so the command surface is
// testable without a process boundary. main() only forwards argv and the
// resulting exit code.
// =============================================================================
#include "cp3/cli.hpp"

int main(int argc, char** argv) {
    return cp3::runCli(argc, argv);
}
