// =============================================================================
// src/cli/src/detail/package_json.hpp - package.json helpers DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/package_json.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines).
//
// Shared by `build` (package vs application), `pack` and `install`. Field
// defaults follow CLI-README §5.1: `name` falls back to the project directory
// name, `version` to "0.0.0", `main` to src/index.cp3 or src/main.cp3, and an
// absent (or unparsable) `type` means "application".
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

#include "cp3/options.hpp"
#include "detail/json.hpp"

namespace cp3::cli {

// Nearest ancestor of `startDir` (itself included) containing package.json.
// Returns false and leaves `root` untouched when there is none.
bool findPackageRoot(const std::filesystem::path& startDir,
                     std::filesystem::path& root);

// Reads + parses `<root>/package.json`; false with a message in `err` when the
// file is missing or not valid JSON.
bool loadPackageJson(const std::filesystem::path& root, JsonValue& out,
                     std::string& err);

// True when package.json declares `"type": "package"`.
bool isPackageProject(const std::filesystem::path& root);

std::string packageName(const JsonValue& manifest,
                        const std::filesystem::path& root);
std::string packageVersion(const JsonValue& manifest);
std::string packageMain(const JsonValue& manifest);

// Applies the project's type-system settings (spec 03) from `root`'s
// package.json: `typeSystem` ("typescript" | "rust" | "cpp") and
// `strictNumericCasts` (bool). An absent/unreadable/invalid field leaves the
// corresponding option untouched, so the CLI's defaults still apply.
void applyProjectTypeSettings(const std::filesystem::path& root,
                              CompileOptions& compile);

}  // namespace cp3::cli
