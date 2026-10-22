// =============================================================================
// src/cli/src/native/sdk.hpp - locating a native SDK and its libraries.
//
// OWNERSHIP: cli module (native-install). PRIVATE header.
//
// The input may be an SDK directory (with include/), a header directory, a
// single header, or a package name. Names are resolved through
// CP3_NATIVE_SDK_PATH, pkg-config and vcpkg. Every failure lists the paths that
// were searched so the user can fix the environment without guessing.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cp3::cli::native {

namespace fs = std::filesystem;

struct SdkRequest {
    std::string input;
    std::vector<fs::path> extraIncludes;
    std::vector<std::string> defines;
    std::vector<fs::path> extraLibDirs;
    std::vector<std::string> links;
};

struct SdkResolution {
    fs::path root;
    fs::path includeDir;
    std::vector<std::string> includeDirs;   // recorded in native.json
    std::vector<std::string> publicRoots;   // declarations under these are bound
    std::vector<std::string> headers;       // absolute public header paths
    std::vector<std::string> clangArgs;     // parse only (target and GCC dirs)
    std::vector<std::string> defines;
    std::vector<std::string> libDirs;
    std::vector<std::string> linkNames;
    std::vector<std::string> staticLibraries;
    std::vector<std::string> sharedLibraries;
    std::vector<std::string> runtimeDlls;
    std::string source;
    std::vector<std::string> searched;
};

// Resolves the SDK and its public headers, then discovers the requested (or
// name-matched) libraries. On failure `error` names what was searched and what
// to install or pass.
bool resolveSdk(const SdkRequest& request, SdkResolution& out,
                std::string& error);

// The ABI family for a libclang target triple: "mingw", "msvc" or "itanium".
std::string deriveAbi(const std::string& target);

// The GCC target triple and system include directories, as clang arguments.
// These are parse-only: they never enter native.json. Empty when no GCC is
// available, in which case libclang parses with its builtin include paths.
std::vector<std::string> gccParseArgs();

// Runs a program and captures stdout+stderr. Returns false when it cannot be
// started; `code` receives the exit status (0 on success).
bool runCapture(const std::vector<std::string>& args, std::string& output,
                int& code);

}  // namespace cp3::cli::native
