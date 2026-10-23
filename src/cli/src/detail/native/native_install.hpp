// =============================================================================
// src/cli/src/detail/native_install.hpp - `cp3 native-install` internals.
//
// OWNERSHIP: cli module. PRIVATE header shared by the option parser and the
// discovery/bind/emit implementation.
// =============================================================================
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace cp3::cli {

struct NativeOptions {
    std::string input;
    std::string name;
    std::string out;
    std::vector<std::filesystem::path> includes;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> libDirs;
    std::vector<std::string> links;
    bool force = false;
    bool dryRun = false;
    bool json = false;
    bool verbose = false;
    bool help = false;
};

void printNativeInstallUsage(std::FILE* out);

// Resolves the SDK, parses headers through libclang, writes the binding, and
// reports progress. Returns a process exit code.
int runNativeInstall(const NativeOptions& options);

}  // namespace cp3::cli
