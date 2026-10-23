// =============================================================================
// src/cli/src/toolchain/toolchain_config.cpp - saved toolchain choices.
//
// OWNERSHIP: cli module (src/cli/**). Implements the global config half and the
// scope dispatch of detail/toolchain_select.hpp. Project package.json editing
// lives in toolchain_manifest.cpp so both files stay small.
//
// Global scope is a plain key=value file (`id`, `version`, `policy`, `program`,
// `saved`), read tolerantly so a hand-edited file never crashes a build.
// CP3_CONFIG_DIR overrides the config directory for tests and CI.
// =============================================================================
#include "detail/toolchain/toolchain.hpp"
#include "detail/toolchain/toolchain_select.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string trimCopy(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string todayIso() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return buffer;
}

fs::path configDirectory() {
    if (const char* override = std::getenv("CP3_CONFIG_DIR")) {
        if (*override != '\0') return fs::path(override);
    }
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) {
        if (*appdata != '\0') return fs::path(appdata) / "cp3";
    }
    if (const char* profile = std::getenv("USERPROFILE")) {
        if (*profile != '\0') return fs::path(profile) / ".config" / "cp3";
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg != '\0') return fs::path(xdg) / "cp3";
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') return fs::path(home) / ".config" / "cp3";
    }
#endif
    std::error_code ec;
    return fs::current_path(ec) / ".cp3-config";
}

bool looksLikePath(const std::string& value) {
    return value.find('/') != std::string::npos ||
           value.find('\\') != std::string::npos ||
           (value.size() > 1 && value[1] == ':');
}

}  // namespace

void warnSavedToolchainChoice(const ToolchainChoice& choice,
                              const std::string& label) {
    ChoiceResolutionResult result;
    resolveToolchainChoice(choice, label, false, result);
    const std::string warning = !result.warning.empty()
                                    ? result.warning
                                    : (!result.error.empty()
                                           ? label + ": " + result.error
                                           : std::string());
    if (warning.empty()) return;
    if (toolchainWarning().find(warning) != std::string::npos) return;
    std::fprintf(stderr, "cp3c: warning: %s\n", warning.c_str());
}

fs::path toolchainConfigPath() {
    return configDirectory() / "toolchain.conf";
}

ToolchainChoice GlobalToolchainConfig::choice() const {
    ToolchainChoice choice;
    choice.id = id;
    choice.version = version;
    choice.policy = policy;
    choice.program = program;
    choice.present = present;
    choice.custom = policy == "custom" ||
                    (!id.empty() && looksLikePath(id)) ||
                    (id.empty() && !program.empty());
    return choice;
}

GlobalToolchainConfig readGlobalToolchainConfig() {
    GlobalToolchainConfig config;
    std::string text;
    if (!readTextFile(toolchainConfigPath(), text)) return config;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string line =
            text.substr(start, end == std::string::npos ? std::string::npos
                                                        : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trimCopy(line);
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trimCopy(trimmed.substr(0, eq));
        const std::string value = trimCopy(trimmed.substr(eq + 1));
        if (key == "id") config.id = value;
        else if (key == "version") config.version = value;
        else if (key == "policy") config.policy = value;
        else if (key == "program") config.program = value;
        else if (key == "saved") config.saved = value;
    }
    config.present = !config.id.empty() || !config.program.empty();
    return config;
}

bool writeGlobalToolchainConfig(const ToolchainChoice& choice, std::string& err) {
    if (choice.id.find('\n') != std::string::npos ||
        choice.version.find('\n') != std::string::npos ||
        choice.policy.find('\n') != std::string::npos ||
        choice.program.find('\n') != std::string::npos) {
        err = "toolchain fields must not contain newlines";
        return false;
    }
    std::string content;
    const std::string id = choice.custom && !choice.program.empty()
                               ? choice.program
                               : choice.id;
    if (!id.empty()) content += "id=" + id + "\n";
    if (!choice.version.empty()) content += "version=" + choice.version + "\n";
    if (!choice.policy.empty()) content += "policy=" + choice.policy + "\n";
    if (!choice.program.empty()) content += "program=" + choice.program + "\n";
    content += "saved=" + todayIso() + "\n";
    const fs::path path = toolchainConfigPath();
    if (!writeTextFile(path, content, nullptr)) {
        err = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool clearGlobalToolchainConfig(bool& removed, std::string& err) {
    std::error_code ec;
    const fs::path path = toolchainConfigPath();
    removed = fs::remove(path, ec);
    if (ec) {
        err = "cannot remove " + path.string() + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

bool saveToolchainChoice(const ToolchainChoice& choice, bool projectScope,
                         const std::filesystem::path& projectRoot,
                         std::string& err) {
    if (projectScope) {
        if (choice.custom) {
            std::fprintf(stderr,
                         "cp3c: warning: storing a machine-specific compiler "
                         "path in package.json; use global scope for portable "
                         "projects\n");
        }
        return writeProjectToolchain(projectRoot, choice, err);
    }
    return writeGlobalToolchainConfig(choice, err);
}

}  // namespace cp3::cli
