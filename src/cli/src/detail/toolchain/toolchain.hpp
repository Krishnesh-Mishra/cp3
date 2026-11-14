// =============================================================================
// src/cli/src/detail/toolchain.hpp - system C++ toolchain DECLARATIONS for cp3c.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/toolchain/toolchain.cpp (STYLE.md §3/§8: headers declare,
// `.cpp` defines).
//
// Beta one is Clang-only. cp3c looks for `clang++`, validates that it can
// compile, link and run a C++20 probe, and uses the newest supported install.
// The choice is probed once and cached; --verbose reports it. Everything
// compiles as C++20.
//
// A saved choice takes precedence over auto-detection
// (detail/toolchain_select.hpp):
//   --toolchain flag > CP3_TOOLCHAIN > project package.json > global config >
//   auto-detect. The first source that resolves to a usable Clang wins; an
//   unusable source is reported and the next one is tried.
//
// cp3 projects never use CMake: cp3c invokes the compiler driver directly, for
// the compile (one object per TU) and link phases
// (docs/cli-tooling/01-compilation-pipeline.md).
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cp3/host/options.hpp"

#include "detail/core/util.hpp"

namespace cp3::cli {

struct Toolchain {
    std::string program;  // full path to clang++, empty when none found
    std::string name;     // always "Clang"
    std::string id;       // always "clang"
    std::string version;  // exact parsed effective compiler version
    std::string recordedVersion;  // version recorded in the choice, if any
    std::string policy;   // latest | exact | custom | auto
    // Extra driver flags this machine needs (the GNU target triple and, when
    // necessary, the MinGW-w64 sysroot on Windows). They are part of the
    // compiler identity: the same driver with a different target produces
    // different objects.
    std::vector<std::string> targetFlags;

    bool found() const noexcept { return !program.empty(); }
};

// Where the effective choice came from (detail/toolchain_select.cpp).
enum class ToolchainSource {
    None,         // no driver found by any source
    Flag,         // --toolchain <id|path>
    Environment,  // CP3_TOOLCHAIN
    Project,      // nearest package.json "toolchain"
    Global,       // user toolchain.conf
    Auto,         // newest supported Clang found on this machine
};

// Resolves the precedence order once and caches the result. A saved source
// whose driver vanished is reported once and skipped.
const Toolchain& detectToolchain();

// Probes the newest supported Clang without consulting any saved choice.
Toolchain probeAutoToolchain();

// CLI wiring: the global --toolchain value (empty clears it), the directory
// whose nearest package.json supplies the project scope, and --strict-toolchain.
// All reset the cache.
void setToolchainOverride(std::string value);
void setToolchainProjectDirectory(fs::path dir);
void setToolchainStrict(bool strict);
void resetToolchainResolution();

// Source of the cached choice, for --verbose and `cp3c toolchain list`.
ToolchainSource toolchainSource() noexcept;
std::string toolchainSourceDescription();
std::string toolchainWarning();
bool toolchainStrictFailed() noexcept;

// Recorded versus effective version, for --verbose and list.
std::string toolchainVersionDescription();

// Human-readable summary for --verbose and failure messages.
std::string describeToolchain(const Toolchain& tc);

// Stable build-cache identity: compiler, exact parsed version and program path.
std::string toolchainCacheKey(const Toolchain& tc);

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

// Per-build optimization switches layered on top of the profile. The release
// profile turns these on by default; the CLI flags in commands.hpp can turn
// each one off. They are also passed to the link step because LTO performs
// code generation there and needs the same -O/march/math settings.
struct BuildOptimizations {
    std::string optLevel;   // empty: profile default (release O3, incremental O2)
    bool lto = false;
    bool nativeIsa = false;
    bool unrollLoops = false;
    bool noMathErrno = false;
    bool noPlt = false;
    std::string pgoProfile;   // -fprofile-instr-use=<file>
    std::string pgoGenerate;  // -fprofile-instr-generate=<template>
};

// Command line for compiling one generated/real C++ translation unit to an
// object file. The working directory is irrelevant (all paths are explicit).
// `step.extraFlags` is appended verbatim (register-level defines such as
// UWS_NO_ZLIB must apply to every TU of an std/network program).
std::vector<std::string> compileArgs(
    const Toolchain& tc, BuildProfile profile, const CompileStep& step,
    const BuildOptimizations& optimizations = {});

// Command line for linking every object into the final executable.
// `step.extraFlags` is appended after the objects, so `-l` resolution order
// stays correct.
std::vector<std::string> linkArgs(
    const Toolchain& tc, BuildProfile profile, const LinkStep& step,
    const BuildOptimizations& optimizations = {});

// Command line for the dev module-libs pipeline (CP3_DEV_MODULE_LIBS): one
// shared library per unit, or cp3runtime_dev.dll for the runtime TUs.
// Mirrors linkArgs (same target/sysroot, same optimizations - LTO applies at
// DLL link time too, same --gc-sections pairing) without the exe-only
// startup flags. `exportDef` is the module-definition file that names the
// cp3 symbols to export (lld has no --exclude-libs; the def file is what
// keeps libstdc++/libuv/... out of the export table). Emits an import
// library beside the DLL so later links can name it explicitly; `importLibs`
// are the import libraries of the DLLs linked earlier (dependencies must
// precede their consumers).
std::vector<std::string> devDllLinkArgs(
    const Toolchain& tc, BuildProfile profile, const fs::path& dll,
    const fs::path& importLib, const std::vector<fs::path>& objects,
    const std::vector<fs::path>& importLibs,
    const std::vector<std::string>& nativeLink,
    const BuildOptimizations& optimizations = {},
    const fs::path& exportDef = {});

// Command line for the dev module-libs bootstrap executable (plain C++ TU,
// no objects: the bootstrap binds everything dynamically). Mirrors linkArgs'
// Windows static startup and adds the runtime include root the generated
// bootstrap includes cp3/dev/hotpatch.hpp from.
std::vector<std::string> bootstrapLinkArgs(
    const Toolchain& tc, BuildProfile profile, const fs::path& exe,
    const fs::path& cpp, const fs::path& runtimeInclude,
    const BuildOptimizations& optimizations = {});

}  // namespace cp3::cli
