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

// Command line for compiling one generated/real C++ translation unit to an
// object file. The working directory is irrelevant (all paths are explicit).
std::vector<std::string> compileArgs(const Toolchain& tc, BuildProfile profile,
                                     const std::vector<fs::path>& includes,
                                     const fs::path& cpp, const fs::path& obj);

// Command line for linking every object into the final executable.
std::vector<std::string> linkArgs(const Toolchain& tc, BuildProfile profile,
                                  const std::vector<fs::path>& objects,
                                  const fs::path& exe);

}  // namespace cp3::cli
