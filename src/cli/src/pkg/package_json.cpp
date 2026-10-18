// =============================================================================
// src/cli/src/pkg/package_json.cpp - package.json helpers DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/package_json.hpp.
// =============================================================================
#include "detail/package_json.hpp"

#include <system_error>

#include "detail/util.hpp"

namespace cp3::cli {

bool findPackageRoot(const fs::path& startDir, fs::path& root) {
    std::error_code ec;
    fs::path probe = fs::absolute(startDir, ec);
    if (ec) probe = startDir;
    for (;;) {
        if (fs::is_regular_file(probe / "package.json", ec)) {
            root = probe;
            return true;
        }
        const fs::path parent = probe.parent_path();
        if (parent.empty() || parent == probe) return false;
        probe = parent;
    }
}

bool loadPackageJson(const fs::path& root, JsonValue& out, std::string& err) {
    const fs::path path = root / "package.json";
    std::string text;
    if (!readTextFile(path, text)) {
        err = "cannot read " + path.string();
        return false;
    }
    if (!JsonValue::parse(text, out, err)) {
        err = path.string() + ": " + err;
        return false;
    }
    if (!out.isObject()) {
        err = path.string() + ": top-level value must be an object";
        return false;
    }
    return true;
}

bool isPackageProject(const fs::path& root) {
    JsonValue manifest;
    std::string err;
    if (!loadPackageJson(root, manifest, err)) return false;
    const std::string* type = manifest.stringField("type");
    return type != nullptr && *type == "package";
}

std::string packageName(const JsonValue& manifest, const fs::path& root) {
    if (const std::string* name = manifest.stringField("name")) {
        if (!name->empty()) return *name;
    }
    const std::string dirName = root.filename().string();
    return dirName.empty() ? "package" : dirName;
}

std::string packageVersion(const JsonValue& manifest) {
    if (const std::string* version = manifest.stringField("version")) {
        if (!version->empty()) return *version;
    }
    return "0.0.0";
}

std::string packageMain(const JsonValue& manifest) {
    if (const std::string* main = manifest.stringField("main")) {
        if (!main->empty()) return *main;
    }
    return "src/index.cp3";
}

}  // namespace cp3::cli
