// =============================================================================
// src/pkg/remote/update_check.cpp - self-update check DEFINITIONS.
// =============================================================================
#include "detail/pkg/update_check.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "detail/core/semver.hpp"
#include "detail/core/util.hpp"
#include "detail/pkg/github.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/package_json.hpp"

namespace cp3::cli {

const char* const kUpdateCheckFile = "update-check.json";
const char* const kSelfRepo = "Krishnesh-Mishra/cp3";

namespace {

namespace fs = std::filesystem;

constexpr std::int64_t kOneDaySec = 24 * 60 * 60;

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

std::int64_t unixNow() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string stripLeadingV(const std::string& text) {
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) {
        return text.substr(1);
    }
    return text;
}

std::string readFileText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

UpdateCheckState readUpdateCheck() {
    UpdateCheckState state;
    const fs::path path = configDirectory() / kUpdateCheckFile;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return state;
    const std::string text = readFileText(path);
    if (text.empty()) return state;
    JsonValue doc;
    std::string err;
    if (!JsonValue::parse(text, doc, err) || !doc.isObject()) {
        return state;  // present=false: corrupt cache is ignored, never fatal
    }
    const JsonValue* tag = doc.find("latest");
    const JsonValue* checked = doc.find("checkedAt");
    if (tag == nullptr || !tag->isString() || checked == nullptr ||
        checked->type != JsonValue::Type::Number) {
        return state;
    }
    state.present = true;
    state.latestTag = tag->string;
    state.checkedAt = static_cast<std::int64_t>(checked->number);
    state.stale = (unixNow() - state.checkedAt) >= kOneDaySec;
    return state;
}

bool writeUpdateCheck(std::int64_t nowSec, const std::string& latestTag,
                      std::string& err) {
    const fs::path dir = configDirectory();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        err = "cannot create " + dir.string();
        return false;
    }
    const fs::path path = dir / kUpdateCheckFile;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot write " + path.string();
        return false;
    }
    out << "{\n  \"checkedAt\": " << nowSec
        << ",\n  \"latest\": \"" << jsonEscape(latestTag) << "\"\n}\n";
    return true;
}

bool isNewerVersion(const std::string& latest, const std::string& current) {
    const SemVer l = parseSemVer(stripLeadingV(latest));
    const SemVer c = parseSemVer(stripLeadingV(current));
    return compareSemVer(l, c) > 0;
}

bool refreshUpdateCheck(bool silent, UpdateCheckState& out, std::string& err) {
    GithubRepo repo;
    repo.owner = "Krishnesh-Mishra";
    repo.repo = "cp3";
    GithubRelease release;
    const bool ok = fetchLatestGithubRelease(repo, release, err);
    if (!ok) {
        if (silent) err.clear();
        return false;
    }
    const std::int64_t now = unixNow();
    std::string writeErr;
    if (!writeUpdateCheck(now, release.tag, writeErr) && !silent) {
        err = writeErr;
        return false;
    }
    out = UpdateCheckState{};
    out.present = true;
    out.latestTag = release.tag;
    out.checkedAt = now;
    out.stale = false;
    return true;
}

}  // namespace cp3::cli
