// =============================================================================
// src/cli/src/commands/link.cpp - `cp3c link` and `cp3c unlink`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdLink and
// cp3::cli::cmdUnlink.
//
// A link is local/private: a directory package is used in place (no copy, so
// edits reach the next build), while a `.cp3pak` is immutable input extracted
// into .cp3/link/<name>/ as a refreshable link cache. Neither path touches a
// registry. `unlink` removes only the lock entry and the link cache, never the
// user's source directory or artifact.
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/cli.hpp"

#include "detail/commands.hpp"
#include "detail/install.hpp"
#include "detail/json.hpp"
#include "detail/link.hpp"
#include "detail/lockfile.hpp"
#include "detail/package_json.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

void printLinkJson(const char* command, const std::vector<LockEntry>& entries) {
    std::string out = "{\"command\":";
    out += jsonQuote(command);
    out += ",\"links\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const LockEntry& entry = entries[i];
        if (i != 0) out += ",";
        out += "{";
        out += "\"name\":" + jsonQuote(entry.name);
        out += ",\"version\":" + jsonQuote(entry.version);
        out += ",\"kind\":" + jsonQuote(entry.kind);
        out += ",\"source\":" + jsonQuote(entry.resolved);
        out += ",\"target\":" + jsonQuote(entry.target);
        if (!entry.integrity.empty()) {
            out += ",\"integrity\":" + jsonQuote(entry.integrity);
        }
        out += "}";
    }
    out += "]}\n";
    std::fputs(out.c_str(), stdout);
}

void printLinkListLine(const LockEntry& entry) {
    std::fprintf(stdout, "%s %s %s %s -> %s\n", entry.name.c_str(),
                 entry.version.c_str(), entry.kind.c_str(),
                 entry.resolved.c_str(), entry.target.c_str());
}

void printLinkLine(const LockEntry& entry) {
    printLinkListLine(entry);
    if (entry.kind == kLinkArtifact) {
        std::fprintf(stderr, "cp3c: linked %s %s (artifact cache at %s)\n",
                     entry.name.c_str(), entry.version.c_str(),
                     entry.target.c_str());
    } else {
        std::fprintf(stderr, "cp3c: linked %s %s (directory %s)\n",
                     entry.name.c_str(), entry.version.c_str(),
                     entry.resolved.c_str());
    }
}

bool readProjectLock(fs::path& projectRoot, std::vector<LockEntry>& lock,
                     std::string& err) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!findPackageRoot(cwd, projectRoot)) {
        err = "no package.json found in " + cwd.string() +
              " or any parent; run 'cp3c init' first";
        return false;
    }
    return readLockfile(projectRoot, lock, err);
}

int listLinks(const CliOptions& opt) {
    fs::path projectRoot;
    std::vector<LockEntry> lock;
    std::string err;
    if (!readProjectLock(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    std::vector<LockEntry> links;
    for (const LockEntry& entry : lock) {
        if (isLinkEntry(entry)) links.push_back(entry);
    }
    if (opt.json) {
        printLinkJson("link", links);
        return kExitSuccess;
    }
    if (links.empty()) {
        std::fprintf(stderr, "cp3c: no linked packages in %s\n",
                     projectRoot.string().c_str());
        return kExitSuccess;
    }
    for (const LockEntry& entry : links) printLinkListLine(entry);
    return kExitSuccess;
}

bool sameLinkTarget(const LockEntry& existing, const fs::path& source) {
    return existing.resolved == source.generic_string();
}

int createLink(const CliOptions& opt) {
    fs::path projectRoot;
    std::vector<LockEntry> lock;
    std::string err;
    if (!readProjectLock(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    std::error_code ec;
    fs::path source = opt.positionals.front();
    if (!source.is_absolute()) source = fs::absolute(source, ec);
    fs::path canonical = fs::weakly_canonical(source, ec);
    if (!ec) source = canonical;
    std::string name;
    std::string version;
    if (!inspectPackageSource(source, name, version, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (!opt.name.empty()) name = opt.name;
    const LockEntry* existing = findLockEntry(lock, name);
    if (existing != nullptr && !isLinkEntry(*existing) && !opt.force) {
        std::fprintf(stderr,
                     "cp3c: '%s' is already installed; pass --force to replace "
                     "it with a link\n",
                     name.c_str());
        return kExitUsageError;
    }
    const std::string expectedKind =
        fs::is_directory(source, ec) ? kLinkDirectory : kLinkArtifact;
    if (existing != nullptr && isLinkEntry(*existing) &&
        (!sameLinkTarget(*existing, source) || existing->kind != expectedKind) &&
        !opt.force) {
        std::fprintf(stderr,
                     "cp3c: '%s' is already linked to %s; pass --force to "
                     "replace it\n",
                     name.c_str(), existing->resolved.c_str());
        return kExitUsageError;
    }
    LinkRequest request;
    request.source = source;
    request.projectRoot = projectRoot;
    request.nameOverride = opt.name;
    request.force = opt.force;
    request.existing = existing;
    LinkResult result;
    if (!linkPackage(request, result, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    upsertLockEntry(lock, result.entry);
    if (!writeLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (opt.json) {
        printLinkJson("link", {result.entry});
    } else {
        if (result.reusedCache) {
            std::fprintf(stderr, "cp3c: reusing unchanged link cache %s\n",
                         result.entry.target.c_str());
        }
        printLinkLine(result.entry);
    }
    return kExitSuccess;
}

}  // namespace

int cmdLink(const CliOptions& opt) {
    if (opt.listLinks) return listLinks(opt);
    if (opt.positionals.empty()) {
        std::fprintf(stderr,
                     "cp3c: link needs a package directory or .cp3pak (or "
                     "--list)\n");
        return kExitUsageError;
    }
    return createLink(opt);
}

int cmdUnlink(const CliOptions& opt) {
    if (opt.positionals.empty()) {
        std::fprintf(stderr, "cp3c: unlink needs a package name\n");
        return kExitUsageError;
    }
    fs::path projectRoot;
    std::vector<LockEntry> lock;
    std::string err;
    if (!readProjectLock(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    const std::string name = opt.positionals[0];
    const LockEntry* entry = findLockEntry(lock, name);
    if (entry == nullptr || !isLinkEntry(*entry)) {
        std::fprintf(stderr, "cp3c: '%s' is not linked\n", name.c_str());
        return kExitUsageError;
    }
    const std::string kind = entry->kind;
    removeLinkCache(projectRoot, name);
    for (std::size_t i = 0; i < lock.size(); ++i) {
        if (lock[i].name == name) {
            lock.erase(lock.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    if (!writeLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (opt.json) {
        std::string out = "{\"command\":\"unlink\",\"name\":";
        out += jsonQuote(name);
        out += ",\"kind\":" + jsonQuote(kind);
        out += "}\n";
        std::fputs(out.c_str(), stdout);
    } else {
        std::fprintf(stderr, "cp3c: unlinked %s (%s)\n", name.c_str(),
                     kind.c_str());
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
