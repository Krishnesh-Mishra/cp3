// =============================================================================
// src/modules/src/detail/file_paths.h - filesystem path helpers (PRIVATE header).
//
// OWNERSHIP: modules module (src/modules/**). Never included cross-module.
//
// Definitions live in src/support/file_paths.cpp (STYLE.md §3: headers declare,
// `.cpp` defines). The project-root/module-name rules deliberately mirror
// cp3::cli::findProjectRoot / cp3::cli::moduleNameFor so the graph's metadata
// matches the CLI's build layout without linking cp3_cli.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace cp3::modules {

namespace fs = std::filesystem;

// Canonical, '/'-separated filesystem key; case-folded on Windows. Used to
// dedupe units on a path regardless of `./`, `..`, separator or case form.
std::string canonicalKey(const fs::path& path);

// The absolute folder containing `path` (weakly canonicalized when possible).
fs::path folderOf(const fs::path& path);

// Nearest ancestor with a `package.json` file, else the file's own folder.
fs::path projectRootFor(const fs::path& file);

// Project-relative logical module name: relative path minus the extension.
std::string moduleNameFor(const fs::path& file);

// Reads a whole file as bytes. Returns false when it cannot be opened.
bool readWholeFile(const fs::path& path, std::string& out);

// ASCII prefix/suffix tests (kept local: cp3_common has no string helpers).
bool startsWith(std::string_view s, std::string_view prefix) noexcept;
bool endsWith(std::string_view s, std::string_view suffix) noexcept;

}  // namespace cp3::modules
