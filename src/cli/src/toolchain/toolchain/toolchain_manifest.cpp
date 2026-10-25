// =============================================================================
// src/cli/src/toolchain/toolchain_manifest.cpp - project package.json scope.
//
// OWNERSHIP: cli module (src/cli/**). Implements the project half of
// detail/toolchain_select.hpp.
//
// The writer is a small brace/string scanner that inserts, replaces or removes
// only the top-level `"toolchain"` value and preserves every other byte,
// including CRLF and field order. Both writers validate the result with
// JsonValue::parse before touching the file. The value is always the object
// form `{ "id": "...", "version": "...", "policy": "latest|exact" }`.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"

#include <string>
#include <system_error>
#include <vector>

#include "detail/pkg/json.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

constexpr std::size_t kNoPos = std::string::npos;

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

std::string serializedValue(const ToolchainChoice& choice) {
    const std::string id = choice.custom && !choice.program.empty()
                               ? choice.program
                               : choice.id;
    const std::string policy = choice.policy == "exact" ? "exact" : "latest";
    std::string value = "{ \"id\": " + jsonQuote(id);
    if (!choice.version.empty()) {
        value += ", \"version\": " + jsonQuote(choice.version);
    }
    value += ", \"policy\": " + jsonQuote(policy) + " }";
    return value;
}

bool verifyValue(const JsonValue& check, const ToolchainChoice& choice) {
    const JsonValue* value = check.find("toolchain");
    if (value == nullptr || !value->isObject()) return false;
    const std::string id = choice.custom && !choice.program.empty()
                               ? choice.program
                               : choice.id;
    const std::string policy = choice.policy == "exact" ? "exact" : "latest";
    const std::string* writtenId = value->stringField("id");
    const std::string* writtenPolicy = value->stringField("policy");
    const std::string* writtenVersion =
        choice.version.empty() ? nullptr : value->stringField("version");
    if (writtenId == nullptr || *writtenId != id) return false;
    if (writtenPolicy == nullptr || *writtenPolicy != policy) return false;
    if (!choice.version.empty() &&
        (writtenVersion == nullptr || *writtenVersion != choice.version)) {
        return false;
    }
    return true;
}

bool readChoice(const JsonValue& manifest, ToolchainChoice& out,
                const fs::path& path, std::string& err) {
    const JsonValue* value = manifest.find("toolchain");
    out = ToolchainChoice{};
    if (value == nullptr) return true;
    out.present = true;
    if (value->isString()) {
        std::string parseError;
        if (!parseToolchainSpec(value->string, out, parseError)) {
            err = path.string() + ": " + parseError;
            return false;
        }
        return true;
    }
    if (!value->isObject()) {
        err = path.string() +
              ": \"toolchain\" must be a string or an object with id, version "
              "and policy";
        return false;
    }
    const std::string* id = value->stringField("id");
    if (id == nullptr || id->empty()) {
        err = path.string() + ": \"toolchain.id\" must be a non-empty string";
        return false;
    }
    const bool pathShaped =
        id->find('/') != std::string::npos ||
        id->find('\\') != std::string::npos ||
        (id->size() > 1 && (*id)[1] == ':');
    if (!pathShaped && !isClangId(*id)) {
        err = path.string() +
              ": \"toolchain.id\" must be clang, apple-clang, or a clang++ "
              "path (beta one supports Clang only)";
        return false;
    }
    out.id = pathShaped ? *id : std::string("clang");
    if (const std::string* version = value->stringField("version")) {
        if (!validVersionSpec(*version)) {
            err = path.string() + ": invalid \"toolchain.version\"";
            return false;
        }
        out.version = *version;
    }
    if (const std::string* policy = value->stringField("policy")) {
        out.policy = *policy;
    }
    if (out.policy.empty()) {
        out.policy = out.version.empty() ? "latest" : "exact";
    }
    if (out.policy != "latest" && out.policy != "exact") {
        err = path.string() +
              ": \"toolchain.policy\" must be \"latest\" or \"exact\"";
        return false;
    }
    if (pathShaped) {
        out.custom = true;
        out.program = out.id;
    }
    return true;
}

}  // namespace

bool toolchainProjectRoot(fs::path& packageRoot) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec) return false;
    return findPackageRoot(cwd, packageRoot);
}

bool readProjectToolchain(const fs::path& packageRoot, ToolchainChoice& out,
                          std::string& err) {
    JsonValue manifest;
    if (!loadPackageJson(packageRoot, manifest, err)) return false;
    return readChoice(manifest, out, packageRoot / "package.json", err);
}

bool writeProjectToolchain(const fs::path& packageRoot,
                           const ToolchainChoice& choice, std::string& err) {
    const fs::path path = packageRoot / "package.json";
    std::string text;
    if (!readTextFile(path, text)) {
        err = "cannot read " + path.string();
        return false;
    }
    JsonValue manifest;
    std::string parseError;
    if (!JsonValue::parse(text, manifest, parseError)) {
        err = path.string() + ": refusing to edit a manifest with a parse "
              "error (" + parseError + ")";
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
    const std::string quoted = serializedValue(choice);
    const FieldSpan* existing = nullptr;
    std::string result;
    if (findField(fields, "toolchain", existing)) {
        result = text.substr(0, existing->valueStart) + quoted +
                 text.substr(existing->valueEnd);
    } else if (fields.empty()) {
        const std::size_t open = text.find('{');
        const std::size_t close = text.rfind('}');
        if (open == kNoPos || close == kNoPos || close < open) {
            err = path.string() + ": could not locate the top-level object";
            return false;
        }
        if (text.find('\n') == kNoPos) {
            result = text.substr(0, open + 1) + "\"toolchain\": " + quoted +
                     text.substr(close);
        } else {
            const std::string nl =
                text.find("\r\n") != kNoPos ? "\r\n" : "\n";
            result = text.substr(0, open + 1) + nl + "  \"toolchain\": " +
                     quoted + nl + text.substr(close);
        }
    } else {
        const FieldSpan& last = fields.back();
        std::string insert;
        if (text.find('\n') == kNoPos) {
            insert = ", \"toolchain\": " + quoted;
        } else {
            const std::string nl =
                text.find("\r\n") != kNoPos ? "\r\n" : "\n";
            insert = "," + nl + indentOf(text, last.keyStart) +
                     "\"toolchain\": " + quoted;
        }
        result = text.substr(0, last.valueEnd) + insert +
                 text.substr(last.valueEnd);
    }
    JsonValue check;
    std::string checkError;
    if (!JsonValue::parse(result, check, checkError)) {
        err = path.string() + ": refusing to write, the edited manifest did "
              "not parse (" + checkError + ")";
        return false;
    }
    if (!verifyValue(check, choice)) {
        err = path.string() +
              ": refusing to write, could not verify the toolchain field";
        return false;
    }
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
        err = path.string() + ": refusing to edit a manifest with a parse "
              "error (" + parseError + ")";
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
