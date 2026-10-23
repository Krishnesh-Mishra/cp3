// =============================================================================
// src/cli/src/commands/editor_plugin.cpp - `cp3c editor-plugin` command word.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdEditorPlugin
// (detail/commands.hpp), the shared option-parser hooks, and the action
// dispatch. Action bodies live in editor_actions.cpp / editor_install.cpp;
// detection, VSIX handling and process launching live in their own units.
// =============================================================================
#include "detail/editor/editor_plugin.hpp"

#include <cstdio>
#include <string>

#include "cp3/cli.hpp"

#include "detail/core/commands.hpp"

namespace cp3::cli {

bool matchEditorFlag(const std::string& token, CliOptions& opt) {
    if (token == "--interactive") { opt.interactive = true; return true; }
    if (token == "--json") { opt.json = true; return true; }
    if (token == "--dry-run") { opt.dryRun = true; return true; }
    if (token == "--force") { opt.force = true; return true; }
    return false;
}

bool matchEditorValueOption(const std::vector<std::string>& tokens,
                            std::size_t& i, CliOptions& opt,
                            std::string& err) {
    struct ValueOption {
        const char* name;
        std::string* target;
    };
    const ValueOption options[] = {
        {"--editor", &opt.editor},
        {"--vsix", &opt.vsix},
        {"--extensions-dir", &opt.extensionsDir},
        {"--out", &opt.out},
    };
    const std::string& token = tokens[i];
    for (const ValueOption& option : options) {
        const std::string name(option.name);
        if (token == name) {
            if (i + 1 >= tokens.size()) {
                err = name + " requires a value";
                return true;
            }
            *option.target = tokens[++i];
            if (option.target->empty()) err = name + " requires a non-empty value";
            return true;
        }
        const std::string prefix = name + "=";
        if (startsWith(token, prefix)) {
            *option.target = token.substr(prefix.size());
            if (option.target->empty()) err = name + " requires a non-empty value";
            return true;
        }
    }
    return false;
}

namespace {

const char* actionName(EditorAction action) {
    switch (action) {
        case EditorAction::Status: return "status";
        case EditorAction::Install: return "install";
        case EditorAction::Uninstall: return "uninstall";
        case EditorAction::Export: return "export";
        case EditorAction::Path: return "path";
    }
    return "status";
}

void printPluginUsage(std::FILE* out) {
    std::fputs(
        "usage: cp3c editor-plugin [status|install|uninstall|export|path] "
        "[options]\n"
        "       cp3c editor-plugins [status|install|uninstall|export|path] "
        "[options]\n"
        "\n"
        "  status      list detected editors and cp3 extension state (default)\n"
        "  install     install the bundled VSIX only when needed\n"
        "  uninstall   remove the cp3 extension from an editor\n"
        "  export      copy the bundled VSIX for sideloading\n"
        "  path        print the bundled VSIX path\n"
        "\n"
        "options:\n"
        "  --editor <id|path>     code, code-insiders, cursor, antigravity,\n"
        "                         codium, all, or a custom editor CLI path\n"
        "  --vsix <path>          install a specific VSIX instead of the bundled one\n"
        "  --extensions-dir <dir> status directory override (one editor only)\n"
        "  --out <file-or-dir>    export destination\n"
        "  --force                reinstall, downgrade, or overwrite an export\n"
        "  --yes, -y              skip the --editor all confirmation\n"
        "  --dry-run              print the editor command, do not run it\n"
        "  --interactive          force the wizard even when stdin is not a TTY\n"
        "  --json                 stable machine output on stdout\n",
        out);
}

bool hasFlagConflict(const EditorOptions& options, std::string& err) {
    if (!options.out.empty() && options.action != EditorAction::Export) {
        err = "--out is only valid with export";
        return true;
    }
    if (!options.vsix.empty() && options.action != EditorAction::Install) {
        err = "--vsix is only valid with install";
        return true;
    }
    if (options.dryRun && options.action != EditorAction::Install &&
        options.action != EditorAction::Uninstall) {
        err = "--dry-run is only valid with install or uninstall";
        return true;
    }
    if (!options.extensionsDir.empty() &&
        (options.editor.empty() || options.editor == "all")) {
        err = "--extensions-dir needs exactly one editor";
        return true;
    }
    if (!options.extensionsDir.empty() &&
        options.action != EditorAction::Status &&
        options.action != EditorAction::Install &&
        options.action != EditorAction::Uninstall) {
        err = "--extensions-dir is not valid with this action";
        return true;
    }
    return false;
}

}  // namespace

int runEditorAction(const EditorOptions& options) {
    EditorSession session;
    session.opt = options;
    EditorActionReport report;
    report.action = actionName(options.action);
    report.editor = options.editor;

    if (options.action == EditorAction::Path ||
        options.action == EditorAction::Export) {
        std::string err;
        if (!prepareEditorVsix(session, true, err)) {
            return failEditorAction(session, report, err);
        }
        return options.action == EditorAction::Path
                   ? runEditorPath(session, report)
                   : runEditorExport(session, report);
    }

    const bool vsixRequired = options.action == EditorAction::Install;
    std::string vsixError;
    if (!prepareEditorVsix(session, vsixRequired, vsixError)) {
        return failEditorAction(session, report, vsixError);
    }

    const EditorPrepareResult prepared = prepareEditorSession(session);
    if (!prepared.ok) {
        if (prepared.exitCode == kExitUsageError) {
            std::fprintf(stderr, "cp3c: %s\n", prepared.error.c_str());
            printPluginUsage(stderr);
            return kExitUsageError;
        }
        return failEditorAction(session, report, prepared.error);
    }

    if (report.editor.empty() && session.editors.size() == 1) {
        report.editor = session.editors.front().id;
    }
    if (options.action == EditorAction::Status) {
        return runEditorStatus(session, report);
    }
    if (options.action == EditorAction::Install) {
        return runEditorInstall(session, report);
    }
    return runEditorUninstall(session, report);
}

int cmdEditorPlugin(const CliOptions& opt) {
    EditorOptions options;
    options.json = opt.json;
    options.dryRun = opt.dryRun;
    options.force = opt.force;
    options.yes = opt.yes;
    options.interactive = opt.interactive;
    options.editor = opt.editor;
    options.vsix = opt.vsix;
    options.extensionsDir = opt.extensionsDir;
    options.out = opt.out;

    if (opt.positionals.empty()) {
        if (opt.interactive || isTty(stdin)) return runEditorWizard(options);
        options.action = EditorAction::Status;
        return runEditorAction(options);
    }
    if (opt.positionals.size() > 1) {
        std::fprintf(stderr, "cp3c: unexpected argument '%s'\n",
                     opt.positionals[1].c_str());
        printPluginUsage(stderr);
        return kExitUsageError;
    }

    const std::string& action = opt.positionals[0];
    if (action == "status") {
        options.action = EditorAction::Status;
    } else if (action == "install") {
        options.action = EditorAction::Install;
    } else if (action == "uninstall") {
        options.action = EditorAction::Uninstall;
    } else if (action == "export") {
        options.action = EditorAction::Export;
    } else if (action == "path") {
        options.action = EditorAction::Path;
    } else {
        std::fprintf(stderr, "cp3c: unknown editor-plugin action '%s'\n",
                     action.c_str());
        printPluginUsage(stderr);
        return kExitUsageError;
    }

    std::string conflict;
    if (hasFlagConflict(options, conflict)) {
        std::fprintf(stderr, "cp3c: %s\n", conflict.c_str());
        printPluginUsage(stderr);
        return kExitUsageError;
    }
    return runEditorAction(options);
}

}  // namespace cp3::cli
