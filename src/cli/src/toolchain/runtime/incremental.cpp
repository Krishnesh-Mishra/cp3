// =============================================================================
// src/cli/src/toolchain/runtime/incremental.cpp - per-module incremental reuse
// (DEFINITIONS).
// =============================================================================
#include "detail/toolchain/incremental.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "detail/core/util.hpp"
#include "detail/cache/cache.hpp"
#include "detail/toolchain/interface_hash.hpp"
#include "cp3/modules.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/core/types.hpp"

namespace cp3::cli {

namespace {

// The incremental fragment cache keys fragments on compilerKey|profile. The
// compilerKey covers the clang toolchain only; cp3c's own semantics are not
// captured by it. Revisions that change sema/codegen diagnostics would keep
// expensive caches warm for sources the new semantics reject, silently
// replaying stale fragments with exit codes the new diagnostics never produce
// (the namespace-import case: fragments from the pre-CP3304 compiler replayed
// with no diagnostics and exit 0, oscillating with cache presence). Bump this
// constant on any commit that changes sema or codegen diagnostics.
constexpr unsigned kIncrementalSemanticsEpoch = 2;

// Length-prefixed field writer/reader: "<len>:<bytes>".
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

std::vector<std::string> splitNewline(const std::string& text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) {
            out.push_back(text.substr(at));
            break;
        }
        out.push_back(text.substr(at, nl - at));
        at = nl + 1;
    }
    return out;
}

std::string joinNewline(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += item;
        out += '\n';
    }
    return out;
}

std::string hexEncode(const std::string& text) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() * 2);
    for (unsigned char c : text) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0xF]);
    }
    return out;
}

bool hexDecode(const std::string& text, std::string& out) {
    out.clear();
    if (text.size() % 2 != 0) return false;
    out.reserve(text.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

std::uint64_t fnv1a(const std::string& text, std::uint64_t hash) {
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

// The class/interface/alias/enum declarations of one unit, in source order,
// unwrapping `export { }` and `export default` wrappers.
std::vector<const Stmt*> classLikeDeclarations(const Program& program) {
    std::vector<const Stmt*> out;
    for (const StmtPtr& stmt : program.body) {
        if (!stmt) continue;
        const Stmt* inner = stmt.get();
        if (inner->kind == NodeKind::ExportDeclaration) {
            const auto& ed = static_cast<const ExportDeclaration&>(*inner);
            if (ed.declaration) inner = ed.declaration.get();
            else continue;
        }
        switch (inner->kind) {
            case NodeKind::ClassDeclaration:
            case NodeKind::InterfaceDeclaration:
            case NodeKind::TypeAliasDeclaration:
            case NodeKind::EnumDeclaration:
                out.push_back(inner);
                break;
            default:
                break;
        }
    }
    return out;
}

// One unit's contribution to the program shape hash: a canonical one-line
// digest of every class/interface/alias/enum it declares. Deterministic per
// source text.
std::string unitShapeText(const CompilationUnit& unit) {
    if (!unit.ast) return {};
    std::string material;
    for (const Stmt* decl : classLikeDeclarations(*unit.ast)) {
        switch (decl->kind) {
            case NodeKind::ClassDeclaration:
                material += classDeclToInterfaceShape(
                                static_cast<const ClassDeclaration&>(*decl));
                material += '\n';
                break;
            case NodeKind::InterfaceDeclaration:
                material += interfaceDeclToInterfaceShape(
                                static_cast<const InterfaceDeclaration&>(*decl));
                material += '\n';
                break;
            case NodeKind::TypeAliasDeclaration:
                material += aliasDeclToInterfaceShape(
                                static_cast<const TypeAliasDeclaration&>(*decl));
                material += '\n';
                break;
            case NodeKind::EnumDeclaration: {
                const auto& e = static_cast<const EnumDeclaration&>(*decl);
                material += "enum:";
                material += e.name;
                material += '\n';
                for (const EnumMemberPtr& member : e.members) {
                    if (!member) continue;
                    material += "  ";
                    material += member->name;
                    material += '\n';
                }
                break;
            }
            default:
                break;
        }
    }
    return material;
}

}  // namespace

std::string incrementalHex64(std::uint64_t value) {
    return hex64(value);
}

fs::path incrementalCachePathFor(const fs::path& entryFile) {
    const fs::path projectRoot = findProjectRoot(entryFile);
    const fs::path cacheDir = projectRoot / ".cp3" / "cache";
    std::error_code ec;
    fs::create_directories(cacheDir, ec);
    return cacheDir / kIncrementalCacheFile;
}

std::string incrementalUnitKey(const CompilationUnit& unit) {
    // The logical module name is the graph's stable identity for a unit
    // (project-relative for local files; the specifier for packages).
    return unit.moduleName.empty() ? "<anonymous>" : unit.moduleName;
}

std::string unitSourceHash(const CompilationUnit& unit) {
    if (unit.file == nullptr) return hex64(0);
    return hex64(fnv1a(unit.file->text));
}

std::string configHashOf(const std::string& compilerKey,
                         const std::string& profile) {
    std::string material = compilerKey;
    material += '|';
    material += profile;
    material += '|';
    material += std::to_string(kIncrementalSemanticsEpoch);
    return hex64(fnv1a(material));
}

std::string programShapeHash(const ModuleGraph& graph) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const CompilationUnit* unit : graph.compilationOrder()) {
        if (unit == nullptr || !unit->ast) continue;
        hash ^= 0xff;
        hash *= 0x100000001b3ull;
        hash = fnv1a(unitShapeText(*unit), hash);
    }
    return hex64(hash);
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
    return hex64(fnv1a(material));
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
        std::string needsMask, nativeHeaders, flags;
        if (!takeStr(line, at, entry.unitKey)) continue;
        if (!takeStr(line, at, entry.sourceHash)) continue;
        if (!takeStr(line, at, entry.ihFingerprint)) continue;
        if (!takeStr(line, at, entry.shapeHash)) continue;
        if (!takeStr(line, at, entry.configHash)) continue;
        if (!takeStr(line, at, entry.interfaceHash)) continue;
        if (!takeStr(line, at, entry.fragment.sourceKey)) continue;
        if (!takeStr(line, at, entry.fragment.namesp)) continue;
        std::string raw;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, entry.fragment.globals)) continue;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, entry.fragment.decls)) continue;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, entry.fragment.defs)) continue;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, entry.fragment.mainBody)) continue;
        if (!takeStr(line, at, needsMask)) continue;
        if (!takeStr(line, at, nativeHeaders)) continue;
        if (!takeStr(line, at, flags)) continue;
        entry.fragment.needsMask = 0;
        bool needsOk = true;
        std::stringstream bits(needsMask);
        std::string bit;
        while (bits >> bit) {
            try {
                entry.fragment.needsMask |=
                    (std::uint64_t{1} << std::stoul(bit));
            } catch (const std::exception&) {
                needsOk = false;
                break;
            }
        }
        if (!needsOk) continue;  // drop malformed records entirely
        std::string decodedHeaders;
        if (!hexDecode(nativeHeaders, decodedHeaders)) continue;
        entry.fragment.nativeHeaders = splitNewline(decodedHeaders);
        unsigned b = 0;
        try {
            b = static_cast<unsigned>(std::stoul(flags));
        } catch (const std::exception&) {
            continue;
        }
        entry.fragment.isEntry = (b & 1u) != 0;
        entry.fragment.isPackage = (b & 2u) != 0;
        entry.fragment.topLevelAwait = (b & 4u) != 0;
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
        putStr(line, entry.shapeHash);
        putStr(line, entry.configHash);
        putStr(line, entry.interfaceHash);
        putStr(line, entry.fragment.sourceKey);
        putStr(line, entry.fragment.namesp);
        putStr(line, hexEncode(entry.fragment.globals));
        putStr(line, hexEncode(entry.fragment.decls));
        putStr(line, hexEncode(entry.fragment.defs));
        putStr(line, hexEncode(entry.fragment.mainBody));
        std::string needs;
        const std::uint64_t mask = entry.fragment.needsMask;
        for (int bit = 0; bit < 64; ++bit) {
            if (mask & (std::uint64_t{1} << bit)) {
                if (!needs.empty()) needs += ' ';
                needs += std::to_string(bit);
            }
        }
        putStr(line, needs);
        putStr(line, hexEncode(joinNewline(entry.fragment.nativeHeaders)));
        unsigned bits = 0;
        if (entry.fragment.isEntry) bits |= 1u;
        if (entry.fragment.isPackage) bits |= 2u;
        if (entry.fragment.topLevelAwait) bits |= 4u;
        putStr(line, std::to_string(bits));
        out += line;
        out += '\n';
    }
    // Write to a sibling temp file and rename so a crash mid-write cannot
    // leave a torn cache; a missing or stale file on the next run is simply
    // a miss (the codegen re-emits and rewrites it). On Windows rename does
    // not replace an existing destination, so remove the old file first;
    // the pre-rewrite copy is a best-effort cache, never the only state.
    const fs::path tmp = fs::path(cachePath).string() + ".tmp";
    if (!writeTextFile(tmp, out, nullptr)) {
        return false;
    }
    std::error_code rmEc;
    fs::remove(cachePath, rmEc);  // best effort; stale cache is not a blocker
    std::error_code ec;
    fs::rename(tmp, cachePath, ec);
    return !ec;
}

// Computes the per-unit reuse decisions for one plural build. Reads the
// cache, re-derives every cheap-to-verify key part, keeps only records that
// match, and rewrites the cache with only those records (stale units are
// pruned). Records missing the fingerprints the CLI can compute (fresh
// records written by codegen) are stamped with the current values. Returns
// the number of kept entries, 0 when the cache was unusable, or -1 when the
// rewrite failed (the next run regenerates).
int prepareIncrementalReuse(const fs::path& cachePath,
                            const ModuleGraph& graph,
                            const std::string& compilerKey,
                            const std::string& profile,
                            const std::unordered_map<std::uint32_t, std::uint64_t>& ihOf) {
    std::vector<IncrementalUnitEntry> cached;
    if (!incrementalCacheLoad(cachePath, cached)) cached.clear();

    const std::string configHash = configHashOf(compilerKey, profile);
    const std::string shapeHash = programShapeHash(graph);

    std::unordered_map<std::string, IncrementalUnitEntry> entriesByKey;
    entriesByKey.reserve(cached.size());
    for (IncrementalUnitEntry& entry : cached) {
        entriesByKey.emplace(entry.unitKey, std::move(entry));
    }

    std::vector<IncrementalUnitEntry> kept;
    kept.reserve(graph.units().size());
    std::unordered_set<std::string> seen;
    for (const CompilationUnit* unit : graph.compilationOrder()) {
        if (unit == nullptr || !unit->ast) continue;
        if (unit->isPrelude && !unit->isEntry) continue;
        const std::string key = incrementalUnitKey(*unit);
        if (!seen.insert(key).second) continue;
        const auto it = entriesByKey.find(key);
        if (it == entriesByKey.end()) continue;
        IncrementalUnitEntry entry = std::move(it->second);
        if (entry.sourceHash != unitSourceHash(*unit)) continue;
        if (!entry.configHash.empty() && entry.configHash != configHash) {
            continue; // toolchain changed since the fragment was emitted
        }
        if (!entry.shapeHash.empty() && entry.shapeHash != shapeHash) {
            continue; // some class/enum changed shape anywhere in the program
        }
        const std::string ih = ihFingerprintOf(graph, *unit, ihOf);
        if (!entry.ihFingerprint.empty() && entry.ihFingerprint != ih) {
            continue; // some local/package dependency's interface changed
        }
        // Stamp the parts codegen cannot compute (or that were empty on a
        // fresh record). A record emitted from this build's AST is by
        // definition consistent with this build's interfaces.
        entry.configHash = configHash;
        entry.shapeHash = shapeHash;
        entry.ihFingerprint = ih;
        const auto unitIh = ihOf.find(unit->id);
        if (unitIh != ihOf.end()) {
            entry.interfaceHash = hex64(unitIh->second);
        }
        kept.push_back(std::move(entry));
    }

    if (!incrementalCacheStore(cachePath, kept)) {
        // The rewrite is best effort; the reuse file is regenerated on the
        // next run. A stale cache with stale entries cannot cause a wrong
        // hit because the splice re-verifies the source hash.
        return -1;
    }
    return static_cast<int>(kept.size());
}

}  // namespace cp3::cli
