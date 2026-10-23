// =============================================================================
// src/cli/src/detail/toolchain_select.hpp - compiler families, versions,
// persistence, resolution and the interactive selector DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/toolchain/toolchain_probe.cpp (family probing),
// toolchain_versions.cpp (version discovery and comparison), toolchain_config.cpp
// (global config), toolchain_manifest.cpp (package.json), toolchain_select.cpp
// (resolution), toolchain_wizard.cpp (menus), toolchain_install.cpp (system
// installer planning) and src/commands/toolchain.cpp (the command surface).
//
// A candidate is a compiler family. A concrete install is one discovered
// driver plus its parsed version. Finding a driver is not enough: validation
// compiles, links and runs a tiny C++20 program before a choice is saved.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "detail/toolchain/toolchain.hpp"

namespace cp3::cli {

// One selectable compiler family (id is stable and goes into saved files).
struct ToolchainCandidate {
    std::string id;    // gcc | clang | msvc | apple-clang
    std::string name;  // friendly name shown in menus
    std::string exe;   // driver searched on PATH
    bool msvc = false;
    bool clang = false;
};

// One discovered system installation. `version` is the normalized compiler
// version (or the MSVC toolset directory name); `banner` is the first line of
// the vendor version output.
struct ToolchainInstall {
    ToolchainCandidate candidate;
    std::string program;
    std::string linkProgram;  // MSVC: matching link.exe
    std::string version;
    std::string banner;
    std::string problem;
    bool usable = false;
};

// One version the platform package manager can install. `version` is the
// compiler version shown to the user; `packageVersion` is the exact package
// identifier passed to the system installer.
struct AvailableToolchainVersion {
    std::string version;
    std::string packageVersion;
};

// Result of probing one family. `program`/`version` describe the newest
// installed version; `problem` explains why an unusable family cannot be used.
struct CandidateStatus {
    ToolchainCandidate candidate;
    std::string program;
    std::string linkProgram;
    std::string version;
    std::string banner;
    std::string problem;
    bool found = false;
    bool usable = false;
};

// Platform menu order: Windows GCC, Clang, MSVC; macOS Apple Clang, Clang, GCC;
// Linux GCC, Clang.
std::vector<ToolchainCandidate> toolchainCandidates();
const ToolchainCandidate* findToolchainCandidate(const std::string& id);

// Extra driver flags this machine needs for the driver to work at all.
//
// A stock Windows LLVM install defaults to the MSVC target, which has no C++
// standard library unless MSVC is installed; the same driver works against a
// MinGW GCC installation under the GNU target. The returned flags are the ones
// that were verified by compiling, linking and running a probe program, so
// the probe and the real build always agree. Empty when the driver's own
// default target is already correct.
std::vector<std::string> toolchainTargetFlags(bool clang, bool msvc,
                                               const std::string& program);

// Parses and compares compiler versions. Missing patch components become zero.
std::string normalizeCompilerVersion(std::string_view text);
int compareCompilerVersions(const std::string& left, const std::string& right);

// Every installed version of a family, newest first. `validate` runs the tiny
// compile link run probe for each version; without it, entries describe what
// was found and parsed. Never copies or installs a compiler.
std::vector<ToolchainInstall> discoverToolchainVersions(
    const ToolchainCandidate& candidate, bool validate);

// Versions the platform package manager offers, newest first. Empty when no
// package manager is available or the vendor has no version list.
std::vector<AvailableToolchainVersion> availableToolchainVersions(
    const ToolchainCandidate& candidate);

// Locates the newest installed version of a family and reports its status.
CandidateStatus probeToolchainCandidate(const ToolchainCandidate& candidate,
                                        bool validate);

// Probes one exact program (used after a version is chosen).
CandidateStatus probeToolchainProgram(const ToolchainCandidate& candidate,
                                      const std::string& program,
                                      const std::string& linkProgram,
                                      bool validate);

// A saved family/version request. `policy` is latest or exact; an empty policy
// with a version keeps the old exact behavior, and an empty version means
// latest. `custom` means `id` (or `program`) is an explicit executable path.
struct ToolchainChoice {
    std::string id;
    std::string version;
    std::string policy;
    std::string program;
    bool custom = false;
    bool present = false;
};

// Parses `id`, `id@version` or an executable path. Does not probe versions.
bool parseToolchainSpec(const std::string& raw, ToolchainChoice& out,
                        std::string& err);

// Resolves a choice against installed system compilers.
enum class ChoiceResolution {
    Ok,
    Invalid,        // unknown family or malformed choice
    FamilyMissing,  // the requested family has no usable installed version
    StrictFailure,  // exact pin or family missing under --strict-toolchain
};

// Resolution outputs grouped so callers pass one result object. `warning` is
// set for fallback paths; `error` is set for invalid choices.
struct ChoiceResolutionResult {
    ChoiceResolution status = ChoiceResolution::Invalid;
    Toolchain toolchain;
    std::string warning;
    std::string error;
};

// `sourceLabel` names the winning source in warnings ("project package.json",
// "global config", "--toolchain", "CP3_TOOLCHAIN"). Exact pins that are missing
// fall back to the newest installed version of the same family unless strict.
// A missing family never silently switches to another family.
ChoiceResolution resolveToolchainChoice(const ToolchainChoice& choice,
                                        const std::string& sourceLabel,
                                        bool strict,
                                        ChoiceResolutionResult& result);

// Diagnostic helper for `toolchain list`: report a stale saved choice even when
// a higher-priority flag or environment value wins. The active resolution's own
// warning is not repeated.
void warnSavedToolchainChoice(const ToolchainChoice& choice,
                              const std::string& label);

// -----------------------------------------------------------------------------
// Global user config: <config dir>/cp3/toolchain.conf, plain key=value lines.
// CP3_CONFIG_DIR overrides the config directory (used by tests).
// -----------------------------------------------------------------------------
struct GlobalToolchainConfig {
    std::string id;
    std::string version;
    std::string policy;
    std::string program;
    std::string saved;
    bool present = false;

    ToolchainChoice choice() const;
};

std::filesystem::path toolchainConfigPath();
GlobalToolchainConfig readGlobalToolchainConfig();
bool writeGlobalToolchainConfig(const ToolchainChoice& choice, std::string& err);
bool clearGlobalToolchainConfig(bool& removed, std::string& err);

// -----------------------------------------------------------------------------
// Project scope: the top-level `"toolchain"` string or object in package.json.
// The writer preserves every other byte (order, indentation, CRLF).
// -----------------------------------------------------------------------------
// Finds the nearest package.json at or above the current directory.
bool toolchainProjectRoot(std::filesystem::path& packageRoot);
bool readProjectToolchain(const std::filesystem::path& packageRoot,
                          ToolchainChoice& out, std::string& err);
bool writeProjectToolchain(const std::filesystem::path& packageRoot,
                           const ToolchainChoice& choice, std::string& err);
bool clearProjectToolchain(const std::filesystem::path& packageRoot,
                           bool& removed, std::string& err);

// Saves a validated choice in the requested scope. Project scope warns when a
// machine-specific custom path is written.
bool saveToolchainChoice(const ToolchainChoice& choice, bool projectScope,
                         const std::filesystem::path& projectRoot,
                         std::string& err);

// -----------------------------------------------------------------------------
// Install planning and execution. planToolchainInstall never runs anything; it
// only selects the exact command (or download URL) for this platform. A
// non-empty packageVersion pins the system package manager to that version.
// -----------------------------------------------------------------------------
struct InstallPlan {
    std::string label;
    std::string command;
    std::vector<std::string> argv;
    std::string url;
    std::string warning;
    bool needsSecondConfirm = false;
    bool runnable = false;
};

InstallPlan planToolchainInstall(const ToolchainCandidate& candidate,
                                 const std::string& packageVersion = {});
// Opens the vendor's own version manager. MSVC uses the Visual Studio
// Installer; other platforms return manual instructions. Never downloads a
// compiler into cp3.
InstallPlan planToolchainMoreVersions(const ToolchainCandidate& candidate);
bool runInstallPlan(const InstallPlan& plan);

// -----------------------------------------------------------------------------
// Minimal single-select menu. A TTY uses arrow keys and Enter (q or Esc
// cancels); a pipe reads one line: a 1-based row number or one of `keys`.
// Returns the chosen index, or -1 when `cancelled` was set (q/Esc/EOF/empty).
// -----------------------------------------------------------------------------
int runMenu(const std::string& title, const std::vector<std::string>& rows,
            const std::vector<std::string>& keys, int initial, bool& cancelled);

// Yes/No single-select. A non-TTY accepts 1/2, y/n, yes/no; EOF means No.
bool runYesNo(const std::string& question);

}  // namespace cp3::cli
