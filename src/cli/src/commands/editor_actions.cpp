// =============================================================================
// src/cli/src/commands/editor_actions.cpp - status/export/path + JSON output.
//
// OWNERSHIP: cli module (src/cli/**). Implements the shared EditorSession
// preparation and the read-only / export actions of `cp3c editor-plugin`.
// =============================================================================
#include "detail/editor_plugin.hpp"

#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/cli.hpp"

#include "detail/editor_prompt.hpp"
#include "detail/json.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

void note(const EditorSession& session, const std::string& text) {
    if (!session.opt.json) std::fprintf(stdout, "%s\n", text.c_str());
}

namespace {

std::string stringArray(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += jsonQuote(values[i]);
    }
    out += "]";
    return out;
}

void appendEditorJson(std::string& out, const EditorInfo& editor) {
    out += "{";
    out += "\"id\": " + jsonQuote(editor.id) + ", ";
    out += "\"name\": " + jsonQuote(editor.name) + ", ";
    out += "\"cli\": " + jsonQuote(editor.cli) + ", ";
    out += "\"extensionsDir\": " + jsonQuote(editor.extensionsDir) + ", ";
    out += "\"installed\": ";
    out += editor.extension.installed ? "true" : "false";
    out += ", \"version\": " + jsonQuote(editor.extension.version) + ", ";
    out += "\"state\": " + jsonQuote(editorStateName(editor.extension.state)) + ", ";
    out += "\"installable\": ";
    out += editor.installable ? "true" : "false";
    out += ", \"hint\": " + jsonQuote(editor.hint);
    out += "}";
}

const char* stateLabel(EditorState state) {
    switch (state) {
        case EditorState::Current: return "current";
        case EditorState::UpdateAvailable: return "update available";
        case EditorState::NewerInstalled: return "newer than bundled";
        case EditorState::NotInstalled: return "not installed";
        case EditorState::CliMissing: return "cli missing";
        case EditorState::Unknown: return "unknown";
    }
    return "unknown";
}

std::string summaryFor(const EditorInfo& editor) {
    const EditorExtension& extension = editor.extension;
    if (!editor.cliFound) {
        if (extension.installed) {
            return "cp3 " + extension.version + " installed (cli missing)";
        }
        return "app found, extension CLI unavailable";
    }
    if (extension.installed) {
        return "cp3 " + extension.version + " installed (" +
               stateLabel(extension.state) + ")";
    }
    if (extension.state == EditorState::Unknown) {
        return "cp3 extension status unknown";
    }
    return "cp3 extension not installed";
}

bool isCustomSelection(const EditorOptions& options) {
    return !options.editor.empty() && options.editor != "all" &&
           !editorIdIsKnown(options.editor);
}

std::vector<EditorInfo> detectedEditors(EditorSession& session) {
    const EditorOptions& options = session.opt;
    const bool custom = isCustomSelection(options);
    const std::string requested = custom ? std::string() : options.editor;
    const std::string overrideDir =
        requested.empty() ? std::string() : options.extensionsDir;
    std::vector<EditorInfo> editors =
        detectEditors(session.vsix.version, requested, overrideDir);
    if (!custom) return editors;

    std::error_code ec;
    if (!fs::is_regular_file(fs::path(options.editor), ec)) return editors;
    editors.push_back(
        customEditor(options.editor, options.extensionsDir, session.vsix.version));
    return editors;
}

std::vector<EditorInfo> selectCandidates(const EditorOptions& options,
                                         const std::vector<EditorInfo>& detected,
                                         bool custom) {
    std::vector<EditorInfo> selected;
    if (options.editor.empty() || options.editor == "all") {
        return detected;
    }
    if (custom && !detected.empty()) {
        selected.push_back(detected.back());
        return selected;
    }
    for (const EditorInfo& editor : detected) {
        if (editor.id == options.editor) selected.push_back(editor);
    }
    return selected;
}

std::vector<EditorInfo> usableOnly(const std::vector<EditorInfo>& selected) {
    std::vector<EditorInfo> usable;
    for (const EditorInfo& editor : selected) {
        if (editor.cliFound && editor.installable) usable.push_back(editor);
    }
    return usable;
}

}  // namespace

void emitEditorJson(const EditorSession& session,
                    const EditorActionReport& report) {
    std::string out = "{\n";
    out += "  \"ok\": ";
    out += report.ok ? "true" : "false";
    out += ",\n  \"action\": " + jsonQuote(report.action) + ",\n";
    out += "  \"vsix\": {";
    out += "\"path\": " + jsonQuote(session.vsix.path.string()) + ", ";
    out += "\"version\": " + jsonQuote(session.vsix.version) + ", ";
    out += "\"sha256\": " + jsonQuote(session.vsix.sha256) + "},\n";
    out += "  \"editors\": [";
    for (std::size_t i = 0; i < session.editors.size(); ++i) {
        if (i != 0) out += ", ";
        appendEditorJson(out, session.editors[i]);
    }
    out += "],\n";
    out += "  \"result\": " + jsonQuote(report.result) + ",\n";
    out += "  \"editor\": " + jsonQuote(report.editor) + ",\n";
    out += "  \"command\": " + stringArray(report.command);
    if (!report.destination.empty()) {
        out += ",\n  \"destination\": " + jsonQuote(report.destination);
    }
    if (!report.sha256.empty()) {
        out += ",\n  \"sha256\": " + jsonQuote(report.sha256);
    }
    if (!report.error.empty()) {
        out += ",\n  \"error\": " + jsonQuote(report.error);
    }
    out += "\n}\n";
    std::fputs(out.c_str(), stdout);
}

int failEditorAction(const EditorSession& session, EditorActionReport& report,
                     const std::string& text) {
    report.ok = false;
    report.result = "failed";
    report.error = text;
    std::fprintf(stderr, "cp3c: %s\n", text.c_str());
    if (session.opt.json) emitEditorJson(session, report);
    return kExitCompileError;
}

void printEditorSummary(const std::vector<EditorInfo>& editors, bool details) {
    std::fputs("cp3 editor support\n\n", stdout);
    if (editors.empty()) {
        std::fputs("  no supported editors detected\n", stdout);
        return;
    }
    for (std::size_t i = 0; i < editors.size(); ++i) {
        const EditorInfo& editor = editors[i];
        std::fprintf(stdout, "  [%zu] %-22s %s\n", i + 1, editor.name.c_str(),
                     summaryFor(editor).c_str());
        if (!details) continue;
        std::fprintf(stdout, "      cli: %s\n",
                     editor.cli.empty() ? "not found" : editor.cli.c_str());
        std::fprintf(stdout, "      extensions: %s\n",
                     editor.extensionsDir.empty() ? "unknown"
                                                  : editor.extensionsDir.c_str());
        if (!editor.hint.empty()) {
            std::fprintf(stdout, "      hint: %s\n", editor.hint.c_str());
        }
    }
}

bool prepareEditorVsix(EditorSession& session, bool required, std::string& err) {
    if (session.opt.action == EditorAction::Install &&
        !session.opt.vsix.empty()) {
        if (!inspectVsix(session.opt.vsix, session.vsix, err)) return false;
    } else {
        session.vsix = findBundledVsix();
    }
    if (required && !session.vsix.found) {
        err = "no bundled cp3 extension package found; this build was packaged "
              "without one (build the VSIX in extensions/cp3-vscode first)";
        return false;
    }
    return true;
}

EditorPrepareResult prepareEditorSession(EditorSession& session) {
    EditorPrepareResult result;
    const EditorOptions& options = session.opt;
    const bool custom = isCustomSelection(options);
    const std::vector<EditorInfo> detected = detectedEditors(session);

    std::error_code fileEc;
    if (custom && !fs::is_regular_file(fs::path(options.editor), fileEc)) {
        result.exitCode = kExitUsageError;
        result.error = "editor CLI not found: " + options.editor;
        return result;
    }

    std::vector<EditorInfo> selected = selectCandidates(options, detected, custom);
    if (options.editor.empty() && options.action != EditorAction::Status) {
        if (selected.empty()) {
            result.exitCode = kExitUsageError;
            result.error = "no editor selected; known ids: code, code-insiders, "
                           "cursor, antigravity, codium";
            return result;
        }
        if (!(options.interactive || isTty(stdin))) {
            result.exitCode = kExitUsageError;
            result.error = "choose an editor with --editor <id|path>; known ids: "
                           "code, code-insiders, cursor, antigravity, codium";
            return result;
        }
        EditorInfo chosen;
        if (!promptEditorChoice(selected, chosen)) {
            result.exitCode = kExitUsageError;
            result.error = "no editor selected";
            return result;
        }
        selected.clear();
        selected.push_back(chosen);
    }

    if (selected.empty() && !options.editor.empty() &&
        options.editor != "all") {
        result.exitCode = kExitCompileError;
        result.error = "editor '" + options.editor + "' was not detected";
        return result;
    }
    if (options.editor == "all" && options.action != EditorAction::Status) {
        selected = usableOnly(selected);
        if (selected.empty()) {
            result.exitCode = kExitCompileError;
            result.error = "no detected editor exposes a usable extension CLI";
            return result;
        }
    }

    session.editors = selected;
    result.ok = true;
    return result;
}

int runEditorStatus(EditorSession& session, EditorActionReport& report) {
    if (!session.opt.json) {
        if (!session.vsix.found) {
            std::fputs("cp3c: no bundled cp3 extension package found; installed "
                       "versions are reported as unknown\n", stderr);
        }
        printEditorSummary(session.editors, true);
    }
    report.result = "ok";
    if (session.opt.json) emitEditorJson(session, report);
    return kExitSuccess;
}

int runEditorExport(EditorSession& session, EditorActionReport& report) {
    if (!session.vsix.found) {
        return failEditorAction(session, report,
                                "no bundled cp3 extension package found; this "
                                "build was packaged without one");
    }
    const fs::path out =
        session.opt.out.empty() ? fs::path() : fs::path(session.opt.out);
    fs::path destination;
    std::string err;
    if (!exportVsix(session.vsix, out, session.opt.force, destination, err)) {
        return failEditorAction(session, report, err);
    }
    std::string hash;
    if (!hashFileSha256(destination, hash, err)) {
        return failEditorAction(session, report, err);
    }
    report.result = "exported";
    report.destination = destination.string();
    report.sha256 = hash;
    note(session, "exported " + destination.string());
    note(session, "sha256 " + hash);
    if (session.opt.json) emitEditorJson(session, report);
    return kExitSuccess;
}

int runEditorPath(EditorSession& session, EditorActionReport& report) {
    if (!session.vsix.found) {
        return failEditorAction(session, report,
                                "no bundled cp3 extension package found; this "
                                "build was packaged without one");
    }
    report.result = "ok";
    if (session.opt.json) {
        emitEditorJson(session, report);
    } else {
        std::fprintf(stdout, "%s\n", session.vsix.path.string().c_str());
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
