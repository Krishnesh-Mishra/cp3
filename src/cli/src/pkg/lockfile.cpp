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
//       },
//       "localkit": {
//         "version": "1.0.0",
//         "source": "link",
//         "kind": "directory",
//         "resolved": "C:/work/localkit",
//         "target": "C:/work/localkit"
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

// Appends `"key": "value",` when the value is non-empty. Keeping every
// optional field out of the file when empty preserves the pre-link shape.
void writeStringField(std::string& out, const char* key, const std::string& value,
                      bool last) {
    if (value.empty()) return;
    out += "      \"" + std::string(key) + "\": " + jsonQuote(value);
    out += last ? "\n" : ",\n";
}

bool endsWithComma(const std::string& out) {
    return !out.empty() && out.back() == '\n' && out.size() >= 2 &&
           out[out.size() - 2] == ',';
}

// Rewrites the final comma of a field list into a newline. The writer emits
// fields optimistically; this trims the separator left by the last present
// optional field.
void finishFields(std::string& out) {
    if (endsWithComma(out)) {
        out[out.size() - 2] = '\n';
        out.pop_back();
    }
}

void writeEntry(std::string& out, const LockEntry& entry, bool last) {
    out += "    " + jsonQuote(entry.name) + ": {\n";
    out += "      \"version\": " + jsonQuote(entry.version) + ",\n";
    writeStringField(out, "source", entry.source, false);
    writeStringField(out, "kind", entry.kind, false);
    writeStringField(out, "repo", entry.repo, false);
    writeStringField(out, "tag", entry.tag, false);
    writeStringField(out, "asset", entry.asset, false);
    writeStringField(out, "resolved", entry.resolved, false);
    writeStringField(out, "target", entry.target, false);
    writeStringField(out, "integrity", entry.integrity, true);
    finishFields(out);
    out += last ? "    }\n" : "    },\n";
}

}  // namespace

bool isLinkEntry(const LockEntry& entry) {
    return entry.source == kSourceLink;
}

LockEntry* findLockEntry(std::vector<LockEntry>& entries,
                         const std::string& name) {
    for (LockEntry& entry : entries) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

const LockEntry* findLockEntry(const std::vector<LockEntry>& entries,
                               const std::string& name) {
    for (const LockEntry& entry : entries) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

void upsertLockEntry(std::vector<LockEntry>& entries, const LockEntry& entry) {
    for (LockEntry& existing : entries) {
        if (existing.name == entry.name) {
            existing = entry;
            return;
        }
    }
    entries.push_back(entry);
}

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
        if (const std::string* value = field.second.stringField("source")) {
            entry.source = *value;
        }
        if (const std::string* value = field.second.stringField("kind")) {
            entry.kind = *value;
        }
        if (const std::string* value = field.second.stringField("target")) {
            entry.target = *value;
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
