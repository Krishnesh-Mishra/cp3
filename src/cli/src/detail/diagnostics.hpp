// =============================================================================
// src/cli/src/detail/diagnostics.hpp - diagnostic rendering DECLARATIONS (DECISIONS D2).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/toolchain/diagnostics.cpp (STYLE.md §3/§8: headers declare,
// `.cpp` defines).
//
// The CLI owns ALL rendering; the compiler stages only report through a
// DiagnosticSink. Two layers:
//
//   * MACHINE (always): exactly one line per diagnostic, the D2 contract:
//       <path>:<line>:<col>: <severity>: <CODE>: <message>
//     This is what tests/runner/run_tests.py matches and must never change.
//
//   * HUMAN (only on a TTY or with --verbose): a source excerpt + caret block
//     printed alongside the machine line. It is purely additive; it never
//     rewrites or suppresses a machine line.
// =============================================================================
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "cp3/diagnostic.hpp"

namespace cp3::cli {

struct RenderOptions {
    bool pretty = false;          // TTY or --verbose
    bool color = false;           // pretty + colored output allowed
    std::string fallbackPath;     // used when a diagnostic has no valid span
};

// Renders every diagnostic. Machine lines always; pretty blocks when requested.
// Notes are emitted as their own machine lines (severity `note`) carrying the
// parent diagnostic's code, per D2's severity set. Suggestions are rendered in
// the pretty layer only - they are advisory prose, not machine-parseable
// assertions, and adding them as machine lines would only add matching noise.
void renderDiagnostics(const std::vector<Diagnostic>& diagnostics,
                       const RenderOptions& opts, std::FILE* out);

}  // namespace cp3::cli
