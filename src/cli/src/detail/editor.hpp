// =============================================================================
// src/cli/src/detail/editor.hpp - editor discovery / VSIX helpers DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Backs `cp3c editor-plugin` (and its `editor-plugins` alias). Detection reads
// the extension directories on disk; the editor CLI is only executed to probe
// extension support, to install, or to uninstall. Definitions live in
// src/commands/editor_detect.cpp, editor_vsix.cpp and editor_process.cpp.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "detail/util.hpp"

namespace cp3::cli {

// The state of the cp3 extension inside one editor's extensions directory.
enum class EditorState {
    Current,
    UpdateAvailable,
    NewerInstalled,
    NotInstalled,
    CliMissing,
    Unknown,
};

struct EditorExtension {
    bool installed = false;
    std::string version;
    EditorState state = EditorState::NotInstalled;
};

// One detected editor (or a custom CLI passed through --editor <path>).
struct EditorInfo {
    std::string id;
    std::string name;
    std::string cli;           // empty when the CLI was not found
    std::string extensionsDir; // empty when it cannot be determined
    bool detected = false;
    bool cliFound = false;
    bool installable = false;  // the CLI accepts extension-management flags
    std::string hint;
    EditorExtension extension;
};

// The bundled (or explicitly selected) extension package.
struct VsixInfo {
    fs::path path;
    std::string version;
    std::string sha256;
    bool found = false;
};

const char* editorStateName(EditorState state) noexcept;

// `cp3.cp3-vscode`: publisher `cp3`, extension name `cp3-vscode`.
const char* editorExtensionId() noexcept;

std::vector<std::string> knownEditorIds();
bool editorIdIsKnown(const std::string& id);

// Detects the known editors. `requestedId` plus `extensionsDirOverride` apply a
// portable/custom status directory to one editor (empty or "all" means none).
std::vector<EditorInfo> detectEditors(const std::string& bundledVersion,
                                      const std::string& requestedId,
                                      const std::string& extensionsDirOverride);

// A `--editor <path>` selection. The caller validates that `cliPath` exists.
EditorInfo customEditor(const std::string& cliPath,
                        const std::string& extensionsDir,
                        const std::string& bundledVersion);

// Reads `<extensionsDir>/cp3.cp3-vscode-*`. A missing directory is
// `not-installed` (never an error); an unreadable version is `unknown`.
bool tryReadEditorExtension(const fs::path& extensionsDir,
                            const std::string& bundledVersion,
                            EditorExtension& out);

// Bundled VSIX: CP3_ROOT/share/vscode, findCp3Root()/share/vscode, then the
// checkout's extensions/cp3-vscode/dist. Highest version wins.
VsixInfo findBundledVsix();

// Validates + hashes an explicit --vsix path.
bool inspectVsix(const fs::path& path, VsixInfo& out, std::string& err);

// Copies the VSIX to `out` (empty = the current directory; an existing
// directory receives the basename). `destination` is absolute on success.
bool exportVsix(const VsixInfo& vsix, const fs::path& out, bool force,
                fs::path& destination, std::string& err);

bool hashFileSha256(const fs::path& path, std::string& hex, std::string& err);

// editor-process.cpp: `.cmd`/`.bat` CLIs run through `cmd.exe /c` on Windows.
std::vector<std::string> editorCommand(const std::string& cli,
                                       const std::vector<std::string>& args);
int runEditorCommand(const std::vector<std::string>& command);

// Runs a process with stdout/stderr discarded (probe and install paths keep
// the CLI's own chatter away from JSON output).
int runProcessQuiet(const std::vector<std::string>& args);

}  // namespace cp3::cli
