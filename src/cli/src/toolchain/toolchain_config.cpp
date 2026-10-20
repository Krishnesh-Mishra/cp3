// =============================================================================
// src/cli/src/toolchain/toolchain_config.cpp - saved toolchain choices.
//
// OWNERSHIP: cli module (src/cli/**). Implements the persistence half of
// detail/toolchain_select.hpp.
//
// Global scope is a plain key=value file (`id`, `program`, `saved`), read
// tolerantly so a hand-edited file never crashes a build. Project scope is the
// top-level `"toolchain"` string in the nearest package.json; the writer is a
// small brace/string scanner that inserts or replaces only that field and
// preserves every other byte, including CRLF and field order. Both writers
// validate the result with JsonValue::parse before touching the file.
// =============================================================================
#include "detail/toolchain_select.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <system_error>
#include <vector>

#include "detail/json.hpp"
#include "detail/package_json.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

constexpr std::size_t kNoPos = std::string::npos;

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

void skipSpace(const std::string& text, std::size_t& i) {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' ||
                               text[i] == '\r' || text[i] == '\n')) {
        ++i;
    }
}

// Advances `i` past a JSON string starting at the opening quote.
bool skipString(const std::string& text, std::size_t& i) {
    if (i >= text.size() || text[i] != '"') return false;
    ++i;
    while (i < text.size()) {
        const char c = text[i++];
        if (c == '\\') {
            if (i >= text.size()) return false;
            ++i;
        } else if (c == '"') {
            return true;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return false;
}

std::string decodeKey(const std::string& token) {
    std::string out;
    for (std::size_t i = 1; i + 1 < token.size(); ++i) {
        if (token[i] == '\\' && i + 2 < token.size()) {
            ++i;
            const char esc = token[i];
            if (esc == 'n') out.push_back('\n');
            else if (esc == 't') out.push_back('\t');
            else if (esc == 'r') out.push_back('\r');
            else out.push_back(esc);
        } else {
            out.push_back(token[i]);
        }
    }
    return out;
}

bool skipValue(const std::string& text, std::size_t& i) {
    if (i >= text.size()) return false;
    const char first = text[i];
    if (first == '"') return skipString(text, i);
    if (first == '{' || first == '[') {
        const char close = first == '{' ? '}' : ']';
        int depth = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '"') {
                if (!skipString(text, i)) return false;
                continue;
            }
            if (c == first) ++depth;
            if (c == close) {
                --depth;
                ++i;
                if (depth == 0) return true;
                continue;
            }
            ++i;
        }
        return false;
    }
    const std::size_t start = i;
    while (i < text.size() && text[i] != ',' && text[i] != '}' &&
           text[i] != ']' && text[i] != ' ' && text[i] != '\t' &&
           text[i] != '\r' && text[i] != '\n') {
        ++i;
    }
    return i > start;
}

struct FieldSpan {
    std::string key;
    std::size_t keyStart = 0;
    std::size_t valueStart = 0;
    std::size_t valueEnd = 0;
    std::size_t commaBefore = kNoPos;
    std::size_t commaAfter = kNoPos;
};

bool scanTopLevelFields(const std::string& text,
                        std::vector<FieldSpan>& fields) {
    std::size_t i = 0;
    skipSpace(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    ++i;
    bool first = true;
    for (;;) {
        skipSpace(text, i);
        if (i >= text.size()) return false;
        if (text[i] == '}') {
            ++i;
            break;
        }
        FieldSpan field;
        if (!first) {
            if (text[i] != ',') return false;
            field.commaBefore = i;
            if (!fields.empty()) fields.back().commaAfter = i;
            ++i;
            skipSpace(text, i);
        }
        if (i >= text.size() || text[i] != '"') return false;
        field.keyStart = i;
        if (!skipString(text, i)) return false;
        const std::size_t keyEnd = i;
        field.key = decodeKey(text.substr(field.keyStart, keyEnd - field.keyStart));
        skipSpace(text, i);
        if (i >= text.size() || text[i] != ':') return false;
        ++i;
        skipSpace(text, i);
        field.valueStart = i;
        if (!skipValue(text, i)) return false;
        field.valueEnd = i;
        fields.push_back(field);
        first = false;
    }
    skipSpace(text, i);
    return i == text.size();
}

std::size_t lineStartOf(const std::string& text, std::size_t pos) {
    const std::size_t newline = text.rfind('\n', pos);
    return newline == kNoPos ? 0 : newline + 1;
}

bool onlyBlanks(const std::string& text, std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
        if (text[i] != ' ' && text[i] != '\t') return false;
    }
    return true;
}

std::string indentOf(const std::string& text, std::size_t keyStart) {
    const std::size_t start = lineStartOf(text, keyStart);
    if (onlyBlanks(text, start, keyStart)) {
        return text.substr(start, keyStart - start);
    }
    return "  ";
}

void consumeLineEnd(const std::string& text, std::size_t& pos) {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }
    if (pos < text.size() && text[pos] == '\r') ++pos;
    if (pos < text.size() && text[pos] == '\n') ++pos;
}

bool findField(const std::vector<FieldSpan>& fields, const std::string& key,
               const FieldSpan*& out) {
    for (const FieldSpan& field : fields) {
        if (field.key == key) {
            out = &field;
            return true;
        }
    }
    return false;
}

bool verifyEdited(const std::string& text, const std::string& path,
                  const std::string& expected, std::string& err) {
    JsonValue check;
    std::string parseError;
    if (!JsonValue::parse(text, check, parseError)) {
        err = path + ": refusing to write, the edited manifest did not parse (" +
              parseError + ")";
        return false;
    }
    const std::string* written = check.stringField("toolchain");
    if (written == nullptr || *written != expected) {
        err = path + ": refusing to write, could not verify the toolchain field";
        return false;
    }
    return true;
}

}  // namespace

fs::path toolchainConfigPath() {
    return configDirectory() / "toolchain.conf";
}

GlobalToolchainConfig readGlobalToolchainConfig() {
    GlobalToolchainConfig config;
    std::string text;
    if (!readTextFile(toolchainConfigPath(), text)) return config;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string line = text.substr(
            start, end == kNoPos ? kNoPos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trimCopy(line);
        start = end == kNoPos ? text.size() + 1 : end + 1;
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trimCopy(trimmed.substr(0, eq));
        const std::string value = trimCopy(trimmed.substr(eq + 1));
        if (key == "id") config.id = value;
        else if (key == "program") config.program = value;
        else if (key == "saved") config.saved = value;
    }
    config.present = !config.id.empty() || !config.program.empty();
    return config;
}

bool writeGlobalToolchainConfig(const std::string& id,
                                const std::string& program, std::string& err) {
    if (id.find('\n') != std::string::npos ||
        program.find('\n') != std::string::npos) {
        err = "toolchain id and program must not contain newlines";
        return false;
    }
    std::string content;
    if (!id.empty()) content += "id=" + id + "\n";
    if (!program.empty()) content += "program=" + program + "\n";
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

bool readProjectToolchain(const fs::path& packageRoot, std::string& out,
                          std::string& err) {
    JsonValue manifest;
    if (!loadPackageJson(packageRoot, manifest, err)) return false;
    out.clear();
    const JsonValue* value = manifest.find("toolchain");
    if (value == nullptr) return true;
    if (!value->isString()) {
        err = (packageRoot / "package.json").string() +
              ": \"toolchain\" must be a string";
        return false;
    }
    out = value->string;
    return true;
}

bool writeProjectToolchain(const fs::path& packageRoot,
                           const std::string& value, std::string& err) {
    const fs::path path = packageRoot / "package.json";
    std::string text;
    if (!readTextFile(path, text)) {
        err = "cannot read " + path.string();
        return false;
    }
    JsonValue manifest;
    std::string parseError;
    if (!JsonValue::parse(text, manifest, parseError)) {
        err = path.string() + ": refusing to edit an unparsable manifest (" +
              parseError + ")";
        return false;
    }
    if (!manifest.isObject()) {
        err = path.string() + ": top-level value must be an object";
        return false;
    }
    std::vector<FieldSpan> fields;
    if (!scanTopLevelFields(text, fields)) {
        err = path.string() + ": could not locate top-level fields safely";
        return false;
    }
    const std::string quoted = jsonQuote(value);
    const FieldSpan* existing = nullptr;
    std::string result;
    if (findField(fields, "toolchain", existing)) {
        if (manifest.stringField("toolchain") == nullptr) {
            err = path.string() + ": \"toolchain\" must be a string";
            return false;
        }
        result = text.substr(0, existing->valueStart) + quoted +
                 text.substr(existing->valueEnd);
    } else if (fields.empty()) {
        const std::size_t open = text.find('{');
        const std::size_t close = text.rfind('}');
        if (open == kNoPos || close == kNoPos || close < open) {
            err = path.string() + ": could not locate the top-level object";
            return false;
        }
        if (text.find('\n') == std::string::npos) {
            result = text.substr(0, open + 1) + "\"toolchain\": " + quoted +
                     text.substr(close);
        } else {
            const std::string nl =
                text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            result = text.substr(0, open + 1) + nl + "  \"toolchain\": " +
                     quoted + nl + text.substr(close);
        }
    } else {
        const FieldSpan& last = fields.back();
        std::string insert;
        if (text.find('\n') == std::string::npos) {
            insert = ", \"toolchain\": " + quoted;
        } else {
            const std::string nl =
                text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            insert = "," + nl + indentOf(text, last.keyStart) +
                     "\"toolchain\": " + quoted;
        }
        result = text.substr(0, last.valueEnd) + insert +
                 text.substr(last.valueEnd);
    }
    if (!verifyEdited(result, path.string(), value, err)) return false;
    if (!writeTextFile(path, result, nullptr)) {
        err = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool clearProjectToolchain(const fs::path& packageRoot, bool& removed,
                           std::string& err) {
    const fs::path path = packageRoot / "package.json";
    std::string text;
    if (!readTextFile(path, text)) {
        err = "cannot read " + path.string();
        return false;
    }
    JsonValue manifest;
    std::string parseError;
    if (!JsonValue::parse(text, manifest, parseError)) {
        err = path.string() + ": refusing to edit an unparsable manifest (" +
              parseError + ")";
        return false;
    }
    std::vector<FieldSpan> fields;
    if (!scanTopLevelFields(text, fields)) {
        err = path.string() + ": could not locate top-level fields safely";
        return false;
    }
    const FieldSpan* field = nullptr;
    if (!findField(fields, "toolchain", field)) {
        removed = false;
        return true;
    }
    const std::size_t lineStart = lineStartOf(text, field->keyStart);
    const bool ownLine = onlyBlanks(text, lineStart, field->keyStart) &&
                         (lineStart == 0 || text[lineStart - 1] == '\n');
    std::size_t start = field->keyStart;
    std::size_t end = field->valueEnd;
    bool consumeNewline = false;
    if (ownLine && field->commaAfter != kNoPos) {
        start = lineStart;
        end = field->commaAfter + 1;
        consumeNewline = true;
    } else if (ownLine && field->commaBefore != kNoPos) {
        start = field->commaBefore;
        end = field->valueEnd;
    } else if (ownLine) {
        start = lineStart;
        end = field->valueEnd;
        consumeNewline = true;
    } else if (field->commaAfter != kNoPos) {
        start = field->commaBefore != kNoPos ? field->commaBefore
                                             : field->keyStart;
        end = field->commaAfter + 1;
    } else if (field->commaBefore != kNoPos) {
        start = field->commaBefore;
        end = field->valueEnd;
    }
    if (consumeNewline) consumeLineEnd(text, end);
    const std::string result = text.substr(0, start) + text.substr(end);
    JsonValue check;
    std::string checkError;
    if (!JsonValue::parse(result, check, checkError)) {
        err = path.string() + ": refusing to remove the field, the edited "
              "manifest did not parse (" + checkError + ")";
        return false;
    }
    if (check.find("toolchain") != nullptr) {
        err = path.string() + ": refusing to remove the field, it is still present";
        return false;
    }
    if (!writeTextFile(path, result, nullptr)) {
        err = "cannot write " + path.string();
        return false;
    }
    removed = true;
    return true;
}

}  // namespace cp3::cli
