// =============================================================================
// src/cli/src/commands/build.cpp - the incremental compile/link/execute driver.
//
// OWNERSHIP: cli module (src/cli/**). Implements
// cp3::cli::detail::buildWithToolchain, used by `cp3c build` and `cp3c run`.
//
// Behaviour (DECISIONS D12 + D61 + docs/cli-tooling/01-compilation-pipeline.md):
//   * .cp3/build/ holds intermediates only (generated C++/hpp, objects,
//     manifest); the linked executable is written to .cp3/out/<entry-stem>;
//   * --release wipes the build directory and ignores the manifest;
//   * otherwise each translation unit (the generated module plus every runtime
//     source) is recompiled only when its content hash or profile changed;
//   * the link step reruns when anything was compiled or the executable is
//     missing; otherwise the cached executable is reused.
//
// DECISIONS D68: std modules with a vendored native backend (third_party/**)
// add include roots, compile flags, runtime TUs and link libraries, selected by
// the program's `std/...` imports (applyNativePackages below).
// =============================================================================
#include <cstdio>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/options.hpp"

#include "detail/cache.hpp"
#include "detail/commands.hpp"
#include "detail/native_packages.hpp"
#include "detail/toolchain.hpp"
#include "detail/util.hpp"

namespace cp3::cli::detail {

namespace {

// Flags the native backends selected for one program contribute (D68).
struct NativeBuildFlags {
    std::vector<std::string> compile;
    std::vector<std::string> link;
};

// Mutable state threaded through one build's translation units.
struct BuildState {
    const CliOptions& opt;
    const Toolchain& tc;
    const BuildLayout& layout;
    const std::string& profile;
    const NativeBuildFlags& nativeFlags;
    bool force;
    std::vector<CacheEntry>& manifest;
    std::vector<fs::path>& objects;
    bool anyCompiled = false;
};

CacheEntry* findEntry(std::vector<CacheEntry>& manifest, const std::string& obj) {
    for (CacheEntry& e : manifest) {
        if (e.obj == obj) return &e;
    }
    return nullptr;
}

// Regenerates one object file when needed. `sourceKey` is what the object
// derives from (the .cp3 path for generated code, the .cpp for the runtime).
bool compileTU(BuildState& st, const std::string& sourceKey, const fs::path& cpp,
               const fs::path& obj) {
    std::string content;
    if (!readTextFile(cpp, content)) {
        std::fprintf(stderr, "cp3c: cannot read '%s'\n", cpp.string().c_str());
        return false;
    }
    std::error_code ec;
    const std::string hash = std::to_string(fnv1a64(content));
    CacheEntry* existing = findEntry(st.manifest, obj.string());
    const bool upToDate = !st.force && existing != nullptr &&
                          existing->hash == hash &&
                          existing->profile == st.profile && fs::exists(obj, ec);
    if (upToDate) {
        existing->source = sourceKey;
        st.objects.push_back(obj);
        return true;
    }

    const CompileStep step{st.layout.includeDirs, cpp, obj, st.nativeFlags.compile};
    const std::vector<std::string> args =
        compileArgs(st.tc, st.opt.compile.profile, step);
    if (st.opt.verbose) {
        std::fprintf(stderr, "cp3c: %s\n", describeCommand(args).c_str());
    }
    const int rc = runProcess(args);
    if (rc != 0) {
        std::fprintf(stderr, "cp3c: C++ compilation failed (exit %d)\n", rc);
        return false;
    }
    st.anyCompiled = true;

    if (existing == nullptr) {
        st.manifest.push_back(
            CacheEntry{st.profile, sourceKey, hash, cpp.string(), obj.string()});
    } else {
        existing->profile = st.profile;
        existing->source = sourceKey;
        existing->hash = hash;
        existing->cpp = cpp.string();
    }
    st.objects.push_back(obj);
    return true;
}

// Compiles the generated module plus every runtime TU and persists the
// manifest for the next (incremental) run.
bool compileProgram(BuildState& st, const fs::path& cppPath, const fs::path& objPath,
                    const std::string& entryKey) {
    if (!compileTU(st, entryKey, cppPath, objPath)) return false;
    for (const fs::path& rt : st.layout.runtimeSources) {
        // D72: parent-qualify the object name - the std families bring TUs that
        // share a stem with the core ones (src/std/crypto/runtime.cpp and
        // src/runtime/src/core/runtime.cpp both stem to "runtime").
        const fs::path rtObj =
            st.layout.buildDir /
            ("_cp3_runtime_" + rt.parent_path().filename().string() + "_" +
             rt.stem().string() + ".o");
        if (!compileTU(st, rt.string(), rt, rtObj)) return false;
    }
    saveManifest(st.layout.manifestPath, st.manifest);
    return true;
}

// Compiles the extra generated TUs of the build (spec 13: the worker modules
// the entry imports). Each is written beside the entry's generated files; their
// headers are what the entry's source includes. Runs before compileProgram so
// the worker objects are part of the same manifest save. Returns false after
// reporting a failed compile.
bool compileExtras(BuildState& st, const BuildRequest& request) {
    if (request.extraModules == nullptr) return true;
    for (const BuildRequest::ExtraModule& extra : *request.extraModules) {
        if (extra.module == nullptr) continue;
        const fs::path cpp = st.layout.buildDir / (extra.moduleName + ".cpp");
        const fs::path hpp = st.layout.buildDir / (extra.moduleName + ".hpp");
        const fs::path obj = st.layout.buildDir / (extra.moduleName + ".o");
        writeTextFile(cpp, extra.module->source, nullptr);
        writeTextFile(hpp, extra.module->header, nullptr);
        if (!compileTU(st, extra.sourceKey, cpp, obj)) return false;
    }
    return true;
}

// Links when anything was (re)compiled or the executable is missing; otherwise
// the cached executable is reused as-is.
bool linkProgram(BuildState& st, const fs::path& exePath) {
    std::error_code ec;
    const bool needLink = st.anyCompiled || st.force || !fs::exists(exePath, ec);
    if (!needLink) {
        if (st.opt.verbose) {
            std::fprintf(stderr, "cp3c: up to date, reusing %s\n",
                         exePath.string().c_str());
        }
        return true;
    }
    const LinkStep step{st.objects, exePath, st.nativeFlags.link};
    const std::vector<std::string> args =
        linkArgs(st.tc, st.opt.compile.profile, step);
    if (st.opt.verbose) {
        std::fprintf(stderr, "cp3c: %s\n", describeCommand(args).c_str());
    }
    const int rc = runProcess(args);
    if (rc != 0) {
        std::fprintf(stderr, "cp3c: linking failed (exit %d)\n", rc);
        return false;
    }
    return true;
}

// D68: every std module the program imports that has a vendored native backend
// (third_party/**, D70) contributes include roots and runtime TUs to `layout`
// plus compile/link flags, so a program that never imports std/network pays
// nothing for uWebSockets. Returns nullopt after reporting on a missing packed
// path, or on an MSVC toolchain (the vendored archives are GCC/Clang builds).
std::optional<NativeBuildFlags> applyNativePackages(
    const CliOptions& opt, const Toolchain& tc,
    const std::vector<std::string>& importedStdModules, BuildLayout& layout,
    bool usesRegex) {
    NativeBuildFlags flags;
    std::vector<const NativePackage*> natives =
        selectNativePackages(importedStdModules);
    // Core-language backends (D48) join the same list; selected from the
    // generated module's content, not the import graph.
    const std::vector<const NativePackage*> core =
        selectCoreNativePackages(usesRegex);
    natives.insert(natives.end(), core.begin(), core.end());
    if (natives.empty()) return flags;

    if (tc.msvc) {
        std::fprintf(stderr,
                     "cp3c: '%s' needs its native backend, which is built for "
                     "GCC/Clang (MinGW-w64); MSVC cannot link it in beta one\n",
                     natives.front()->specifier);
        std::fprintf(stderr, "      install g++ or clang++ and re-run.\n");
        return std::nullopt;
    }
    auto reportBroken = [](const char* spec, const char* what,
                           const fs::path& path) {
        std::fprintf(stderr,
                     "cp3c: native backend for '%s' is incomplete: missing "
                     "%s '%s'\n",
                     spec, what, path.string().c_str());
    };
    std::error_code ec;
    const fs::path repo = repositoryRoot();
    for (const NativePackage* pkg : natives) {
        auto resolve = [&](const std::string& rel) {
            return repo.empty() ? fs::path(rel) : repo / rel;
        };
        for (const std::string& rel : pkg->includeDirs) {
            const fs::path dir = resolveIncludeRoot(repo, rel);
            if (!fs::is_directory(dir, ec)) {
                reportBroken(pkg->specifier, "include directory", dir);
                return std::nullopt;
            }
            layout.includeDirs.push_back(dir);
        }
        for (const std::string& rel : pkg->runtimeSources) {
            const fs::path src = resolve(rel);
            if (!fs::is_regular_file(src, ec)) {
                reportBroken(pkg->specifier, "runtime source", src);
                return std::nullopt;
            }
            layout.runtimeSources.push_back(src);
        }
        for (const std::string& flag : pkg->compileFlags) {
            flags.compile.push_back(flag);
        }
        for (const std::string& rel : pkg->libDirs) {
            const fs::path dir = resolve(rel);
            if (!fs::is_directory(dir, ec)) {
                reportBroken(pkg->specifier, "library directory", dir);
                return std::nullopt;
            }
            flags.link.push_back("-L" + dir.string());
        }
        for (const std::string& flag : pkg->linkFlags) {
            flags.link.push_back(flag);
        }
        if (opt.verbose) {
            std::fprintf(stderr, "cp3c: native backend: %s\n", pkg->specifier);
        }
    }
    return flags;
}

}  // namespace

int buildWithToolchain(const BuildRequest& request) {
    const CliOptions& opt = *request.opt;
    const fs::path& entry = *request.entry;
    const std::string& moduleName = *request.moduleName;
    const GeneratedModule& mod = *request.module;

    BuildLayout layout = makeBuildLayout(entry);
    std::error_code ec;

    if (opt.compile.profile == BuildProfile::Release) {
        fs::remove_all(layout.buildDir, ec);  // --release: clean whole-program
    }
    fs::create_directories(layout.buildDir, ec);
    fs::create_directories(layout.outDir, ec);  // D61: final artifact lives here

    const fs::path cppPath = layout.buildDir / fs::path(moduleName + ".cpp");
    const fs::path hppPath = layout.buildDir / fs::path(moduleName + ".hpp");
    const fs::path objPath = layout.buildDir / fs::path(moduleName + ".o");
    writeTextFile(cppPath, mod.source, nullptr);
    writeTextFile(hppPath, mod.header, nullptr);

    const Toolchain& tc = detectToolchain();
    if (!tc.found()) {
        std::fprintf(stderr,
                     "cp3c: no C++ toolchain found on this system "
                     "(looked for cl, clang++, g++ on PATH)\n");
        std::fprintf(stderr,
                     "      install MSVC, Clang, or GCC/g++ and re-run.\n");
        return kExitCompileError;
    }
    if (opt.verbose) {
        std::fprintf(stderr, "cp3c: toolchain: %s\n",
                     describeToolchain(tc).c_str());
    }

    // D48: the core regex backend is needed exactly when the generated C++
    // names it (the literal lowering and the usage-based include scan in
    // codegen both leave the include in mod.source).
    const bool usesRegex =
        mod.source.find("cp3/core/regex.hpp") != std::string::npos;
    const std::optional<NativeBuildFlags> nativeFlags =
        applyNativePackages(opt, tc, *request.importedStdModules, layout,
                            usesRegex);
    if (!nativeFlags.has_value()) return kExitCompileError;

    std::vector<CacheEntry> manifest = loadManifest(layout.manifestPath);
    const std::string profile = profileNameOf(opt.compile.profile);
    const bool force = (opt.compile.profile == BuildProfile::Release);

    std::vector<fs::path> objects;
    BuildState state{opt, tc,        layout,   profile,
                     *nativeFlags, force, manifest, objects};

    if (!compileExtras(state, request)) {
        return kExitCompileError;
    }
    if (!compileProgram(state, cppPath, objPath, entry.string())) {
        return kExitCompileError;
    }
    if (!linkProgram(state, layout.exePath)) {
        return kExitCompileError;
    }

    if (!request.execute) {
        if (opt.verbose) {
            std::fprintf(stderr, "cp3c: built %s\n",
                         layout.exePath.string().c_str());
        }
        return kExitSuccess;
    }

    std::vector<std::string> runArgs;
    runArgs.push_back(layout.exePath.string());
    for (const std::string& a : opt.programArgs) runArgs.push_back(a);
    const int rc = runProcess(runArgs);
    if (rc < 0) {
        std::fprintf(stderr, "cp3c: could not execute %s\n",
                     layout.exePath.string().c_str());
        return kExitCompileError;
    }
    return rc;  // the program's own exit code is propagated verbatim.
}

}  // namespace cp3::cli::detail
