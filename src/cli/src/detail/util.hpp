// =============================================================================
// src/cli/src/detail/util.hpp - platform/IO/string helper DECLARATIONS for cp3c.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header - not part of the frozen
// public surface (cp3/cli.hpp).
//
// Definitions live in src/toolchain/util.cpp (STYLE.md §3/§8: headers declare, `.cpp`
// defines). Standard C++20 only. No third-party libraries.
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cp3::cli {

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// File IO
// -----------------------------------------------------------------------------

// Reads a whole file as bytes. Returns false (and leaves `out` untouched-ish)
// when the file cannot be opened.
bool readTextFile(const fs::path& path, std::string& out);

// Writes `content` to `path`, creating parent directories. When `changed` is
// non-null it is set to true only if the file was created or its bytes differed
// (so callers can implement incremental generation).
bool writeTextFile(const fs::path& path, std::string_view content,
                   bool* changed = nullptr);

// -----------------------------------------------------------------------------
// Hashing (FNV-1a 64) - used only for change detection, never for security.
// -----------------------------------------------------------------------------
std::uint64_t fnv1a64(std::string_view data) noexcept;

// -----------------------------------------------------------------------------
// Executable discovery / process execution
// -----------------------------------------------------------------------------

std::string stripSurroundingQuotes(std::string s);

// Searches PATH for `name` (trying platform-appropriate suffixes on Windows).
// On success `found` receives the full path and true is returned.
bool findExecutable(const std::string& name, std::string& found);

// Runs a program to completion with inherited stdin/stdout/stderr and returns
// its exit code. Returns -1 when the process could not be launched. No shell is
// involved, so arguments with spaces need no quoting.
int runProcess(const std::vector<std::string>& args);

// Renders an argv vector for a --verbose log line.
std::string describeCommand(const std::vector<std::string>& args);

// -----------------------------------------------------------------------------
// Terminal / memory probing
// -----------------------------------------------------------------------------

bool isTty(std::FILE* f) noexcept;

// Total physical RAM in megabytes, or 0 when it cannot be determined.
std::uint64_t totalPhysicalMemoryMb() noexcept;

// -----------------------------------------------------------------------------
// Repository-relative include roots
// -----------------------------------------------------------------------------
//
// The generated C++ includes the frozen runtime header (cp3/core/runtime.hpp) and
// may include common headers. `__FILE__` inside util.cpp identifies the
// repository, letting us locate src/runtime/include and src/common/include
// without any build-time define. Only directories that actually exist are
// returned.
std::vector<fs::path> compilerIncludeRoots();

// Absolute paths of the runtime translation units that generated code links
// against. The runtime moved from a flat `src/runtime/` directory to
// `src/runtime/src/`; the directory is therefore *listed* rather than a single
// file name hard-coded, preferring the current layout. Only the documented
// minimum set is returned (see src/toolchain/util.cpp).
std::vector<fs::path> runtimeSourcePaths();

// -----------------------------------------------------------------------------
// Misc
// -----------------------------------------------------------------------------

bool endsWith(std::string_view s, std::string_view suffix) noexcept;

bool startsWith(std::string_view s, std::string_view prefix) noexcept;

// Removes a directory tree, ignoring errors. Returns true when nothing remains.
bool removeTree(const fs::path& dir);

}  // namespace cp3::cli
