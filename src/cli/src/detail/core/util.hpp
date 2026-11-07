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

// A child process that the development watcher can keep alive while it waits
// for source edits. The native handle is private to process.cpp; callers must
// only use the helpers below and stop a live child before discarding it.
struct ChildProcess {
    std::intptr_t native = -1;
};

enum class ChildProcessState { Running, Exited, Failed };

bool startChildProcess(const std::vector<std::string>& args,
                       ChildProcess& child);
ChildProcessState pollChildProcess(ChildProcess& child, int& exitCode);
void stopChildProcess(ChildProcess& child) noexcept;

// Runs `args` with stdout and stderr captured into `out`. Returns the exit
// code, or -1 when the process could not be launched. No shell is involved.
int runProcessCapture(const std::vector<std::string>& args, std::string& out);

// Renders an argv vector for a --verbose log line.
std::string describeCommand(const std::vector<std::string>& args);

// -----------------------------------------------------------------------------
// Terminal / memory probing
// -----------------------------------------------------------------------------

bool isTty(std::FILE* f) noexcept;

// Total physical RAM in megabytes, or 0 when it cannot be determined.
std::uint64_t totalPhysicalMemoryMb() noexcept;

// -----------------------------------------------------------------------------
// Cp3 root discovery and the include roots that hang off it
// -----------------------------------------------------------------------------
//
// The generated C++ includes the frozen runtime header (cp3/core/runtime.hpp) and
// may include common headers, so a program build needs the shipped header trees
// and the runtime translation units. The installer ships those trees next to
// bin/cp3c.exe (dist/packaging/windows), so the root is discovered relocatably: from
// CP3_ROOT (set by the cp3.cmd shim), from the running executable's ancestors,
// and finally from this module's compile-time path (`__FILE__`) for an
// in-checkout compiler. Since D72 the always-available std families (string,
// typed-arrays) contribute their ABI trees here; every other family's tree is
// added by its native row when imported. Only directories that actually exist
// are returned.
std::vector<fs::path> compilerIncludeRoots();

// True when `markers` occurs in `text`. `markers` is a '|'-separated any-of
// list; an empty list means "always". Used to decide which runtime translation
// units a generated program links: the program links what it names.
bool markerListMatches(const std::string& text, std::string_view markers);

// Absolute paths of the runtime translation units that `programText` (the
// generated C++ of the whole program: every generated source and header, plus
// the text of the selected native-backend TUs) links against. The runtime moved
// from a flat `src/runtime/` directory to `src/runtime/src/`; the directory is
// therefore *listed* rather than a single file name hard-coded, preferring the
// current layout. Only the units whose markers occur in `programText` are
// returned, so a program that never uses the loop, promises, or the parallel
// engine does not link those TUs at all.
std::vector<fs::path> runtimeSourcePaths(const std::string& programText);

// Cp3 root: the ancestor directory that owns the shared trees (`src/runtime`
// plus `src/std`; a source checkout also has `src/cli`), normally also
// `vendor/third_party/`. Empty when the layout cannot be recognised. The std modules'
// native backends (detail/native_packages.hpp, DECISIONS D68/D70) resolve their
// vendored paths against this root.
fs::path repositoryRoot();

// Maps a repository-relative include root ("src/<x>/include", for example
// "src/runtime/include" or "src/std/console/include") onto the installed
// layout, where the same header tree lives at "<root>/include/<x>"
// (dist/packaging/windows/package.ps1 stages every header tree there). Returns
// "<root>/<rel>" unchanged when that directory exists, so a source checkout is
// unaffected; falls back to "<root>/<rel>" when the mapped directory does not
// exist either, so callers report the path a checkout would have to provide.
fs::path resolveIncludeRoot(const fs::path& root, const std::string& rel);

// -----------------------------------------------------------------------------
// Misc
// -----------------------------------------------------------------------------

bool endsWith(std::string_view s, std::string_view suffix) noexcept;

bool startsWith(std::string_view s, std::string_view prefix) noexcept;

// Removes a directory tree, ignoring errors. Returns true when nothing remains.
bool removeTree(const fs::path& dir);

}  // namespace cp3::cli
