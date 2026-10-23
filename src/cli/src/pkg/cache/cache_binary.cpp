// =============================================================================
// src/cli/src/pkg/cache_binary.cpp - the precompiled binary cache DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/cache_binary.hpp.
//
// The cache is described twice, on purpose: the per-platform manifest inside
// the archive says what one platform's artifact is (and how it was built),
// while the container manifest (cp3pak.json) says which platforms the archive
// carries and which platforms the package claims. A consumer therefore never
// has to guess.
// =============================================================================
#include "detail/cache/cache_binary.hpp"

#include <algorithm>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

// The member name of a platform's cache manifest.
constexpr const char* kCacheManifestName = "cache.json";

// The prefix every cache member lives under.
constexpr const char* kPakPrefix = "pak/";

std::string artifactToJson(const PlatformCacheArtifact& artifact) {
    std::string out = "{";
    out += "\"module\":" + jsonQuote(artifact.module);
    out += ",\"member\":" + jsonQuote(artifact.member);
    out += ",\"sourceHash\":" + jsonQuote(artifact.sourceHash);
    out += ",\"headerHash\":" + jsonQuote(artifact.headerHash);
    out += "}";
    return out;
}

bool parseArtifact(const JsonValue& value, PlatformCacheArtifact& out,
                   std::string& err) {
    if (!value.isObject()) {
        err = "cache.json: an artifact entry must be an object";
        return false;
    }
    const std::string* module = value.stringField("module");
    const std::string* member = value.stringField("member");
    const std::string* sourceHash = value.stringField("sourceHash");
    const std::string* headerHash = value.stringField("headerHash");
    if (module == nullptr || member == nullptr || sourceHash == nullptr ||
        headerHash == nullptr) {
        err = "cache.json: an artifact entry needs module, member, sourceHash "
              "and headerHash";
        return false;
    }
    out.module = *module;
    out.member = *member;
    out.sourceHash = *sourceHash;
    out.headerHash = *headerHash;
    return true;
}

// The "<module>" part of a "pak/<module>.cpp" member name.
std::string moduleOfSourceMember(const std::string& name) {
    std::string stem = name.substr(std::string(kPakPrefix).size());
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".cpp") == 0) {
        stem.resize(stem.size() - 4);
    }
    return stem;
}

}  // namespace

std::string pakSourceModule(const std::string& name) {
    return moduleOfSourceMember(name);
}

bool parseCacheBinary(const JsonValue& manifest, CacheBinaryPlan& out,
                      std::string& err) {
    out = CacheBinaryPlan{};
    const JsonValue* build = manifest.find("build");
    if (build == nullptr || !build->isObject()) return true;
    const JsonValue* value = build->find("cacheBinary");
    if (value == nullptr) return true;
    out.requested = true;
    if (value->type == JsonValue::Type::Bool) {
        if (!value->boolean) return true;  // `false` opts out
        PlatformId here;
        here.family = currentPlatform().family;
        here.arch = currentPlatform().arch;
        out.claims.push_back(here);
        return true;
    }
    if (value->type != JsonValue::Type::Array) {
        err = "build.cacheBinary must be true or an array of platform ids";
        return false;
    }
    std::vector<std::string> ids;
    for (const JsonValue& item : value->array) {
        if (item.type != JsonValue::Type::String) {
            err = "build.cacheBinary entries must be platform id strings";
            return false;
        }
        ids.push_back(item.string);
    }
    std::string parseError;
    out.claims = normalizePlatformIds(ids, parseError);
    if (!parseError.empty()) {
        err = "build.cacheBinary: " + parseError;
        return false;
    }
    return true;
}

bool excludeSourceRequested(const JsonValue& manifest) {
    const JsonValue* build = manifest.find("build");
    if (build == nullptr) return false;
    const JsonValue* flag = build->find("excludeSource");
    return flag != nullptr && flag->type == JsonValue::Type::Bool &&
           flag->boolean;
}

std::string platformCacheManifestToJson(const PlatformCacheManifest& manifest) {
    std::string out = "{\n";
    out += "  \"formatVersion\": " + std::to_string(manifest.formatVersion) +
           ",\n";
    out += "  \"platform\": " + jsonQuote(manifest.platform) + ",\n";
    out += "  \"abi\": " + jsonQuote(manifest.abi) + ",\n";
    out += "  \"kind\": " + jsonQuote(manifest.kind) + ",\n";
    out += "  \"artifacts\": [";
    for (std::size_t i = 0; i < manifest.artifacts.size(); ++i) {
        if (i != 0) out += ",";
        out += artifactToJson(manifest.artifacts[i]);
    }
    out += "],\n";
    out += "  \"compiler\": " + jsonQuote(manifest.compiler) + ",\n";
    out += "  \"compilerId\": " + jsonQuote(manifest.compilerId) + ",\n";
    out += "  \"compilerVersion\": " + jsonQuote(manifest.compilerVersion) +
           ",\n";
    out += "  \"compilerPath\": " + jsonQuote(manifest.compilerPath) + ",\n";
    out += "  \"profile\": " + jsonQuote(manifest.profile) + "\n";
    out += "}\n";
    return out;
}

bool parsePlatformCacheManifest(const std::string& text,
                                PlatformCacheManifest& out, std::string& err) {
    JsonValue doc;
    if (!JsonValue::parse(text, doc, err)) {
        err = "invalid cache.json: " + err;
        return false;
    }
    out = PlatformCacheManifest{};
    const JsonValue* version = doc.find("formatVersion");
    if (version != nullptr && version->type == JsonValue::Type::Number) {
        out.formatVersion = static_cast<int>(version->number);
    }
    const std::string* platform = doc.stringField("platform");
    if (platform == nullptr || platform->empty()) {
        err = "cache.json: missing platform";
        return false;
    }
    out.platform = *platform;
    if (const std::string* abi = doc.stringField("abi")) out.abi = *abi;
    if (const std::string* kind = doc.stringField("kind")) out.kind = *kind;
    if (const std::string* compiler = doc.stringField("compiler")) {
        out.compiler = *compiler;
    }
    if (const std::string* id = doc.stringField("compilerId")) {
        out.compilerId = *id;
    }
    if (const std::string* versionText = doc.stringField("compilerVersion")) {
        out.compilerVersion = *versionText;
    }
    if (const std::string* path = doc.stringField("compilerPath")) {
        out.compilerPath = *path;
    }
    if (const std::string* profile = doc.stringField("profile")) {
        out.profile = *profile;
    }
    const JsonValue* artifacts = doc.arrayField("artifacts");
    if (artifacts == nullptr) return true;
    for (const JsonValue& item : artifacts->array) {
        PlatformCacheArtifact artifact;
        if (!parseArtifact(item, artifact, err)) return false;
        out.artifacts.push_back(artifact);
    }
    return true;
}

std::string platformCacheManifestMember(const PlatformId& platform) {
    return std::string(kPakPrefix) + platformIdText(platform) + "/" +
           kCacheManifestName;
}

std::string platformCacheArtifactMember(const PlatformId& platform,
                                         const std::string& module) {
    return std::string(kPakPrefix) + platformIdText(platform) + "/" + module +
           ".o";
}

std::vector<PlatformId> archiveCachedPlatforms(const std::vector<TarEntry>& members) {
    std::vector<PlatformId> out;
    for (const TarEntry& member : members) {
        if (!endsWith(member.name, "/" + std::string(kCacheManifestName))) {
            continue;
        }
        PlatformId id;
        std::string err;
        const std::string dir = member.name.substr(
            std::string(kPakPrefix).size(),
            member.name.size() - std::string(kPakPrefix).size() -
                std::string(kCacheManifestName).size() - 1);
        if (!parsePlatformId(dir, id, err)) continue;
        const bool known = std::any_of(
            out.begin(), out.end(), [&](const PlatformId& seen) {
                return seen.family == id.family && seen.arch == id.arch;
            });
        if (!known) out.push_back(id);
    }
    return out;
}

bool archiveFindPlatformCache(const std::vector<TarEntry>& members,
                              const PlatformId& platform,
                              PlatformCacheManifest& out) {
    const std::string name = platformCacheManifestMember(platform);
    for (const TarEntry& member : members) {
        if (member.name != name) continue;
        std::string err;
        if (!parsePlatformCacheManifest(member.data, out, err)) return false;
        return true;
    }
    return false;
}

bool archiveHasPlatformCache(const std::vector<TarEntry>& members,
                             const PlatformId& platform) {
    const std::string name = platformCacheManifestMember(platform);
    for (const TarEntry& member : members) {
        if (member.name == name) return true;
    }
    return false;
}

bool claimsSatisfied(const std::vector<PlatformId>& claims,
                     const std::vector<PlatformId>& cached) {
    return unsatisfiedClaims(claims, cached).empty();
}

std::vector<std::string> unsatisfiedClaims(const std::vector<PlatformId>& claims,
                                           const std::vector<PlatformId>& cached) {
    std::vector<std::string> missing;
    for (const PlatformId& claim : claims) {
        const bool covered = std::any_of(
            cached.begin(), cached.end(), [&](const PlatformId& have) {
                return platformSatisfies(claim, have);
            });
        if (!covered) missing.push_back(platformIdText(claim));
    }
    return missing;
}

bool isPakSourceMember(const std::string& name) {
    if (name.rfind(kPakPrefix, 0) != 0) return false;
    return endsWith(name, ".cpp");
}

std::vector<const TarEntry*> archiveSourceMembers(const std::vector<TarEntry>& members) {
    std::vector<const TarEntry*> out;
    for (const TarEntry& member : members) {
        if (isPakSourceMember(member.name)) out.push_back(&member);
    }
    return out;
}

bool archiveHasSource(const std::vector<TarEntry>& members) {
    return !archiveSourceMembers(members).empty();
}

std::size_t removePlatformCacheMembers(std::vector<TarEntry>& members,
                                       const PlatformId& platform) {
    const std::string prefix =
        std::string(kPakPrefix) + platformIdText(platform) + "/";
    std::size_t index = members.size();
    std::vector<TarEntry> kept;
    kept.reserve(members.size());
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (members[i].name.rfind(prefix, 0) == 0) {
            if (index == members.size()) index = kept.size();
            continue;
        }
        kept.push_back(std::move(members[i]));
    }
    members = std::move(kept);
    return index;
}

std::size_t pakInsertionIndex(const std::vector<TarEntry>& members) {
    std::size_t index = 0;
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (members[i].name.rfind(kPakPrefix, 0) == 0) index = i + 1;
    }
    return index;
}

}  // namespace cp3::cli
