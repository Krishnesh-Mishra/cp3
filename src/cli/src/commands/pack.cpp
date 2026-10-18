// =============================================================================
// src/cli/src/commands/pack.cpp - `cp3c build` for `"type": "package"` projects.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail::packProject.
//
// Output (CLI-README §5, D61): intermediates under .cp3/build/, the final
// artifact .cp3/out/<name>.cp3pak - a gzip-compressed archive carrying
//   cp3pak.json      container manifest (integer formatVersion, §6)
//   package.json     the author's manifest, verbatim
//   index.cp3        signatures/types only, generated from the entry AST
//   pak/<module>.*   the transpiled C++ (cpp + hpp)
//   <build.files>    extra files listed in package.json, verbatim
//
// No C++ toolchain is involved: a package build produces source, not a
// binary (CLI-README §5.3). `excludeSource` is refused - it requires cached
// binaries, which this build does not produce yet (§5.4).
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "detail/cache.hpp"
#include "detail/commands.hpp"
#include "detail/cp3pak.hpp"
#include "detail/json.hpp"
#include "detail/package_json.hpp"
#include "detail/signatures.hpp"
#include "detail/tar.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace detail {

namespace {

bool hasExcludeSource(const JsonValue& manifest) {
    const JsonValue* build = manifest.find("build");
    if (build == nullptr) return false;
    const JsonValue* flag = build->find("excludeSource");
    return flag != nullptr && flag->type == JsonValue::Type::Bool && flag->boolean;
}

bool hasCacheBinaries(const JsonValue& manifest) {
    const JsonValue* build = manifest.find("build");
    if (build == nullptr) return false;
    const JsonValue* list = build->arrayField("cacheBinary");
    return list != nullptr && !list->array.empty();
}

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

// Refuses/annotates the properties of a package build this build cannot honor.
// Returns kExitSuccess when packing may proceed.
int checkPackManifest(const JsonValue& manifest, const std::string& entryText,
                      const Program& program) {
    // CLI-README §5.2: `export default { ... }` is mandatory for packages.
    if (!hasDefaultExport(program)) {
        std::fprintf(stderr,
                     "cp3c: %s: 'type: package' projects need an "
                     "`export default { ... }` in the entry file (CLI-README "
                     "§5.2); no .cp3pak was produced\n",
                     entryText.c_str());
        return kExitCompileError;
    }
    // CLI-README §5.4: excludeSource requires cached binaries to replace the
    // dropped source; cached-binary packing is not implemented in this build.
    if (hasExcludeSource(manifest)) {
        std::fprintf(stderr,
                     "cp3c: build.excludeSource needs cacheBinary binaries to "
                     "keep the package installable (CLI-README §5.4); "
                     "cacheBinary production is not implemented in this "
                     "build\n");
        return notImplemented("pack (excludeSource)");
    }
    if (hasCacheBinaries(manifest)) {
        std::fprintf(stderr,
                     "cp3c: warning: build.cacheBinary is set, but cached "
                     "binaries are not produced in this build; the package "
                     "ships source only\n");
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

// Archive members, in the order CLI-README §5.3 lists them.
bool collectEntries(const PackInput& input, const JsonValue& manifest,
                    const fs::path& projectRoot, std::vector<TarEntry>& entries,
                    std::string& err) {
    std::string packageJsonText;
    if (!readTextFile(projectRoot / "package.json", packageJsonText)) {
        err = "cannot read " + (projectRoot / "package.json").string();
        return false;
    }
    entries.push_back(TarEntry{"package.json", std::move(packageJsonText)});
    entries.push_back(
        TarEntry{"index.cp3", stripToSignatures(*input.source, *input.program)});
    entries.push_back(
        TarEntry{"pak/" + *input.moduleName + ".cpp", input.module->source});
    entries.push_back(
        TarEntry{"pak/" + *input.moduleName + ".hpp", input.module->header});
    return appendBuildFiles(manifest, projectRoot, entries, err);
}

}  // namespace

int packProject(const PackInput& input) {
    const fs::path projectRoot = findProjectRoot(*input.entryPath);

    JsonValue manifest;
    std::string err;
    if (!loadPackageJson(projectRoot, manifest, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    const std::string name = packageName(manifest, projectRoot);
    const std::string version = packageVersion(manifest);
    const int policy =
        checkPackManifest(manifest, input.entryPath->string(), *input.program);
    if (policy != kExitSuccess) return policy;
    if (!writeIntermediates(input, projectRoot, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    std::vector<TarEntry> entries;
    if (!collectEntries(input, manifest, projectRoot, entries, err)) {
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
    const fs::path pakPath = outDir / (name + ".cp3pak");
    Cp3pakManifest pak;
    pak.formatVersion = kCp3pakFormatVersion;
    pak.name = name;
    pak.version = version;
    pak.main = packageMain(manifest);
    if (!writeCp3pak(pakPath, pak, entries, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    if (input.opt->verbose) {
        std::fprintf(stderr, "cp3c: %zu archive members, formatVersion %d\n",
                     entries.size() + 1, kCp3pakFormatVersion);
    }
    std::fprintf(stderr, "cp3c: packed %s (%s %s)\n", pakPath.string().c_str(),
                 name.c_str(), version.c_str());
    return kExitSuccess;
}

}  // namespace detail

}  // namespace cp3::cli
