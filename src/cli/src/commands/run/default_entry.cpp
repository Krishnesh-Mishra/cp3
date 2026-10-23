// =============================================================================
// src/cli/src/commands/default_entry.cpp - default entry lookup for `build`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::defaultEntryFile.
//
// Split out of cli.cpp to keep the command dispatcher within the STYLE.md
// source-file budget. Behaviour is unchanged: package.json "main" first, then
// the conventional entry file names.
// =============================================================================
#include <string>
#include <system_error>

#include "detail/core/commands.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

// Minimal `"main": "path"` extraction from package.json.
std::string extractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t kpos = json.find(needle);
    if (kpos == std::string::npos) return {};
    std::size_t i = kpos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' ||
                               json[i] == '\r' || json[i] == '\n')) {
        ++i;
    }
    if (i >= json.size() || json[i] != ':') return {};
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' ||
                               json[i] == '\r' || json[i] == '\n')) {
        ++i;
    }
    if (i >= json.size() || json[i] != '"') return {};
    ++i;
    std::string out;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            ++i;
        }
        out.push_back(json[i]);
        ++i;
    }
    return out;
}

}  // namespace

std::string defaultEntryFile() {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);

    std::string pkg;
    if (readTextFile(cwd / "package.json", pkg)) {
        const std::string main = extractJsonString(pkg, "main");
        if (!main.empty()) {
            const fs::path candidate = cwd / main;
            if (fs::exists(candidate, ec)) return candidate.string();
        }
    }
    for (const char* rel : {"src/main.cp3", "src/index.cp3", "index.cp3",
                            "main.cp3"}) {
        const fs::path candidate = cwd / rel;
        if (fs::exists(candidate, ec)) return candidate.string();
    }
    return {};
}

}  // namespace cp3::cli
