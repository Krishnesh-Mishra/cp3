// =============================================================================
// src/cli/src/pkg/lockfile.cpp - `.cp3/lock.json` DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/lockfile.hpp.
//
// Shape (stable, hand-readable):
//   {
//     "lockfileVersion": 1,
//     "packages": {
//       "basickit": {
//         "version": "1.0.0",
//         "resolved": "file:../basickit.cp3pak",
//         "integrity": "sha256:..."
//       }
//     }
//   }
// =============================================================================
#include "detail/lockfile.hpp"

#include <algorithm>

#include "detail/json.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

constexpr int kLockfileVersion = 1;

void writeEntry(std::string& out, const LockEntry& entry, bool last) {
    out += "    " + jsonQuote(entry.name) + ": {\n";
    out += "      \"version\": " + jsonQuote(entry.version) + ",\n";
    if (!entry.repo.empty()) {
        out += "      \"repo\": " + jsonQuote(entry.repo) + ",\n";
    }
    if (!entry.tag.empty()) {
        out += "      \"tag\": " + jsonQuote(entry.tag) + ",\n";
    }
    if (!entry.asset.empty()) {
        out += "      \"asset\": " + jsonQuote(entry.asset) + ",\n";
    }
    out += "      \"resolved\": " + jsonQuote(entry.resolved) + ",\n";
    out += "      \"integrity\": " + jsonQuote(entry.integrity) + "\n";
    out += last ? "    }\n" : "    },\n";
}

}  // namespace

bool readLockfile(const fs::path& projectRoot, std::vector<LockEntry>& out,
                  std::string& err) {
    out.clear();
    err.clear();
    const fs::path path = projectRoot / ".cp3" / "lock.json";
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return true;  // no lock yet
    std::string text;
    if (!readTextFile(path, text)) {
        err = "cannot read " + path.string();
        return false;
    }
    JsonValue doc;
    if (!JsonValue::parse(text, doc, err)) {
        err = path.string() + ": " + err;
        return false;
    }
    const JsonValue* packages = doc.find("packages");
    if (packages == nullptr || !packages->isObject()) {
        err = path.string() + ": missing \"packages\" object";
        return false;
    }
    for (const auto& field : packages->object) {
        if (!field.second.isObject()) continue;
        LockEntry entry;
        entry.name = field.first;
        if (const std::string* value = field.second.stringField("version")) {
            entry.version = *value;
        }
        if (const std::string* value = field.second.stringField("resolved")) {
            entry.resolved = *value;
        }
        if (const std::string* value = field.second.stringField("integrity")) {
            entry.integrity = *value;
        }
        if (const std::string* value = field.second.stringField("repo")) {
            entry.repo = *value;
        }
        if (const std::string* value = field.second.stringField("tag")) {
            entry.tag = *value;
        }
        if (const std::string* value = field.second.stringField("asset")) {
            entry.asset = *value;
        }
        out.push_back(std::move(entry));
    }
    return true;
}

bool writeLockfile(const fs::path& projectRoot, const std::vector<LockEntry>& entries,
                   std::string& err) {
    err.clear();
    std::vector<LockEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const LockEntry& a, const LockEntry& b) {
                  return a.name < b.name;
              });
    std::string out;
    out += "{\n";
    out += "  \"lockfileVersion\": " + std::to_string(kLockfileVersion) + ",\n";
    out += "  \"packages\": {\n";
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        writeEntry(out, sorted[i], i + 1 == sorted.size());
    }
    out += "  }\n";
    out += "}\n";

    const fs::path dir = projectRoot / ".cp3";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!writeTextFile(dir / "lock.json", out, nullptr)) {
        err = "cannot write " + (dir / "lock.json").string();
        return false;
    }
    return true;
}

}  // namespace cp3::cli
