// =============================================================================
// cp3/cli.hpp - the command surface for the `cp3c` binary.
//
// OWNERSHIP: cli module. The CLI owns argument parsing and all rendering; the
// frontend stages report through DiagnosticSink and never print themselves.
//
// Exit-code contract (also relied on by tests/runner/run_tests.py):
//   0  success
//   1  compile error (diagnostics were printed)
//   2  usage error (bad flag/command; usage was printed to stderr)
// `run` and `exec` additionally propagate the launched program's own exit code.
// =============================================================================
#pragma once

namespace cp3 {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitCompileError = 1;
inline constexpr int kExitUsageError = 2;

// Entry point for the `cp3c` executable. Never throws; returns an exit code.
int runCli(int argc, char** argv);

}  // namespace cp3
