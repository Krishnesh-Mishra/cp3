// =============================================================================
// src/cli/src/detail/toolchain.hpp - system C++ toolchain DECLARATIONS for cp3c.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/toolchain/toolchain.cpp (STYLE.md §3/§8: headers declare,
// `.cpp` defines).
//
// Selection order is DECISIONS D12:
//   Windows: MSVC `cl` -> Clang `clang++`, then last-resort `g++` -> `clang++`.
//   POSIX:   Clang `clang++` -> `g++`,      then last-resort `g++` -> `clang++`.
// The first present in the order wins. The choice is probed once and cached;
// --verbose reports it. Everything compiles as C++20.
//
// cp3 projects never use CMake: cp3c invokes the compiler driver directly, for
// the compile (one object per TU) and link phases
// (docs/cli-tooling/01-compilation-pipeline.md).
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cp3/options.hpp"

#include "detail/util.hpp"

namespace cp3::cli {

struct Toolchain {
    std::string program;  // full path to the driver, empty when none found
    std::string name;     // friendly name: "MSVC cl", "Clang", "GCC"
    bool msvc = false;
    bool clang = false;

    bool found() const noexcept { return !program.empty(); }
};

// Probes the D12 order once and caches the result.
const Toolchain& detectToolchain();

// Human-readable summary for --verbose and failure messages.
std::string describeToolchain(const Toolchain& tc);

// One compilation step: the TU, its object, the program's include roots and any
// native-backend compile flags (DECISIONS D68, detail/native_packages.hpp).
struct CompileStep {
    std::vector<fs::path> includes;
    fs::path cpp;
    fs::path obj;
    std::vector<std::string> extraFlags;
};

// One link step: every object of the program, the output executable and any
// native-backend flags (-L directories then -l archives, D68).
struct LinkStep {
    std::vector<fs::path> objects;
    fs::path exe;
    std::vector<std::string> extraFlags;
};

// Command line for compiling one generated/real C++ translation unit to an
// object file. The working directory is irrelevant (all paths are explicit).
// `step.extraFlags` is appended verbatim (register-level defines such as
// UWS_NO_ZLIB must apply to every TU of an std/network program).
std::vector<std::string> compileArgs(const Toolchain& tc, BuildProfile profile,
                                     const CompileStep& step);

// Command line for linking every object into the final executable.
// `step.extraFlags` is appended after the objects, so `-l` resolution order
// stays correct.
std::vector<std::string> linkArgs(const Toolchain& tc, BuildProfile profile,
                                  const LinkStep& step);

}  // namespace cp3::cli
