// =============================================================================
// src/cli/src/commands/editor_install.cpp - install / uninstall actions.
//
// OWNERSHIP: cli module (src/cli/**). Part of `cp3c editor-plugin`. Runs the
// editor CLI only when the extension directory says the bundled version is not
// already current (or --force was passed), then re-reads the directory.
// =============================================================================
#include "detail/editor/editor_plugin.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "cp3/cli.hpp"

#include "detail/editor/editor_prompt.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

struct InstallTally {
    bool changed = false;
    bool updated = false;
    bool already = false;
    bool planned = false;
    bool failed = false;
};

struct UninstallTally {
    bool removed = false;
    bool absent = false;
    bool planned = false;
    bool failed = false;
};

int installOne(EditorSession& session, EditorInfo& editor,
               EditorActionReport& report, InstallTally& tally) {
    const EditorOptions& options = session.opt;
    if (!editor.cliFound || !editor.installable) {
        const std::string hint =
            editor.hint.empty() ? "extension CLI unavailable" : editor.hint;
        std::fprintf(stderr, "cp3c: %s: %s\n", editor.name.c_str(),
                     hint.c_str());
        tally.failed = true;
        return kExitCompileError;
    }

    const bool wasInstalled = editor.extension.installed;
    if (wasInstalled && editor.extension.state == EditorState::Current &&
        !options.force) {
        note(session, "  " + editor.name + ": already installed (cp3 " +
                          editor.extension.version + ")");
        tally.already = true;
        return kExitSuccess;
    }
    if (wasInstalled && editor.extension.state == EditorState::NewerInstalled &&
        !options.force) {
        note(session, "  " + editor.name + ": cp3 " +
                          editor.extension.version + " is newer than bundled " +
                          session.vsix.version +
                          "; not downgrading (use --force)");
        tally.already = true;
        return kExitSuccess;
    }

    const std::vector<std::string> command = editorCommand(
        editor.cli,
        {"--install-extension", session.vsix.path.string(), "--force"});
    report.command = command;
    if (options.dryRun) {
        note(session, "  " + editor.name + ": would run " +
                          describeCommand(command));
        tally.planned = true;
        return kExitSuccess;
    }

    note(session, "  " + editor.name + ": installing cp3 " +
                      session.vsix.version + " ...");
    const int rc = runEditorCommand(command);
    if (rc != 0) {
        std::fprintf(stderr, "cp3c: %s: editor CLI failed (exit %d)\n",
                     editor.name.c_str(), rc);
        tally.failed = true;
        return kExitCompileError;
    }

    EditorExtension after;
    if (!editor.extensionsDir.empty()) {
        tryReadEditorExtension(editor.extensionsDir, session.vsix.version, after);
    } else {
        after.state = EditorState::Unknown;
    }
    editor.extension = after;
    if (!after.installed || after.version.empty()) {
        note(session, "  " + editor.name +
                          ": command succeeded; the extension is not visible in "
                          "the extensions directory yet; reload the editor");
    }
    tally.changed = true;
    tally.updated = wasInstalled;
    report.result = wasInstalled ? "updated" : "installed";
    note(session, "  " + editor.name + ": " +
                      (wasInstalled ? "updated" : "installed") + " cp3 " +
                      (after.version.empty() ? session.vsix.version
                                             : after.version));
    return kExitSuccess;
}

int uninstallOne(EditorSession& session, EditorInfo& editor,
                 EditorActionReport& report, UninstallTally& tally) {
    const EditorOptions& options = session.opt;
    if (!editor.extension.installed) {
        note(session, "  " + editor.name + ": cp3 extension not installed");
        tally.absent = true;
        return kExitSuccess;
    }
    if (!editor.cliFound || !editor.installable) {
        const std::string hint =
            editor.hint.empty() ? "extension CLI unavailable" : editor.hint;
        std::fprintf(stderr, "cp3c: %s: %s\n", editor.name.c_str(),
                     hint.c_str());
        tally.failed = true;
        return kExitCompileError;
    }

    const std::vector<std::string> command =
        editorCommand(editor.cli, {"--uninstall-extension", editorExtensionId()});
    report.command = command;
    if (options.dryRun) {
        note(session, "  " + editor.name + ": would run " +
                          describeCommand(command));
        tally.planned = true;
        return kExitSuccess;
    }

    note(session, "  " + editor.name + ": uninstalling cp3 ...");
    const int rc = runEditorCommand(command);
    if (rc != 0) {
        std::fprintf(stderr, "cp3c: %s: editor CLI failed (exit %d)\n",
                     editor.name.c_str(), rc);
        tally.failed = true;
        return kExitCompileError;
    }
    EditorExtension after;
    if (!editor.extensionsDir.empty()) {
        tryReadEditorExtension(editor.extensionsDir, session.vsix.version, after);
    } else {
        after.state = EditorState::Unknown;
    }
    editor.extension = after;
    if (after.installed) {
        note(session, "  " + editor.name +
                          ": command succeeded; the extension is still visible; "
                          "reload the editor");
    } else {
        note(session, "  " + editor.name + ": uninstalled cp3");
    }
    tally.removed = true;
    report.result = "uninstalled";
    return kExitSuccess;
}

bool confirmAll(const EditorSession& session, const std::string& question) {
    if (session.opt.editor != "all" || session.opt.yes) return true;
    if (!(session.opt.interactive || isTty(stdin))) {
        std::fputs("cp3c: --editor all needs --yes when stdin is not a "
                   "terminal\n", stderr);
        return false;
    }
    return promptYesNo(question, false);
}

}  // namespace

int runEditorInstall(EditorSession& session, EditorActionReport& report) {
    const EditorOptions& options = session.opt;
    if (!confirmAll(session, "Install the cp3 extension in every detected editor?")) {
        if (!(options.interactive || isTty(stdin)) && options.editor == "all") {
            return kExitUsageError;
        }
        note(session, "cancelled");
        report.result = "ok";
        if (options.json) emitEditorJson(session, report);
        return kExitSuccess;
    }

    InstallTally tally;
    for (EditorInfo& editor : session.editors) {
        installOne(session, editor, report, tally);
    }
    if (tally.failed) {
        return failEditorAction(session, report,
                                "one or more editors could not install");
    }
    if (options.dryRun) {
        report.result = "ok";
    } else if (tally.changed) {
        report.result = tally.updated ? "updated" : "installed";
    } else if (tally.already || tally.planned) {
        report.result = "already-installed";
    } else {
        report.result = "ok";
    }
    if (options.json) emitEditorJson(session, report);
    return kExitSuccess;
}

int runEditorUninstall(EditorSession& session, EditorActionReport& report) {
    const EditorOptions& options = session.opt;
    if (!confirmAll(session,
                    "Uninstall the cp3 extension from every detected editor?")) {
        if (!(options.interactive || isTty(stdin)) && options.editor == "all") {
            return kExitUsageError;
        }
        note(session, "cancelled");
        report.result = "ok";
        if (options.json) emitEditorJson(session, report);
        return kExitSuccess;
    }

    UninstallTally tally;
    for (EditorInfo& editor : session.editors) {
        uninstallOne(session, editor, report, tally);
    }
    if (tally.failed) {
        return failEditorAction(session, report,
                                "one or more editors could not uninstall");
    }
    if (options.dryRun) {
        report.result = "ok";
    } else if (tally.removed) {
        report.result = "uninstalled";
    } else {
        report.result = "ok";
        note(session, "cp3 extension not installed in the selected editor(s)");
    }
    if (options.json) emitEditorJson(session, report);
    return kExitSuccess;
}

}  // namespace cp3::cli
