// =============================================================================
// src/cli/src/detail/toolchain_select.hpp - compiler candidates, probing,
// persistence and the interactive selector DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/toolchain/toolchain_probe.cpp (candidate probing and
// validation), toolchain_config.cpp (global config + project package.json),
// toolchain_menu.cpp (single-select input and install plans) and
// src/commands/toolchain.cpp (the `cp3c toolchain` command surface).
//
// A candidate is a named compiler family. Finding its driver is not enough:
// probeToolchainCandidate(validate=true) compiles, links and runs a tiny C++20
// program, so `cp3c toolchain list` reports installs that cannot actually
// produce a binary truthfully.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "detail/toolchain.hpp"

namespace cp3::cli {

// One selectable compiler family (id is stable and goes into the saved files).
struct ToolchainCandidate {
    std::string id;    // gcc | clang | msvc | apple-clang
    std::string name;  // friendly name shown in menus
    std::string exe;   // driver searched on PATH
    bool msvc = false;
    bool clang = false;
};

// Result of probing one candidate. `problem` explains why an unusable
// candidate cannot be used; it is empty for a usable one.
struct CandidateStatus {
    ToolchainCandidate candidate;
    std::string program;      // full driver path, empty when missing
    std::string linkProgram;  // MSVC: link.exe path (required by linkArgs)
    std::string version;      // first line of the version banner
    std::string problem;
    bool found = false;
    bool usable = false;
};

// Platform menu order: Windows GCC, Clang, MSVC; macOS Apple Clang, Clang, GCC;
// Linux GCC, Clang.
std::vector<ToolchainCandidate> toolchainCandidates();
const ToolchainCandidate* findToolchainCandidate(const std::string& id);

// Locates a candidate. With `validate` it also runs the tiny compile link run
// probe described in the file header.
CandidateStatus probeToolchainCandidate(const ToolchainCandidate& candidate,
                                        bool validate);

// Resolves an id, a bare command name or a full driver path into a Toolchain.
// Fails with a precise reason when nothing executable matches.
bool resolveToolchainValue(const std::string& value, Toolchain& out,
                           std::string& err);

// -----------------------------------------------------------------------------
// Global user config: <config dir>/cp3/toolchain.conf, plain key=value lines.
// CP3_CONFIG_DIR overrides the config directory (used by tests).
// -----------------------------------------------------------------------------
struct GlobalToolchainConfig {
    std::string id;
    std::string program;
    std::string saved;
    bool present = false;
};

std::filesystem::path toolchainConfigPath();
GlobalToolchainConfig readGlobalToolchainConfig();
bool writeGlobalToolchainConfig(const std::string& id,
                                const std::string& program, std::string& err);
bool clearGlobalToolchainConfig(bool& removed, std::string& err);

// -----------------------------------------------------------------------------
// Project scope: the top-level `"toolchain"` string in package.json. The
// writer preserves every other byte (order, indentation, CRLF).
// -----------------------------------------------------------------------------
bool readProjectToolchain(const std::filesystem::path& packageRoot,
                          std::string& out, std::string& err);
bool writeProjectToolchain(const std::filesystem::path& packageRoot,
                           const std::string& value, std::string& err);
bool clearProjectToolchain(const std::filesystem::path& packageRoot,
                           bool& removed, std::string& err);

// -----------------------------------------------------------------------------
// Install planning and execution. planToolchainInstall never runs anything; it
// only selects the exact command (or download URL) for this platform.
// -----------------------------------------------------------------------------
struct InstallPlan {
    std::string label;   // exact command, or the manual instructions
    std::string command; // human-readable command line (empty when manual)
    std::vector<std::string> argv;
    std::string url;
    std::string warning;
    bool needsSecondConfirm = false;  // MSVC Build Tools is a large download
    bool runnable = false;
};

InstallPlan planToolchainInstall(const ToolchainCandidate& candidate);
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
