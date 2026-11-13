// =============================================================================
// src/cli/src/toolchain/runtime/incremental.cpp - per-module incremental cache
// DEFINITIONS.
// =============================================================================
#include "detail/toolchain/incremental.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <unordered_map>

#include "detail/core/util.hpp"
#include "cp3/meta/module.hpp"

namespace cp3::cli {

namespace {

// Length-prefixed field writer/reader (private to this file): "<len>:<bytes>".
void putStr(std::string& out, const std::string& text) {
    out += std::to_string(text.size());
    out += ':';
    out += text;
}

bool takeStr(const std::string& in, std::size_t& at, std::string& out) {
    const std::size_t sep = in.find(':', at);
    if (sep == std::string::npos) return false;
    std::size_t len = 0;
    try {
        len = std::stoul(in.substr(at, sep - at));
    } catch (const std::exception&) {
        return false;
    }
    if (sep + 1 + len > in.size()) return false;
    out = in.substr(sep + 1, len);
    at = sep + 1 + len;
    return true;
}

std::string hex64(std::uint64_t value) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string(buf, 16);
}

}  // namespace

std::string incrementalUnitKey(const CompilationUnit& unit) {
    // The logical module name is the graph's stable identity for a unit
    // (project-relative for local files; the specifier for packages).
    return unit.moduleName.empty() ? "<anonymous>" : unit.moduleName;
}

std::string unitSourceHash(const CompilationUnit& unit) {
    if (unit.file == nullptr) return hex64(0);
    return hex64(fnv1a64(unit.file->text));
}

std::string classMethodHash(const ModuleGraph& graph) {
    // The multiset of class method names across the whole program: the same
    // signal the codegen virtual-set pass aggregates, computed parse-only.
    // Any change to a class member anywhere retires every cached fragment
    // that depends on program-wide dispatch.
    std::unordered_map<std::string, std::uint64_t> multiplicity;
    for (const CompilationUnit* unit : graph.units()) {
        if (unit == nullptr || !unit->ast) continue;
        std::vector<ClassDeclaration*> classes;
        std::function<void(const StmtPtr&)> collectClass;
        collectClass = [&classes](const StmtPtr& stmt) {
            if (!stmt) return;
            if (stmt->kind == NodeKind::ClassDeclaration) {
                classes.push_back(static_cast<ClassDeclaration*>(stmt.get()));
            }
        };
        for (const StmtPtr& stmt : unit->ast->body) collectClass(stmt);
        for (ClassDeclaration* cls : classes) {
            for (const ClassMemberPtr& member : cls->members) {
                if (!member) continue;
                std::string name;
                if (member->kind == NodeKind::MethodDeclaration) {
                    const auto& m = static_cast<const MethodDeclaration&>(*member);
                    if (m.key && m.key->kind == NodeKind::Identifier) {
                        name = static_cast<const Identifier&>(*m.key).name;
                    }
                } else if (member->kind == NodeKind::AccessorDeclaration) {
                    const auto& a = static_cast<const AccessorDeclaration&>(*member);
                    if (a.key && a.key->kind == NodeKind::Identifier) {
                        name = "access:" +
                               static_cast<const Identifier&>(*a.key).name;
                    }
                } else if (member->kind == NodeKind::ConstructorDeclaration) {
                    name = "constructor";
                } else {
                    continue;
                }
                multiplicity[name] += 1;
            }
        }
    }
    std::vector<std::string> names;
    names.reserve(multiplicity.size());
    for (const auto& entry : multiplicity) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    std::string material;
    for (const std::string& name : names) {
        material += name;
        material += '=';
        material += std::to_string(multiplicity[name]);
        material += '\n';
    }
    return hex64(fnv1a64(material));
}

std::string configHashOf(const std::string& compilerKey,
                         const std::string& profile) {
    std::string material = compilerKey;
    material += '|';
    material += profile;
    return hex64(fnv1a64(material));
}

std::string ihFingerprintOf(
    const ModuleGraph& graph, const CompilationUnit& unit,
    const std::unordered_map<std::uint32_t, std::uint64_t>& ihOf) {
    std::string material;
    for (const ModuleDependency& dep : graph.dependencies()) {
        if (dep.importer != unit.id) continue;
        if (dep.resolution != ModuleResolution::Local &&
            dep.resolution != ModuleResolution::Package) {
            continue;
        }
        if (dep.imported == UINT32_MAX) continue;
        const auto it = ihOf.find(dep.imported);
        if (it == ihOf.end()) continue;
        material += hex64(it->second);
        material += '\n';
    }
    return hex64(fnv1a64(material));
}

bool incrementalCacheLoad(const fs::path& cachePath,
                          std::vector<IncrementalUnitEntry>& out) {
    std::ifstream in(cachePath);
    if (!in) return false;
    std::string line;
    if (!std::getline(in, line)) return false;
    if (line != kIncrementalCacheFormat) return false;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::size_t at = 0;
        IncrementalUnitEntry entry;
        if (!takeStr(line, at, entry.unitKey)) continue;
        if (!takeStr(line, at, entry.sourceHash)) continue;
        if (!takeStr(line, at, entry.ihFingerprint)) continue;
        if (!takeStr(line, at, entry.classMethodHash)) continue;
        if (!takeStr(line, at, entry.configHash)) continue;
        if (!takeStr(line, at, entry.interfaceHash)) continue;
        if (!takeStr(line, at, entry.fragment.sourceKey)) continue;
        if (!takeStr(line, at, entry.fragment.namesp)) continue;
        if (!takeStr(line, at, entry.fragment.globals)) continue;
        if (!takeStr(line, at, entry.fragment.decls)) continue;
        if (!takeStr(line, at, entry.fragment.defs)) continue;
        if (!takeStr(line, at, entry.fragment.mainBody)) continue;
        std::string flags;
        if (!takeStr(line, at, flags)) continue;
        // flags: bit 0 isEntry, bit 1 isPackage, bit 2 topLevelAwait
        unsigned bits = 0;
        try {
            bits = static_cast<unsigned>(std::stoul(flags));
        } catch (const std::exception&) {
            continue;
        }
        entry.fragment.isEntry = (bits & 1u) != 0;
        entry.fragment.isPackage = (bits & 2u) != 0;
        entry.fragment.topLevelAwait = (bits & 4u) != 0;
        out.push_back(std::move(entry));
    }
    return true;
}

bool incrementalCacheStore(const fs::path& cachePath,
                           const std::vector<IncrementalUnitEntry>& entries) {
    std::string out;
    out += kIncrementalCacheFormat;
    out += '\n';
    for (const IncrementalUnitEntry& entry : entries) {
        std::string line;
        putStr(line, entry.unitKey);
        putStr(line, entry.sourceHash);
        putStr(line, entry.ihFingerprint);
        putStr(line, entry.classMethodHash);
        putStr(line, entry.configHash);
        putStr(line, entry.interfaceHash);
        putStr(line, entry.fragment.sourceKey);
        putStr(line, entry.fragment.namesp);
        putStr(line, entry.fragment.globals);
        putStr(line, entry.fragment.decls);
        putStr(line, entry.fragment.defs);
        putStr(line, entry.fragment.mainBody);
        unsigned bits = 0;
        if (entry.fragment.isEntry) bits |= 1u;
        if (entry.fragment.isPackage) bits |= 2u;
        if (entry.fragment.topLevelAwait) bits |= 4u;
        putStr(line, std::to_string(bits));
        out += line;
        out += '\n';
    }
    return writeTextFile(cachePath, out, nullptr);
}

}  // namespace cp3::cli
