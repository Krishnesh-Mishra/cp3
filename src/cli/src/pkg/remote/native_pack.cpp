// =============================================================================
// src/cli/src/pkg/native_pack.cpp - native assets in a `.cp3pak` DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/pkg/native_pack.hpp.
// =============================================================================
#include "detail/pkg/native_pack.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cp3/host/native_bindings.hpp"

#include "detail/core/util.hpp"
#include "native/libclang/metadata.hpp"

namespace cp3::cli {

namespace {

namespace fs = std::filesystem;

constexpr const char* kNativePrefix = ".cp3/native/";

bool isPlainBindingId(const std::string& id) {
    if (id.empty() || id == "." || id == "..") return false;
    for (const char c : id) {
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '\'') {
            return false;
        }
    }
    return true;
}

fs::path resolvedPath(const fs::path& root, const std::string& recorded) {
    const fs::path path(recorded);
    if (path.is_absolute()) return path.lexically_normal();
    return (root / path).lexically_normal();
}

std::string canonicalText(const fs::path& path) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(path, ec);
    return (ec ? path.lexically_normal() : canonical).generic_string();
}

// One binding being packed: its archive prefix and the files already emitted,
// so an explicit library that also sits inside a copied lib directory is not
// stored twice.
struct BindingPack {
    const fs::path& projectRoot;
    std::string id;
    std::string root;
    std::vector<TarEntry>& entries;
    std::set<std::string> members;
    std::map<std::string, std::string> copied;
};

bool addFile(BindingPack& pack, const fs::path& source,
             const std::string& member, std::string& err) {
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        err = "the native binding '" + pack.id + "' records '" +
              source.generic_string() + "', which is not a regular file";
        return false;
    }
    const std::string key = canonicalText(source);
    const auto seen = pack.copied.find(key);
    if (seen != pack.copied.end()) return true;
    if (!pack.members.insert(member).second) {
        err = "the native binding '" + pack.id +
              "' produced the archive member '" + member + "' twice";
        return false;
    }
    std::string data;
    if (!readTextFile(source, data)) {
        err = "cannot read native asset '" + source.generic_string() + "'";
        return false;
    }
    pack.copied.emplace(key, member);
    pack.entries.push_back(TarEntry{member, std::move(data)});
    return true;
}

// Copies every regular file under `sourceDir` into `prefix`, preserving the
// tree's relative layout. A missing directory is a pack error: the archive
// would otherwise install but fail to compile or link.
bool addTree(BindingPack& pack, const fs::path& sourceDir,
             const std::string& prefix, std::string& err) {
    std::error_code ec;
    if (!fs::is_directory(sourceDir, ec)) {
        err = "the native binding '" + pack.id + "' records the directory '" +
              sourceDir.generic_string() + "', which does not exist";
        return false;
    }
    if (canonicalText(sourceDir) == canonicalText(pack.projectRoot)) {
        err = "the native binding '" + pack.id +
              "' records the project root as an include/library directory; "
              "vendor the SDK under native/ first";
        return false;
    }
    for (fs::recursive_directory_iterator it(sourceDir, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            err = "cannot scan native directory '" + sourceDir.generic_string() +
                  "' (" + ec.message() + ")";
            return false;
        }
        if (!it->is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(it->path(), sourceDir, ec);
        if (ec) {
            err = "cannot relativize native asset '" +
                  it->path().generic_string() + "'";
            return false;
        }
        const std::string member =
            prefix + "/" + rel.generic_string();
        if (!addFile(pack, it->path(), member, err)) return false;
    }
    return true;
}

// An explicit library/DLL/source file. A numbered directory keeps two files
// with the same name from colliding while preserving the original basename -
// Windows resolves a linked DLL by name beside the executable.
std::string explicitMember(BindingPack& pack, const std::string& group,
                           std::size_t index, const fs::path& source) {
    std::string name = source.filename().generic_string();
    if (name.empty()) name = "file";
    return pack.root + "/" + group + "/" + std::to_string(index) + "/" + name;
}

bool addExplicitFiles(BindingPack& pack, const std::vector<std::string>& paths,
                      const std::string& group,
                      std::vector<std::string>& rewritten, std::string& err) {
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const fs::path source = resolvedPath(pack.projectRoot, paths[i]);
        const std::string key = canonicalText(source);
        const auto seen = pack.copied.find(key);
        if (seen != pack.copied.end()) {
            rewritten.push_back(seen->second);
            continue;
        }
        const std::string member = explicitMember(pack, group, i, source);
        if (!addFile(pack, source, member, err)) return false;
        rewritten.push_back(member);
    }
    return true;
}

bool addIncludeDirs(BindingPack& pack, const NativeModule& module,
                    NativeModule& rewritten, std::string& err) {
    for (std::size_t i = 0; i < module.includeDirs.size(); ++i) {
        const fs::path source =
            resolvedPath(pack.projectRoot, module.includeDirs[i]);
        const std::string member =
            pack.root + "/include/" + std::to_string(i);
        if (!addTree(pack, source, member, err)) return false;
        rewritten.includeDirs.push_back(member);
    }
    return true;
}

bool addLibDirs(BindingPack& pack, const NativeModule& module,
                NativeModule& rewritten, std::string& err) {
    for (std::size_t i = 0; i < module.libDirs.size(); ++i) {
        const fs::path source =
            resolvedPath(pack.projectRoot, module.libDirs[i]);
        const std::string member = pack.root + "/lib/" + std::to_string(i);
        if (!addTree(pack, source, member, err)) return false;
        rewritten.libDirs.push_back(member);
    }
    return true;
}

bool packBinding(const fs::path& projectRoot, const std::string& id,
                 std::vector<TarEntry>& entries, std::string& err) {
    if (!isPlainBindingId(id)) {
        err = "native binding id '" + id + "' is not usable in an archive";
        return false;
    }
    const fs::path bindingDir = projectRoot / ".cp3" / "native" / fs::path(id);
    const NativeReadResult read = readNativeBinding(bindingDir);
    if (read.status != NativeReadStatus::Ok) {
        err = "cannot pack native binding '" + id + "': " + read.message;
        if (!read.remedy.empty()) err += "; " + read.remedy;
        return false;
    }
    std::string index;
    if (!readTextFile(bindingDir / "index.cp3", index)) {
        err = "cannot read the native binding declaration '" +
              (bindingDir / "index.cp3").generic_string() + "'";
        return false;
    }

    BindingPack pack{projectRoot, id, std::string(kNativePrefix) + id,
                     entries, {}, {}};
    NativeModule rewritten = read.module;
    rewritten.includeDirs.clear();
    rewritten.libDirs.clear();
    rewritten.staticLibraries.clear();
    rewritten.sharedLibraries.clear();
    rewritten.runtimeDlls.clear();

    if (!addIncludeDirs(pack, read.module, rewritten, err)) return false;
    if (!addLibDirs(pack, read.module, rewritten, err)) return false;
    if (!addExplicitFiles(pack, read.module.staticLibraries, "static",
                          rewritten.staticLibraries, err)) {
        return false;
    }
    if (!addExplicitFiles(pack, read.module.sharedLibraries, "shared",
                          rewritten.sharedLibraries, err)) {
        return false;
    }
    if (!addExplicitFiles(pack, read.module.runtimeDlls, "runtime",
                          rewritten.runtimeDlls, err)) {
        return false;
    }
    if (!read.module.source.empty()) {
        // `source` is descriptive: native-install records the SDK root, a
        // header path or even a pkg-config locator. Only a real file can be
        // archived; anything else stays as the author's note.
        const fs::path source = resolvedPath(projectRoot, read.module.source);
        std::error_code ec;
        if (fs::is_regular_file(source, ec)) {
            const std::string member =
                explicitMember(pack, "source", 0, source);
            if (!addFile(pack, source, member, err)) return false;
            rewritten.source = member;
        }
    }

    // The rewritten paths are relative to the project root, exactly like the
    // original metadata's paths, so the module loader needs no special case.
    entries.push_back(TarEntry{pack.root + "/native.json",
                               native::renderMetadata(rewritten, index)});
    entries.push_back(TarEntry{pack.root + "/index.cp3", std::move(index)});
    return true;
}

bool hasNativeImport(const std::string& text, const std::string& id) {
    const std::string needle = "import native";
    std::size_t at = 0;
    while ((at = text.find(needle, at)) != std::string::npos) {
        std::size_t pos = at + needle.size();
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' ||
                text[pos] == '\n')) {
            ++pos;
        }
        if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
            at = pos;
            continue;
        }
        const char quote = text[pos++];
        const std::size_t end = text.find(quote, pos);
        if (end == std::string::npos) return false;
        if (text.compare(pos, end - pos, id) == 0 &&
            end - pos == id.size()) {
            return true;
        }
        at = end + 1;
    }
    return false;
}

std::string importAlias(const std::string& id) {
    std::string out = "cp3Native_";
    for (const char c : id) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_';
        out.push_back(plain ? c : '_');
    }
    return out;
}

bool writeEntry(const fs::path& projectRoot, const TarEntry& entry,
                std::string& err) {
    const fs::path dest = projectRoot / fs::path(entry.name);
    if (!writeTextFile(dest, entry.data, nullptr)) {
        err = "cannot write native asset " + dest.generic_string();
        return false;
    }
    return true;
}

}  // namespace

bool isNativeArchiveMember(const std::string& name) {
    return name.rfind(kNativePrefix, 0) == 0 &&
           name.size() > std::string(kNativePrefix).size();
}

std::vector<std::string> nativeBindingIdsInArchive(
    const std::vector<TarEntry>& members) {
    std::vector<std::string> ids;
    const std::string suffix = "/native.json";
    for (const TarEntry& member : members) {
        if (!isNativeArchiveMember(member.name)) continue;
        if (member.name.size() <= suffix.size()) continue;
        if (member.name.compare(member.name.size() - suffix.size(),
                                suffix.size(), suffix) != 0) {
            continue;
        }
        const std::string rest = member.name.substr(
            std::string(kNativePrefix).size(),
            member.name.size() - std::string(kNativePrefix).size() -
                suffix.size());
        if (rest.empty() || rest.find('/') != std::string::npos) continue;
        if (std::find(ids.begin(), ids.end(), rest) == ids.end()) {
            ids.push_back(rest);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> nativeBindingIdsInText(const std::string& text) {
    std::vector<std::string> ids;
    const std::string needle = "import native";
    std::size_t at = 0;
    while ((at = text.find(needle, at)) != std::string::npos) {
        std::size_t pos = at + needle.size();
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' ||
                text[pos] == '\n')) {
            ++pos;
        }
        if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
            at = pos;
            continue;
        }
        const char quote = text[pos++];
        const std::size_t end = text.find(quote, pos);
        if (end == std::string::npos) break;
        const std::string id = text.substr(pos, end - pos);
        if (!id.empty() &&
            std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
        at = end + 1;
    }
    return ids;
}

std::vector<std::string> nativeBindingIdsInDirectory(
    const fs::path& packageRoot) {
    std::vector<std::string> ids;
    const fs::path root = packageRoot / ".cp3" / "native";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return ids;
    for (fs::directory_iterator it(root, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec)) continue;
        if (!fs::is_regular_file(it->path() / "native.json", ec)) continue;
        ids.push_back(it->path().filename().string());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool collectNativeBindingMembers(const fs::path& projectRoot,
                                 const std::vector<std::string>& ids,
                                 std::vector<TarEntry>& entries,
                                 std::string& err) {
    for (const std::string& id : ids) {
        if (!packBinding(projectRoot, id, entries, err)) return false;
    }
    return true;
}

bool installNativeMembers(const std::vector<TarEntry>& members,
                          const fs::path& projectRoot, std::string& err) {
    for (const TarEntry& member : members) {
        if (!isNativeArchiveMember(member.name)) continue;
        if (!writeEntry(projectRoot, member, err)) return false;
    }
    return true;
}

bool installNativeBindingsFromDirectory(const fs::path& packageRoot,
                                        const fs::path& projectRoot,
                                        std::string& err) {
    const std::vector<std::string> ids =
        nativeBindingIdsInDirectory(packageRoot);
    if (ids.empty()) return true;
    std::vector<TarEntry> entries;
    if (!collectNativeBindingMembers(packageRoot, ids, entries, err)) {
        return false;
    }
    for (const TarEntry& entry : entries) {
        if (!writeEntry(projectRoot, entry, err)) return false;
    }
    return true;
}

bool nativeBuildFlagsFromProject(const fs::path& projectRoot,
                                 const std::vector<std::string>& ids,
                                 std::vector<std::string>& includeDirs,
                                 std::vector<std::string>& defines,
                                 std::string& err) {
    for (const std::string& id : ids) {
        const fs::path bindingDir =
            projectRoot / ".cp3" / "native" / fs::path(id);
        const NativeReadResult read = readNativeBinding(bindingDir);
        if (read.status != NativeReadStatus::Ok) {
            err = "cannot read native binding '" + id + "': " + read.message;
            if (!read.remedy.empty()) err += "; " + read.remedy;
            return false;
        }
        for (const std::string& dir : read.module.includeDirs) {
            const std::string path =
                resolvedPath(projectRoot, dir).generic_string();
            if (std::find(includeDirs.begin(), includeDirs.end(), path) ==
                includeDirs.end()) {
                includeDirs.push_back(path);
            }
        }
        for (const std::string& define : read.module.defines) {
            if (std::find(defines.begin(), defines.end(), define) ==
                defines.end()) {
                defines.push_back(define);
            }
        }
    }
    return true;
}

bool nativeBuildFlagsFromArchive(const std::vector<TarEntry>& members,
                                 const fs::path& workRoot,
                                 std::vector<std::string>& includeDirs,
                                 std::vector<std::string>& defines,
                                 std::string& err) {
    if (!installNativeMembers(members, workRoot, err)) return false;
    return nativeBuildFlagsFromProject(
        workRoot, nativeBindingIdsInArchive(members), includeDirs, defines,
        err);
}

std::string ensureNativeImports(std::string surface,
                                const std::vector<std::string>& ids) {
    std::string prefix;
    for (const std::string& id : ids) {
        if (!isPlainBindingId(id)) continue;
        if (hasNativeImport(surface, id)) continue;
        prefix += "import native \"" + id + "\" as " + importAlias(id) + ";\n";
    }
    if (prefix.empty()) return surface;
    return prefix + surface;
}

}  // namespace cp3::cli
