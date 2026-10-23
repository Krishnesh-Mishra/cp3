// =============================================================================
// src/cli/src/commands/editor_wizard.cpp - `cp3c editor-plugin` wizard.
//
// OWNERSHIP: cli module (src/cli/**). Runs on a TTY, or with --interactive.
// Every action delegates to runEditorAction so the wizard and the scriptable
// paths share install/status/export/uninstall semantics.
// =============================================================================
#include "detail/editor/editor_plugin.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/cli.hpp"

#include "detail/editor/editor_prompt.hpp"

namespace cp3::cli {

namespace {

int wizardInstall(const EditorOptions& base,
                  const std::vector<EditorInfo>& editors,
                  const VsixInfo& vsix) {
    if (editors.empty()) {
        std::fputs("cp3c: no supported editors detected; choose an editor CLI "
                   "path first\n", stderr);
        return kExitCompileError;
    }
    EditorInfo chosen;
    if (!promptEditorChoice(editors, chosen)) return kExitUsageError;

    EditorOptions options = base;
    options.action = EditorAction::Install;
    options.editor = chosen.id;
    if (chosen.extension.installed &&
        chosen.extension.state == EditorState::Current && !options.force) {
        if (!promptYesNo("cp3 " + chosen.extension.version +
                             " is already installed. Reinstall?",
                         false)) {
            std::fputs("already installed\n", stdout);
            return kExitSuccess;
        }
        options.force = true;
    } else if (chosen.extension.state == EditorState::NewerInstalled &&
               !options.force) {
        if (!promptYesNo("cp3 " + chosen.extension.version +
                             " is newer than bundled " + vsix.version +
                             ". Downgrade?",
                         false)) {
            std::fputs("left the newer version installed\n", stdout);
            return kExitSuccess;
        }
        options.force = true;
    }
    return runEditorAction(options);
}

int wizardExport(const EditorOptions& base, const VsixInfo& vsix) {
    EditorOptions options = base;
    options.action = EditorAction::Export;
    options.vsix.clear();
    if (vsix.found && options.out.empty()) {
        std::error_code ec;
        const fs::path destination =
            fs::current_path(ec) / vsix.path.filename();
        if (!ec && fs::exists(destination, ec)) {
            if (!promptYesNo("Overwrite " + destination.string() + "?", false)) {
                return kExitSuccess;
            }
            options.force = true;
        }
    }
    return runEditorAction(options);
}

int wizardUninstall(const EditorOptions& base,
                    const std::vector<EditorInfo>& editors) {
    if (editors.empty()) {
        std::fputs("cp3c: no supported editors detected\n", stderr);
        return kExitCompileError;
    }
    EditorInfo chosen;
    if (!promptEditorChoice(editors, chosen)) return kExitUsageError;
    if (!chosen.extension.installed) {
        std::fputs("cp3 extension not installed\n", stdout);
        return kExitSuccess;
    }
    if (!promptYesNo("Uninstall the cp3 extension from " + chosen.name + "?",
                     false)) {
        return kExitSuccess;
    }
    EditorOptions options = base;
    options.action = EditorAction::Uninstall;
    options.editor = chosen.id;
    options.force = false;
    options.dryRun = false;
    return runEditorAction(options);
}

int wizardCustomPath(const EditorOptions& base) {
    std::string path;
    if (!readAnswer("  Editor CLI path: ", path)) return kExitUsageError;
    if (path.empty()) return kExitSuccess;
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(path), ec)) {
        std::fprintf(stderr, "cp3c: '%s' is not a file\n", path.c_str());
        return kExitUsageError;
    }
    std::string extensions;
    if (!readAnswer("  Extensions directory (optional): ", extensions)) {
        return kExitUsageError;
    }
    EditorOptions options = base;
    options.action = EditorAction::Status;
    options.editor = path;
    options.extensionsDir = extensions;
    options.json = false;
    return runEditorAction(options);
}

}  // namespace

int runEditorWizard(const EditorOptions& base) {
    const VsixInfo vsix = findBundledVsix();
    const std::vector<EditorInfo> editors =
        detectEditors(vsix.version, "", "");
    printEditorSummary(editors, false);
    std::fputs(
        "\n  What do you want to do?\n"
        "    1. Install or update the cp3 extension\n"
        "    2. Check installed status\n"
        "    3. Export the extension package (.vsix)\n"
        "    4. Uninstall the cp3 extension\n"
        "    5. Choose an editor CLI path\n"
        "    6. Quit\n",
        stdout);

    const int choice = promptMenuChoice(1, 6, "  Choice");
    if (choice < 0) return kExitUsageError;
    if (choice == 1) return wizardInstall(base, editors, vsix);
    if (choice == 2) {
        EditorOptions options = base;
        options.action = EditorAction::Status;
        options.json = false;
        return runEditorAction(options);
    }
    if (choice == 3) return wizardExport(base, vsix);
    if (choice == 4) return wizardUninstall(base, editors);
    if (choice == 5) return wizardCustomPath(base);
    return kExitSuccess;
}

}  // namespace cp3::cli
