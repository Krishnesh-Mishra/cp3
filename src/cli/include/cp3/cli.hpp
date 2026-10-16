// =============================================================================
// cp3/cli.hpp — the command surface for the `cp3c` binary.
//
// OWNERSHIP: cli module. The CLI owns argument parsing and all rendering; the
// frontend stages report through DiagnosticSink and never print themselves.
//
// Exit-code contract (also relied on by tests/runner/run_tests.py):
//   0  success
//   1  compile error (diagnostics were printed)
//   2  usage error (bad flag/command; usage was printed to stderr)
//   3  not implemented — this build is still a scaffold (a marker line
//      containing kNotImplementedMarker is printed to stderr)
// =============================================================================
#pragma once

namespace cp3 {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitCompileError = 1;
inline constexpr int kExitUsageError = 2;
inline constexpr int kExitNotImplemented = 3;

// Printed to stderr together with kExitNotImplemented so tooling (and the test
// harness) can tell "the scaffold ran" apart from a genuine failure.
inline constexpr const char* kNotImplementedMarker = "not implemented";

// Entry point for the `cp3c` executable. Never throws; returns an exit code.
int runCli(int argc, char** argv);

}  // namespace cp3
