// =============================================================================
// src/cli/src/commands/editor_detect.cpp - editor discovery + extension status.
//
// OWNERSHIP: cli module (src/cli/**). Implements the detection half of
// detail/editor.hpp for `cp3c editor-plugin`. The extension directory is the
// source of truth; the CLI is only probed for extension-management support.
// =============================================================================
#include "detail/editor.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "detail/json.hpp"
#include "detail/semver.hpp"

namespace cp3::cli {

namespace {

struct EditorSpec {
    std::string id;
    std::string name;
    std::string cliName;
    fs::path extensionsDir;
    std::vector<fs::path> cliCandidates;
    std::vector<fs::path> appMarkers;
    bool probeCli = false;
};

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

fs::path envPath(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return {};
    const std::string text = stripSurroundingQuotes(value);
    return text.empty() ? fs::path() : fs::path(text);
}

fs::path homeDirectory() {
#if defined(_WIN32)
    fs::path home = envPath("USERPROFILE");
    if (home.empty()) {
        const fs::path drive = envPath("HOMEDRIVE");
        const fs::path tail = envPath("HOMEPATH");
        if (!drive.empty() && !tail.empty()) home = drive / tail;
    }
    return home;
#else
    return envPath("HOME");
#endif
}

fs::path extensionDir(const fs::path& home, const char* relative) {
    if (home.empty()) return {};
    return home / fs::path(relative);
}

std::string absolutePath(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) return path.string();
    return absolute.lexically_normal().string();
}

EditorSpec makeSpec(const char* id, const char* name, const char* cli) {
    EditorSpec spec;
    spec.id = id;
    spec.name = name;
    spec.cliName = cli;
    return spec;
}

void addIfFile(std::vector<fs::path>& out, const fs::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) out.push_back(path);
}

void addIfDir(std::vector<fs::path>& out, const fs::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    if (fs::is_directory(path, ec)) out.push_back(path);
}

// Bounded scan of %LOCALAPPDATA%\Programs: one directory level whose name
// contains "antigravity". The app directory itself is the detection marker.
void scanAntigravity(const fs::path& programs, EditorSpec& spec) {
    std::error_code ec;
    fs::directory_iterator it(programs, ec);
    const fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = lowerAscii(it->path().filename().string());
        if (name.find("antigravity") == std::string::npos) continue;
        addIfDir(spec.appMarkers, it->path());
        for (const char* rel : {"antigravity.cmd", "antigravity.exe",
                                "bin/antigravity.cmd", "bin/antigravity.exe",
                                "resources/app/bin/antigravity.cmd"}) {
            addIfFile(spec.cliCandidates, it->path() / rel);
        }
    }
}

void addWindowsCandidates(EditorSpec& spec) {
    const fs::path local = envPath("LOCALAPPDATA");
    const fs::path programs = envPath("ProgramFiles");
    const fs::path programsX86 = envPath("ProgramFiles(x86)");
    if (spec.id == "code") {
        if (!local.empty()) {
            addIfFile(spec.cliCandidates,
                      local / "Programs/Microsoft VS Code/bin/code.cmd");
            addIfFile(spec.appMarkers,
                      local / "Programs/Microsoft VS Code/Code.exe");
        }
        for (const fs::path& base : {programs, programsX86}) {
            if (base.empty()) continue;
            addIfFile(spec.cliCandidates, base / "Microsoft VS Code/bin/code.cmd");
            addIfFile(spec.appMarkers, base / "Microsoft VS Code/Code.exe");
        }
    } else if (spec.id == "code-insiders") {
        if (!local.empty()) {
            addIfFile(spec.cliCandidates,
                      local / "Programs/Microsoft VS Code Insiders/bin/code-insiders.cmd");
            addIfFile(spec.appMarkers,
                      local / "Programs/Microsoft VS Code Insiders/Code - Insiders.exe");
        }
        for (const fs::path& base : {programs, programsX86}) {
            if (base.empty()) continue;
            addIfFile(spec.cliCandidates,
                      base / "Microsoft VS Code Insiders/bin/code-insiders.cmd");
        }
    } else if (spec.id == "cursor") {
        if (!local.empty()) {
            addIfFile(spec.cliCandidates,
                      local / "Programs/Cursor/resources/app/bin/cursor.cmd");
            addIfFile(spec.appMarkers, local / "Programs/Cursor/Cursor.exe");
        }
        for (const fs::path& base : {programs, programsX86}) {
            if (base.empty()) continue;
            addIfFile(spec.cliCandidates, base / "Cursor/resources/app/bin/cursor.cmd");
            addIfFile(spec.appMarkers, base / "Cursor/Cursor.exe");
        }
    } else if (spec.id == "codium") {
        if (!local.empty()) {
            addIfFile(spec.cliCandidates, local / "Programs/VSCodium/bin/codium.cmd");
            addIfFile(spec.appMarkers, local / "Programs/VSCodium/VSCodium.exe");
        }
        for (const fs::path& base : {programs, programsX86}) {
            if (base.empty()) continue;
            addIfFile(spec.cliCandidates, base / "VSCodium/bin/codium.cmd");
            addIfFile(spec.appMarkers, base / "VSCodium/VSCodium.exe");
        }
    } else if (spec.id == "antigravity") {
        if (!local.empty()) {
            addIfFile(spec.cliCandidates,
                      local / "Programs/Antigravity/bin/antigravity.cmd");
            scanAntigravity(local / "Programs", spec);
        }
        for (const fs::path& base : {programs, programsX86}) {
            if (base.empty()) continue;
            addIfFile(spec.cliCandidates, base / "Antigravity/bin/antigravity.cmd");
            addIfDir(spec.appMarkers, base / "Antigravity");
        }
    }
}

#if !defined(_WIN32)
void addUnixCandidates(EditorSpec& spec) {
#if defined(__APPLE__)
    const std::string app = "/Applications/";
    if (spec.id == "code") {
        spec.cliCandidates.emplace_back(
            app + "Visual Studio Code.app/Contents/Resources/app/bin/code");
        spec.appMarkers.emplace_back(app + "Visual Studio Code.app");
    } else if (spec.id == "code-insiders") {
        spec.cliCandidates.emplace_back(
            app + "Visual Studio Code - Insiders.app/Contents/Resources/app/bin/code-insiders");
        spec.appMarkers.emplace_back(app + "Visual Studio Code - Insiders.app");
    } else if (spec.id == "cursor") {
        spec.cliCandidates.emplace_back(
            app + "Cursor.app/Contents/Resources/app/bin/cursor");
        spec.appMarkers.emplace_back(app + "Cursor.app");
    } else if (spec.id == "codium") {
        spec.cliCandidates.emplace_back(
            app + "VSCodium.app/Contents/Resources/app/bin/codium");
        spec.appMarkers.emplace_back(app + "VSCodium.app");
    } else if (spec.id == "antigravity") {
        spec.cliCandidates.emplace_back(
            app + "Antigravity.app/Contents/Resources/app/bin/antigravity");
        spec.appMarkers.emplace_back(app + "Antigravity.app");
    }
#endif
    for (const char* path : {"/usr/bin", "/usr/local/bin", "/opt/homebrew/bin",
                             "/snap/bin"}) {
        spec.cliCandidates.emplace_back(fs::path(path) / spec.cliName);
    }
    if (spec.id == "code") {
        spec.cliCandidates.emplace_back("/usr/share/code/bin/code");
    } else if (spec.id == "code-insiders") {
        spec.cliCandidates.emplace_back("/usr/share/code-insiders/bin/code-insiders");
    } else if (spec.id == "codium") {
        spec.cliCandidates.emplace_back("/usr/share/codium/bin/codium");
    }
}
#endif  // !defined(_WIN32)

std::vector<EditorSpec> editorSpecs() {
    const fs::path home = homeDirectory();
    std::vector<EditorSpec> specs;
    specs.push_back(makeSpec("code", "Visual Studio Code", "code"));
    specs.push_back(makeSpec("code-insiders", "VS Code Insiders",
                             "code-insiders"));
    specs.push_back(makeSpec("cursor", "Cursor", "cursor"));
    specs.push_back(makeSpec("antigravity", "Antigravity", "antigravity"));
    specs.push_back(makeSpec("codium", "VSCodium", "codium"));

    specs[0].extensionsDir = extensionDir(home, ".vscode/extensions");
    specs[1].extensionsDir = extensionDir(home, ".vscode-insiders/extensions");
    specs[2].extensionsDir = extensionDir(home, ".cursor/extensions");
    specs[3].extensionsDir = extensionDir(home, ".antigravity/extensions");
    specs[4].extensionsDir = extensionDir(home, ".vscode-oss/extensions");
    for (EditorSpec& spec : specs) {
#if defined(_WIN32)
        addWindowsCandidates(spec);
#else
        addUnixCandidates(spec);
#endif
    }
    specs[3].probeCli = true;  // Antigravity: do not assume VS Code CLI flags.
    return specs;
}

bool anyExists(const std::vector<fs::path>& paths) {
    for (const fs::path& path : paths) {
        std::error_code ec;
        if (fs::exists(path, ec)) return true;
    }
    return false;
}

std::string findCli(const EditorSpec& spec) {
    std::string found;
    if (findExecutable(spec.cliName, found)) return absolutePath(found);
    std::error_code ec;
    for (const fs::path& candidate : spec.cliCandidates) {
        if (fs::is_regular_file(candidate, ec)) return absolutePath(candidate);
    }
    return {};
}

bool probeSupportsExtensions(const std::string& cli) {
    return runProcessQuiet(editorCommand(cli, {"--list-extensions"})) == 0;
}

bool versionNewer(const std::string& candidate, const std::string& current) {
    const SemVer a = parseSemVer(candidate);
    const SemVer b = parseSemVer(current);
    if (a.valid && b.valid) return compareSemVer(a, b) > 0;
    if (a.valid != b.valid) return a.valid;
    return candidate > current;
}

bool isPlatformSuffix(const std::string& tail) {
    for (const char* prefix : {"win32", "linux", "darwin", "alpine",
                               "universal", "web"}) {
        if (tail.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

std::string versionFromDirectoryName(const std::string& name) {
    const std::string prefix = "cp3.cp3-vscode-";
    if (name.rfind(prefix, 0) != 0) return {};
    std::string rest = name.substr(prefix.size());
    const std::size_t dash = rest.find('-');
    if (dash != std::string::npos) {
        const std::string core = rest.substr(0, dash);
        if (parseSemVer(core).valid &&
            isPlatformSuffix(lowerAscii(rest.substr(dash + 1)))) {
            rest = core;
        }
    }
    return rest;
}

void applyExtensionState(EditorInfo& info, const std::string& bundled) {
    if (info.extensionsDir.empty()) {
        info.extension.state = EditorState::Unknown;
    } else {
        tryReadEditorExtension(info.extensionsDir, bundled, info.extension);
    }
    if (!info.cliFound) info.extension.state = EditorState::CliMissing;
}

std::string hintFor(const EditorInfo& info) {
    if (!info.cliFound) {
        return "editor CLI not found; use export and the editor's Extensions view";
    }
    if (!info.installable) {
        return "extension CLI unavailable; use export and the editor's Extensions view";
    }
    if (info.extensionsDir.empty()) {
        return "pass --extensions-dir to read the extension status";
    }
    return {};
}

}  // namespace

const char* editorStateName(EditorState state) noexcept {
    switch (state) {
        case EditorState::Current: return "current";
        case EditorState::UpdateAvailable: return "update-available";
        case EditorState::NewerInstalled: return "newer-installed";
        case EditorState::NotInstalled: return "not-installed";
        case EditorState::CliMissing: return "cli-missing";
        case EditorState::Unknown: return "unknown";
    }
    return "unknown";
}

const char* editorExtensionId() noexcept { return "cp3.cp3-vscode"; }

std::vector<std::string> knownEditorIds() {
    return {"code", "code-insiders", "cursor", "antigravity", "codium"};
}

bool editorIdIsKnown(const std::string& id) {
    const std::vector<std::string> ids = knownEditorIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string extensionVersionFromDirectory(const fs::path& dir) {
    std::string text;
    if (readTextFile(dir / "package.json", text)) {
        JsonValue manifest;
        std::string err;
        if (JsonValue::parse(text, manifest, err)) {
            const std::string* version = manifest.stringField("version");
            if (version != nullptr && !version->empty()) return *version;
        }
    }
    return versionFromDirectoryName(dir.filename().string());
}

bool tryReadEditorExtension(const fs::path& extensionsDir,
                            const std::string& bundledVersion,
                            EditorExtension& out) {
    out = EditorExtension{};
    if (extensionsDir.empty()) {
        out.state = EditorState::Unknown;
        return true;
    }
    std::error_code ec;
    if (!fs::is_directory(extensionsDir, ec)) {
        out.state = EditorState::NotInstalled;
        return true;
    }

    bool sawDirectory = false;
    bool sawVersion = false;
    std::string best;
    const fs::directory_iterator end;
    for (fs::directory_iterator it(extensionsDir, ec); it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.rfind("cp3.cp3-vscode-", 0) != 0) continue;
        std::error_code dirEc;
        if (!it->is_directory(dirEc)) continue;
        sawDirectory = true;
        const std::string version = extensionVersionFromDirectory(it->path());
        if (version.empty()) continue;
        if (!sawVersion || versionNewer(version, best)) {
            best = version;
            sawVersion = true;
        }
    }
    if (!sawVersion) {
        out.installed = sawDirectory;
        out.state = sawDirectory ? EditorState::Unknown : EditorState::NotInstalled;
        return true;
    }

    out.installed = true;
    out.version = best;
    const SemVer installed = parseSemVer(best);
    const SemVer bundled = parseSemVer(bundledVersion);
    if (!installed.valid || !bundled.valid) {
        out.state = EditorState::Unknown;
        return true;
    }
    const int order = compareSemVer(installed, bundled);
    out.state = order == 0 ? EditorState::Current
                           : (order < 0 ? EditorState::UpdateAvailable
                                        : EditorState::NewerInstalled);
    return true;
}

std::vector<EditorInfo> detectEditors(const std::string& bundledVersion,
                                      const std::string& requestedId,
                                      const std::string& extensionsDirOverride) {
    std::vector<EditorInfo> out;
    for (const EditorSpec& spec : editorSpecs()) {
        EditorInfo info;
        info.id = spec.id;
        info.name = spec.name;
        info.cli = findCli(spec);
        info.cliFound = !info.cli.empty();

        fs::path extensions = spec.extensionsDir;
        if (!extensionsDirOverride.empty() && requestedId == spec.id) {
            extensions = extensionsDirOverride;
        }
        std::error_code ec;
        const bool dirExists =
            !extensions.empty() && fs::is_directory(extensions, ec);
        info.extensionsDir = extensions.empty() ? "" : absolutePath(extensions);
        info.detected = info.cliFound || anyExists(spec.appMarkers) || dirExists;
        info.installable =
            info.cliFound && (!spec.probeCli || probeSupportsExtensions(info.cli));
        applyExtensionState(info, bundledVersion);
        info.hint = hintFor(info);
        if (info.detected) out.push_back(std::move(info));
    }
    return out;
}

EditorInfo customEditor(const std::string& cliPath,
                        const std::string& extensionsDir,
                        const std::string& bundledVersion) {
    EditorInfo info;
    info.id = "custom";
    info.name = "Custom editor";
    info.cli = absolutePath(cliPath);
    info.cliFound = true;
    info.detected = true;
    info.installable = probeSupportsExtensions(info.cli);
    // A user-supplied path to a known editor CLI is still that editor. Match
    // the executable stem so `--editor C:\...\code.cmd` reports and probes as
    // Visual Studio Code rather than an anonymous custom tool.
    const std::string stem = lowerAscii(fs::path(cliPath).stem().string());
    for (const EditorSpec& spec : editorSpecs()) {
        if (lowerAscii(spec.cliName) != stem) continue;
        info.id = spec.id;
        info.name = spec.name;
        if (extensionsDir.empty() && !spec.extensionsDir.empty()) {
            info.extensionsDir = absolutePath(spec.extensionsDir);
        }
        break;
    }
    if (!extensionsDir.empty()) {
        info.extensionsDir = absolutePath(extensionsDir);
    }
    applyExtensionState(info, bundledVersion);
    info.hint = hintFor(info);
    return info;
}

}  // namespace cp3::cli
