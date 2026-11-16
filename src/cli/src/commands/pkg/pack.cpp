// =============================================================================
// src/cli/src/commands/pack.cpp - `cp3c build` for `"type": "package"` projects.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail::packProject.
//
// Output (CLI-README §5, D61): intermediates under .cp3/build/, the final
// artifact .cp3/out/<name>.cp3pak - a gzip-compressed archive carrying
//   cp3pak.json      the container manifest (integer formatVersion 2), written
//                    FIRST so a reader can validate the container version
//                    before doing anything with the payload
//   package.json     the author's manifest, verbatim
//   <closure>        the entry plus every module it reaches through relative
//                    imports, as plain .cp3 files at their authored paths
//                    relative to the package root (src/main.cp3 stays
//                    src/main.cp3)
//   <native members> the native bindings the module graph resolved (as before)
//   <build.files>    extra files listed in package.json, verbatim
//
// A package build produces source, not binaries: no transpiled C++ and no
// cached binary are shipped, and none are needed - the consumer compiles the
// same `.cp3` units it would compile locally. `build.cacheBinary` is accepted
// but IGNORED for source packages (a warning is printed and the build stays
// green); `build.excludeSource` has no effect for the same reason (the sources
// are the package). The container manifest names the declared `main` exactly
// as authored.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/modules.hpp"

#include "detail/cache/cache.hpp"
#include "detail/cache/cache_binary.hpp"
#include "detail/core/commands.hpp"
#include "detail/pkg/cp3pak.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/native_pack.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/pkg/signatures.hpp"
#include "detail/pkg/source_closure.hpp"
#include "detail/archive/tar.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace detail {

namespace {

std::string genericName(std::string name) {
    for (char& c : name) {
        if (c == '\\') c = '/';
    }
    return name;
}

// Appends `<path>` (relative to the project) from the author's build.files.
bool appendBuildFile(const fs::path& projectRoot, const std::string& listedName,
                     std::vector<TarEntry>& entries, std::string& err) {
    const std::string name = genericName(listedName);
    const fs::path path = projectRoot / fs::path(name);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        err = "build.files lists '" + listedName +
              "', but that file does not exist";
        return false;
    }
    std::string data;
    if (!readTextFile(path, data)) {
        err = "cannot read build.files entry '" + listedName + "'";
        return false;
    }
    entries.push_back(TarEntry{name, std::move(data)});
    return true;
}

bool appendBuildFiles(const JsonValue& manifest, const fs::path& projectRoot,
                      std::vector<TarEntry>& entries, std::string& err) {
    const JsonValue* build = manifest.find("build");
    if (build == nullptr) return true;
    const JsonValue* files = build->arrayField("files");
    if (files == nullptr) return true;
    for (const JsonValue& item : files->array) {
        if (item.type != JsonValue::Type::String) {
            err = "build.files entries must be strings";
            return false;
        }
        if (!appendBuildFile(projectRoot, item.string, entries, err)) {
            return false;
        }
    }
    return true;
}

// Refuses the properties of a package build this build cannot honor.
// Returns kExitSuccess when packing may proceed.
int checkPackManifest(const std::string& entryText, const Program& program) {
    // A package may surface its API through a default export
    // (`export default { ... }`, `export default Counter`) or through named
    // exports (`export class Counter`, `export { Counter }`); either form
    // packs cleanly because the consumer compiles the authored sources.
    if (!hasPackageExportSurface(program)) {
        std::fprintf(stderr,
                     "cp3c: %s: 'type: package' projects need an "
                     "`export default { ... }`, `export class <Name>` or an "
                     "`export { ... }` in the entry file (CLI-README "
                     "§5.2); no .cp3pak was produced\n",
                     entryText.c_str());
        return kExitCompileError;
    }
    return kExitSuccess;
}

// Intermediates mirror the application pipeline: .cp3/build/<module>.{cpp,hpp}
bool writeIntermediates(const PackInput& input, const fs::path& projectRoot,
                        std::string& err) {
    const fs::path moduleBase =
        projectRoot / ".cp3" / "build" / fs::path(*input.moduleName);
    std::error_code ec;
    fs::create_directories(moduleBase.parent_path(), ec);
    if (ec) {
        err = "cannot create " + moduleBase.parent_path().string() + " (" +
              ec.message() + ")";
        return false;
    }
    if (!writeTextFile(moduleBase.string() + ".cpp", input.module->source,
                       nullptr) ||
        !writeTextFile(moduleBase.string() + ".hpp", input.module->header,
                       nullptr)) {
        err = "cannot write build intermediates under " +
              (projectRoot / ".cp3" / "build").string();
        return false;
    }
    return true;
}

// The native binding ids the package build resolved. The module graph is
// the authority (every binding it loaded is embedded in the archive).
std::vector<std::string> nativeIdsForPack(const PackInput& input) {
    std::vector<std::string> ids;
    const auto add = [&ids](const std::vector<NativeBinding>* bindings) {
        if (bindings == nullptr) return;
        for (const NativeBinding& binding : *bindings) {
            if (binding.id.empty()) continue;
            if (std::find(ids.begin(), ids.end(), binding.id) == ids.end()) {
                ids.push_back(binding.id);
            }
        }
    };
    add(input.nativeBindings);
    if (input.graph != nullptr) add(&input.graph->nativeBindings());
    return ids;
}

// Archive members: package.json, the entry's relative-import closure, the
// native binding members, then the author's build.files. (cp3pak.json is
// written first by writeCp3pak.)
bool collectEntries(const PackInput& input, const JsonValue& manifest,
                    const fs::path& projectRoot, std::vector<TarEntry>& entries,
                    const std::vector<std::string>& nativeIds,
                    std::string& err) {
    std::string packageJsonText;
    if (!readTextFile(projectRoot / "package.json", packageJsonText)) {
        err = "cannot read " + (projectRoot / "package.json").string();
        return false;
    }
    entries.push_back(TarEntry{"package.json", std::move(packageJsonText)});
    // The reachable relative-import closure of the entry, at paths relative
    // to the package root. A unit whose file escapes the root is a hard
    // error here, before a single member is consumed downstream: the pak
    // must not be produced at all (the caller turns this into a non-zero
    // exit).
    std::vector<TarEntry> closure;
    if (input.graph == nullptr) {
        err = "internal error: the package build has no module graph";
        return false;
    }
    if (!collectRelativeClosure(*input.graph, projectRoot, closure, err)) {
        return false;
    }
    entries.insert(entries.end(),
                   std::make_move_iterator(closure.begin()),
                   std::make_move_iterator(closure.end()));
    if (!collectNativeBindingMembers(projectRoot, nativeIds, entries, err)) {
        return false;
    }
    return appendBuildFiles(manifest, projectRoot, entries, err);
}

// The package's manifest and the optional `build.cacheBinary` plan. The
// field is accepted but IGNORED for source packages: a warning keeps the
// build green, and the value is still parsed so a malformed one is reported
// - as a warning, never a hard error.
bool readPackManifest(const fs::path& projectRoot, JsonValue& manifest,
                      std::string& err) {
    if (!loadPackageJson(projectRoot, manifest, err)) return false;
    CacheBinaryPlan plan;
    std::string planErr;
    if (!parseCacheBinary(manifest, plan, planErr)) {
        std::fprintf(stderr,
                     "cp3c: warning: build.cacheBinary is ignored for "
                     "source packages (no cached binary is produced or "
                     "needed): %s\n",
                     planErr.c_str());
        return true;
    }
    if (plan.requested) {
        std::fprintf(stderr,
                     "cp3c: warning: build.cacheBinary is ignored for "
                     "source packages (no cached binary is produced or "
                     "needed)\n");
    }
    return true;
}

// Everything the packed archive records about itself.
struct PackOutput {
    std::string name;
    std::string version;
    std::string main;   // the declared `main`, exactly as authored
};

// Writes the .cp3pak: the container manifest plus the archive members.
bool writePackArchive(const fs::path& pakPath, const PackOutput& out,
                      const std::vector<TarEntry>& entries, std::string& err) {
    Cp3pakManifest pak;
    pak.formatVersion = kCp3pakFormatVersion;
    pak.name = out.name;
    pak.version = out.version;
    pak.main = out.main;
    return writeCp3pak(pakPath, pak, entries, err);
}

}  // namespace

int packProject(const PackInput& input) {
    const fs::path projectRoot = findProjectRoot(*input.entryPath);

    JsonValue manifest;
    std::string err;
    if (!readPackManifest(projectRoot, manifest, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    const int policy =
        checkPackManifest(input.entryPath->string(), *input.program);
    if (policy != kExitSuccess) return policy;

    const std::vector<std::string> nativeIds = nativeIdsForPack(input);

    // Resolve the members first: a package that escapes its root produces
    // nothing, not even intermediates or an empty .cp3/out directory entry.
    std::vector<TarEntry> entries;
    if (!collectEntries(input, manifest, projectRoot, entries, nativeIds,
                        err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (!writeIntermediates(input, projectRoot, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    const fs::path outDir = projectRoot / ".cp3" / "out";
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr, "cp3c: cannot create %s (%s)\n",
                     outDir.string().c_str(), ec.message().c_str());
        return kExitCompileError;
    }
    PackOutput out;
    out.name = packageName(manifest, projectRoot);
    out.version = packageVersion(manifest);
    // The container names the declared `main` exactly as the author wrote it;
    // it is never renamed or re-rooted.
    out.main = packageMain(manifest);
    const fs::path pakPath = outDir / (out.name + ".cp3pak");
    if (!writePackArchive(pakPath, out, entries, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    if (input.opt->verbose) {
        std::fprintf(stderr, "cp3c: %zu archive members, formatVersion %d\n",
                     entries.size() + 1, kCp3pakFormatVersion);
    }
    std::fprintf(stderr, "cp3c: packed %s (%s %s)\n", pakPath.string().c_str(),
                 out.name.c_str(), out.version.c_str());
    return kExitSuccess;
}

}  // namespace detail

}  // namespace cp3::cli
