// =============================================================================
// src/cli/src/pkg/install.cpp - local package install DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/install.hpp.
//
// A `.cp3pak` is decompressed once into .cp3/packages/<name>/ as plain files
// (CLI-README §4). A directory install compiles its TypeScript-ish source
// into the same shape a `.cp3pak` carries (package.json + generated
// index.cp3 + pak/<module>.{cpp,hpp} + build.files) so consumers build
// against the generated surface and the transpiled C++; when the directory
// has no compilable entry (or compilation fails) install rejects it with a
// clear "use a .cp3pak" error instead of copying uncompilable source. The
// lock records version + source + a SHA-256 (artifact) or directory digest.
// =============================================================================
#include "detail/pkg/install.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/codegen_program.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/version.hpp"
#include "cp3/core/source.hpp"

#include "detail/cache/checksum.hpp"
#include "detail/cache/cache.hpp"
#include "detail/core/commands.hpp"
#include "detail/core/diagnostics.hpp"
#include "detail/pkg/cp3pak.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/native_pack.hpp"
#include "detail/pkg/signatures.hpp"
#include "detail/cache/package_cache.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/toolchain/pipeline.hpp"
#include "detail/core/semver.hpp"
#include "detail/archive/tar.hpp"
#include "detail/core/worker_module.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string compilerVersion() {
    std::string version = kVersion;
    const std::string stage = kVersionStage;
    if (!stage.empty() && version.find('-') == std::string::npos) {
        version += "-" + stage;
    }
    return version;
}

void warnUnsatisfiedEngines(const fs::path& packageDir, const std::string& name) {
    JsonValue manifest;
    std::string err;
    if (!loadPackageJson(packageDir, manifest, err)) return;
    const JsonValue* engines = manifest.find("engines");
    if (engines == nullptr) return;
    const std::string* range = engines->stringField("cp3");
    if (range == nullptr || range->empty()) return;
    const SemVer compiler = parseSemVer(compilerVersion());
    if (semverSatisfies(compiler, *range)) return;
    std::fprintf(stderr,
                 "cp3c: warning: '%s' declares engines.cp3 \"%s\", but this is "
                 "cp3c %s\n",
                 name.c_str(), range->c_str(), compilerVersion().c_str());
}

bool packageDirFor(const fs::path& projectRoot, const std::string& name,
                   fs::path& out, std::string& err) {
    if (name.empty() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos || name == "." || name == "..") {
        err = "package name '" + name + "' is not usable as a directory name";
        return false;
    }
    out = projectRoot / ".cp3" / "packages" / name;
    return true;
}

std::string relativeName(const fs::path& base, const fs::path& path) {
    std::error_code ec;
    fs::path rel = fs::relative(path, base, ec);
    if (ec || rel.empty()) rel = path;
    return rel.generic_string();
}

bool skipCopyEntry(const fs::path& relative) {
    if (relative.empty()) return true;
    const std::string first = relative.begin()->string();
    return first == ".cp3" || first == ".git" || first == ".bin";
}

bool copyTree(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) {
        err = "cannot create " + to.string() + " (" + ec.message() + ")";
        return false;
    }
    for (fs::recursive_directory_iterator it(from, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            err = "cannot scan " + from.string() + " (" + ec.message() + ")";
            return false;
        }
        const fs::path rel = fs::relative(it->path(), from, ec);
        if (ec || skipCopyEntry(rel)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        const fs::path dest = to / rel;
        ec.clear();
        if (it->is_directory(ec)) {
            fs::create_directories(dest, ec);
        } else if (it->is_regular_file(ec)) {
            fs::create_directories(dest.parent_path(), ec);
            if (!ec) {
                fs::copy_file(it->path(), dest,
                              fs::copy_options::overwrite_existing, ec);
            }
        }
        if (ec) {
            err = "cannot copy " + it->path().string() + " (" + ec.message() + ")";
            return false;
        }
    }
    return true;
}

// Deterministic digest of a package directory: SHA-256 over the sorted
// "relative-path content-hash" lines. Distinguishes a directory install from a
// single-artifact one in the lock ("dir-sha256:" vs "sha256:").
std::string treeDigest(const fs::path& dir) {
    std::vector<std::string> lines;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(it->path(), dir, ec);
        if (ec || skipCopyEntry(rel)) continue;
        std::string data;
        if (!readTextFile(it->path(), data)) continue;
        lines.push_back(rel.generic_string() + " " + sha256Hex(data));
    }
    std::sort(lines.begin(), lines.end());
    std::string joined;
    for (const std::string& line : lines) {
        joined += line;
        joined += "\n";
    }
    return sha256Hex(joined);
}

bool parseArchivePackageJson(const std::vector<TarEntry>& members,
                             JsonValue& manifest, std::string& err) {
    for (const TarEntry& member : members) {
        if (member.name != "package.json") continue;
        if (!JsonValue::parse(member.data, manifest, err)) {
            err = "archive's package.json is invalid: " + err;
            return false;
        }
        return true;
    }
    err = "archive has no package.json";
    return false;
}

// Assembles the whole-program unit list for the codegen path (mirrors
// run.cpp): every loaded unit with a parsed AST, the entry carrying its own
// SemaResult and every other unit its per-unit table.
std::vector<CheckedModuleUnit> installCheckedUnits(
    const ModuleGraph& graph, const SemaResult& entrySema,
    const std::unordered_map<std::uint32_t, SemaResult>& unitSemas) {
    std::vector<CheckedModuleUnit> units;
    for (CompilationUnit* unit : graph.compilationOrder()) {
        if (unit == nullptr || !unit->ast || unit->isPrelude) continue;
        const SemaResult* sema = nullptr;
        if (unit->isEntry) {
            sema = &entrySema;
        } else {
            const auto it = unitSemas.find(unit->id);
            if (it == unitSemas.end()) continue;
            sema = &it->second;
        }
        units.push_back(CheckedModuleUnit{unit, sema});
    }
    return units;
}

std::string installUsePakMessage(const fs::path& source) {
    return "cannot install '" + source.string() +
           "': it has no compilable package entry (a directory install "
           "compiles the TypeScript-ish source with its default or named "
           "exports into a .cp3pak-shaped layout); use a .cp3pak instead "
           "(build it with 'cp3c build' in the package directory)";
}

// Compiles a package directory's entry into the generated C++ for the
// .cp3pak-shaped install layout. Returns false with a "use a .cp3pak"
// message when the directory has no compilable entry; callers keep the
// legacy copy path only for directories with no entry file at all.
bool compilePackageEntry(const fs::path& source, const JsonValue& manifest,
                         GeneratedModule& module, std::string& moduleName,
                         const Program*& programOut, SourceManager& sm,
                         SourceFile*& entryFile, CompileOptions& compile,
                         std::string& err) {
    const std::string main = packageMain(manifest);
    const fs::path entryPath = source / fs::path(main);
    std::error_code ec;
    if (!fs::is_regular_file(entryPath, ec)) {
        err = installUsePakMessage(source);
        return false;
    }
    applyProjectTypeSettings(source, compile);
    const fs::path projectRoot = source;
    moduleName = moduleNameFor(entryPath, projectRoot);
    std::string loadErr;
    if (!loadEntrySource(entryPath.string(), sm, entryFile, loadErr)) {
        err = installUsePakMessage(source) + " (" + loadErr + ")";
        return false;
    }
    CollectingDiagnosticSink feSink;
    std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sm, *entryFile, feSink);
    compile.nativeBindings = graph->nativeBindings();
    CompilationUnit* entry = graph->entry();
    SemaResult sema;
    std::unordered_map<std::uint32_t, SemaResult> unitSemas;
    if (entry != nullptr && entry->ast) {
        analyzeProgramGraph(*graph, sema, unitSemas, feSink, compile);
    }
    RenderOptions ro;
    ro.fallbackPath = entryPath.string();
    renderDiagnostics(feSink.diagnostics(), ro, stderr);
    if (feSink.hasErrors() || entry == nullptr || !entry->ast) {
        err = installUsePakMessage(source) +
              " (the package source did not pass the compiler frontend; fix "
              "it and 'cp3c build' the package, or install a .cp3pak)";
        return false;
    }
    if (!hasPackageExportSurface(*entry->ast)) {
        err = installUsePakMessage(source) +
              " (the package entry needs `export default { ... }`, "
              "`export class <Name>`, or `export { ... }`)";
        return false;
    }
    CodegenOptions options;
    options.importedEnums = importedEnumsFor(*graph, *entry);
    options.typeSystem = compile.typeSystem;
    options.nativeBindings = compile.nativeBindings;
    CollectingDiagnosticSink cgSink;
    if (hasProgramModules(*graph)) {
        const std::vector<CheckedModuleUnit> units =
            installCheckedUnits(*graph, sema, unitSemas);
        module = generateProgramModule(
            {*graph, units, moduleName, cgSink, false, options});
    } else {
        module = generateModule({*entry->ast, sema, moduleName, cgSink, false, options});
    }
    renderDiagnostics(cgSink.diagnostics(), ro, stderr);
    if (cgSink.hasErrors() || (module.source.empty() && module.header.empty())) {
        err = installUsePakMessage(source) +
              " (code generation failed; fix the package source, or install "
              "a .cp3pak built with 'cp3c build')";
        return false;
    }
    programOut = entry->ast.get();
    return true;
}

bool installFromDirectory(const fs::path& source, const fs::path& projectRoot,
                          LockEntry& entry, std::string& err) {
    JsonValue manifest;
    if (!loadPackageJson(source, manifest, err)) return false;
    const std::string name = packageName(manifest, source);
    const std::string version = packageVersion(manifest);
    // A package that excluded its C++ source is only usable on a platform it
    // ships a cached binary for (CLI-README §5.4).
    PlatformCacheManifest cache;
    const bool hasCache = installedPlatformCache(source, cache);
    if (!checkPackageUsable(name, version, manifest, hasCache, err)) {
        return false;
    }
    fs::path packageDir;
    if (!packageDirFor(projectRoot, name, packageDir, err)) return false;

    removeTree(packageDir);
    std::error_code ec;
    const fs::path entryProbe = source / fs::path(packageMain(manifest));
    if (!fs::is_regular_file(entryProbe, ec)) {
        // Legacy path: a directory with no entry file at all (metadata-only
        // fixtures) installs as a plain copy, as before.
        if (!copyTree(source, packageDir, err)) return false;
    } else {
        // Compiled path: `cp3 install <dir>` transpiles the package entry
        // into the same members a `.cp3pak` carries, so downstream builds
        // link the generated C++ instead of the raw source.
        SourceManager sm;
        SourceFile* entryFile = nullptr;
        CompileOptions compile;
        GeneratedModule module;
        std::string moduleName;
        const Program* program = nullptr;
        if (!compilePackageEntry(source, manifest, module, moduleName, program,
                                 sm, entryFile, compile, err)) {
            return false;
        }
        // Materialize the .cp3pak-shaped layout: package.json verbatim,
        // index.cp3 from signatures + native imports, pak/<module>.{cpp,hpp}.
        std::string packageJsonText;
        if (!readTextFile(source / "package.json", packageJsonText)) {
            err = "cannot read " + (source / "package.json").string();
            return false;
        }
        fs::create_directories(packageDir, ec);
        if (ec) {
            err = "cannot create " + packageDir.string() + " (" + ec.message() + ")";
            return false;
        }
        if (!writeTextFile(packageDir / "package.json", packageJsonText, nullptr)) {
            err = "cannot write " + (packageDir / "package.json").string();
            return false;
        }
        std::vector<TarEntry> members;
        members.push_back(TarEntry{"package.json", std::move(packageJsonText)});
        std::string surface = stripToSignatures(*entryFile, *program, compile.typeSystem);
        std::vector<std::string> nativeIds;
        for (const NativeBinding& binding : compile.nativeBindings) {
            if (binding.id.empty()) continue;
            if (std::find(nativeIds.begin(), nativeIds.end(), binding.id) ==
                nativeIds.end()) {
                nativeIds.push_back(binding.id);
            }
        }
        for (const std::string& id : nativeBindingIdsInText(surface)) {
            if (std::find(nativeIds.begin(), nativeIds.end(), id) == nativeIds.end()) {
                nativeIds.push_back(id);
            }
        }
        surface = ensureNativeImports(std::move(surface), nativeIds);
        members.push_back(TarEntry{"index.cp3", std::move(surface)});
        members.push_back(
            TarEntry{"pak/" + moduleName + ".cpp", module.source});
        members.push_back(
            TarEntry{"pak/" + moduleName + ".hpp", module.header});
        // Extra shipped files listed in build.files (mirrors pack.cpp).
        const JsonValue* build = manifest.find("build");
        const JsonValue* files = build ? build->arrayField("files") : nullptr;
        if (files != nullptr) {
            for (const JsonValue& item : files->array) {
                if (item.type != JsonValue::Type::String) {
                    err = "build.files entries must be strings";
                    return false;
                }
                const fs::path filePath = source / fs::path(item.string);
                std::string data;
                if (!readTextFile(filePath, data)) {
                    err = "build.files lists '" + item.string +
                          "', but that file does not exist";
                    return false;
                }
                members.push_back(TarEntry{item.string, std::move(data)});
            }
        }
        for (const TarEntry& member : members) {
            if (member.name == "package.json") continue;
            const fs::path dest = packageDir / fs::path(member.name);
            fs::create_directories(dest.parent_path(), ec);
            if (ec || !writeTextFile(dest, member.data, nullptr)) {
                err = "cannot write " + dest.string();
                return false;
            }
        }
    }
    // The package's `.cp3/native` tree is not part of the installed package
    // copy (`copyTree` skips `.cp3`); it belongs to the consuming project so
    // `import native` resolves it. Relocate the assets under the binding's
    // self-contained `.cp3/native/<id>/` layout.
    if (!installNativeBindingsFromDirectory(source, projectRoot, err)) {
        return false;
    }
    entry.name = name;
    entry.version = packageVersion(manifest);
    entry.resolved = "file:" + relativeName(projectRoot, source);
    entry.integrity = "dir-sha256:" + treeDigest(packageDir);
    warnUnsatisfiedEngines(packageDir, name);
    reportPackageArtifactMode(name, packageDir);
    return true;
}

bool writePackageMember(const fs::path& packageDir, const TarEntry& member,
                        std::string& err) {
    std::string data = member.data;
    // Beta-one archives generated the export surface with a doubled keyword
    // (`export export function ...`) because the AST span already carried the
    // modifier. The generator is fixed, but an already-published archive must
    // keep installing; repair the generated surface on the way in.
    if (member.name == "index.cp3") {
        const std::string doubled = "export export ";
        std::size_t at = 0;
        while ((at = data.find(doubled, at)) != std::string::npos) {
            const bool lineStart =
                at == 0 || data[at - 1] == '\n' || data[at - 1] == '\r';
            if (!lineStart) {
                at += doubled.size();
                continue;
            }
            data.erase(at, std::string("export ").size());
        }
    }
    const fs::path dest = packageDir / fs::path(member.name);
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (!writeTextFile(dest, data, nullptr)) {
        err = "cannot write " + dest.string();
        return false;
    }
    return true;
}

}  // namespace

bool packageSourceExists(const fs::path& source) {
    std::error_code ec;
    return fs::is_directory(source, ec) || fs::is_regular_file(source, ec);
}

bool inspectPackageSource(const fs::path& source, std::string& name,
                          std::string& version, std::string& err) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        JsonValue manifest;
        if (!loadPackageJson(source, manifest, err)) return false;
        name = packageName(manifest, source);
        version = packageVersion(manifest);
        return true;
    }
    if (fs::is_regular_file(source, ec)) {
        Cp3pakManifest pak;
        std::vector<TarEntry> members;
        if (!readCp3pak(source, pak, members, err)) return false;
        JsonValue manifest;
        if (!parseArchivePackageJson(members, manifest, err)) return false;
        const std::string* declared = manifest.stringField("name");
        if (declared == nullptr || declared->empty()) {
            err = "archive's package.json has no \"name\"";
            return false;
        }
        name = *declared;
        version = packageVersion(manifest);
        return true;
    }
    err = "'" + source.string() +
          "' does not exist (expected a .cp3pak file or a package directory)";
    return false;
}

bool installArchivePackage(const fs::path& archive, const fs::path& projectRoot,
                           LockEntry& entry, std::string& err) {
    Cp3pakManifest pak;
    std::vector<TarEntry> members;
    if (!readCp3pak(archive, pak, members, err)) return false;

    JsonValue manifest;
    if (!parseArchivePackageJson(members, manifest, err)) return false;
    const std::string* declared = manifest.stringField("name");
    if (declared == nullptr || declared->empty()) {
        err = "archive's package.json has no \"name\"";
        return false;
    }
    const std::string name = *declared;
    const std::string version = packageVersion(manifest);
    // A package that excluded its C++ source is only usable on a platform it
    // ships a cached binary for (CLI-README §5.4).
    const bool hasCache =
        archiveHasPlatformCache(members, currentPlatform());
    if (!checkPackageUsable(name, version, manifest, hasCache, err)) {
        return false;
    }
    fs::path packageDir;
    if (!packageDirFor(projectRoot, name, packageDir, err)) return false;

    removeTree(packageDir);
    // Embedded native assets are installed at the project root, not inside the
    // package directory: the compiler resolves `import native "<id>"` through
    // `<project>/.cp3/native/<id>`.
    if (!installNativeMembers(members, projectRoot, err)) return false;
    for (const TarEntry& member : members) {
        if (isNativeArchiveMember(member.name)) continue;
        if (!writePackageMember(packageDir, member, err)) return false;
    }

    std::string bytes;
    if (!readTextFile(archive, bytes)) {
        err = "cannot re-read " + archive.string() + " for hashing";
        return false;
    }
    entry.name = name;
    entry.version = version;
    entry.resolved = "file:" + relativeName(projectRoot, archive);
    entry.integrity = "sha256:" + sha256Hex(bytes);
    warnUnsatisfiedEngines(packageDir, name);
    reportPackageArtifactMode(name, packageDir);
    return true;
}

bool installLocalPackage(const fs::path& source, const fs::path& projectRoot,
                         LockEntry& entry, std::string& err) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        return installFromDirectory(source, projectRoot, entry, err);
    }
    if (fs::is_regular_file(source, ec)) {
        return installArchivePackage(source, projectRoot, entry, err);
    }
    err = "'" + source.string() +
          "' does not exist (expected a .cp3pak file or a package directory)";
    return false;
}

}  // namespace cp3::cli
