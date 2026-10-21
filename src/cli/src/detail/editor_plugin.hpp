// =============================================================================
// src/cli/src/detail/editor_plugin.hpp - `cp3c editor-plugin` internals.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// cmdEditorPlugin (detail/commands.hpp) parses the command word into an
// EditorOptions and dispatches here. The wizard reuses the same action path,
// so flags such as --dry-run, --force and --yes behave identically in both
// modes. The translation units split the surface by concern:
//   editor_plugin.cpp  flags, dispatch, usage
//   editor_actions.cpp status/export/path + JSON + editor selection
//   editor_install.cpp install and uninstall
// =============================================================================
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "detail/editor.hpp"

namespace cp3::cli {

struct CliOptions;

// cli.cpp's shared option parser delegates the editor-plugin flags here so the
// shared file only carries one call per parser hook.
bool matchEditorFlag(const std::string& token, CliOptions& opt);
bool matchEditorValueOption(const std::vector<std::string>& tokens,
                            std::size_t& i, CliOptions& opt, std::string& err);

enum class EditorAction { Status, Install, Uninstall, Export, Path };

struct EditorOptions {
    EditorAction action = EditorAction::Status;
    std::string editor;         // known id, "all", or a custom CLI path
    std::string vsix;           // install only: explicit VSIX
    std::string extensionsDir;  // one-editor status override
    std::string out;            // export destination
    bool json = false;
    bool dryRun = false;
    bool force = false;
    bool yes = false;
    bool interactive = false;
};

// The state one action works on. `editors` is the selected list (all detected
// editors, one known id, or a custom CLI).
struct EditorSession {
    EditorOptions opt;
    VsixInfo vsix;
    std::vector<EditorInfo> editors;
};

struct EditorActionReport {
    std::string action;
    std::string result = "ok";
    std::string editor;
    std::vector<std::string> command;
    std::string destination;
    std::string sha256;
    std::string error;
    bool ok = true;
};

struct EditorPrepareResult {
    bool ok = false;
    int exitCode = 1;
    std::string error;
};

// Shared implementation across the editor-plugin translation units.
void note(const EditorSession& session, const std::string& text);
void emitEditorJson(const EditorSession& session,
                    const EditorActionReport& report);
int failEditorAction(const EditorSession& session, EditorActionReport& report,
                     const std::string& text);
bool prepareEditorVsix(EditorSession& session, bool required, std::string& err);
EditorPrepareResult prepareEditorSession(EditorSession& session);
int runEditorInstall(EditorSession& session, EditorActionReport& report);
int runEditorUninstall(EditorSession& session, EditorActionReport& report);
int runEditorStatus(EditorSession& session, EditorActionReport& report);
int runEditorExport(EditorSession& session, EditorActionReport& report);
int runEditorPath(EditorSession& session, EditorActionReport& report);

int runEditorAction(const EditorOptions& options);
int runEditorWizard(const EditorOptions& options);

// Human status list (`details` adds the CLI path, extensions directory and
// hint lines). Shared by `status` and the wizard.
void printEditorSummary(const std::vector<EditorInfo>& editors, bool details);

}  // namespace cp3::cli
