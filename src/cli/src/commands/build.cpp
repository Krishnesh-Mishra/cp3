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
//   * every manifest entry records the include-tree fingerprint it was built
//     under (detail/cache.hpp); a changed fingerprint retires the entry,
//     because TUs are not tracked back to the headers they include;
//   * the link step reruns when anything compiled, the executable is missing,
//     or its link stamp - object-set paths + content hashes, profile, header
//     fingerprint, native link flags - changed, so a runtime TU rebuilt for an
//     earlier program still invalidates every executable that contains it.
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
    const std::string& headerFingerprint;  // of the current include roots
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
    // The entry also carries the include-tree fingerprint it was built under:
    // the manifest does not track which headers a TU includes, so an include
    // change retires every entry on its next build (detail/cache.hpp).
    const bool upToDate = !st.force && existing != nullptr &&
                          existing->hash == hash &&
                          existing->profile == st.profile &&
                          existing->header == st.headerFingerprint &&
                          fs::exists(obj, ec);
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
        st.manifest.push_back(CacheEntry{st.profile, sourceKey, hash,
                                         st.headerFingerprint, cpp.string(),
                                         obj.string()});
    } else {
        existing->profile = st.profile;
        existing->source = sourceKey;
        existing->hash = hash;
        existing->header = st.headerFingerprint;
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

// The link step's input identity: every object's path and content hash plus
// the profile, the header fingerprint, the native link flags, and the exact
// link command line (argv[0], output path and profile/security flags such as
// `-static` included). Hashing the command is what makes a toolchain-flag
// change relink an otherwise up-to-date program.
std::string linkStampOf(const BuildState& st,
                        const std::vector<std::string>& linkCommand) {
    std::string material = st.profile;
    material += '\n';
    material += st.headerFingerprint;
    for (const fs::path& obj : st.objects) {
        material += '\n';
        material += obj.string();
        material += '\t';
        for (const CacheEntry& e : st.manifest) {
            if (e.obj == obj.string()) {
                material += e.hash;
                break;
            }
        }
    }
    for (const std::string& flag : st.nativeFlags.link) {
        material += '\n';
        material += flag;
    }
    for (const std::string& arg : linkCommand) {
        material += '\n';
        material += arg;
    }
    return std::to_string(fnv1a64(material));
}

// Links when anything was (re)compiled, the executable is missing, or its link
// stamp no longer matches the object set being linked. The stamp is what keeps
// an executable from surviving a runtime TU rebuild performed in an earlier
// program's pass: that program's exe is relinked because it compiled the TU,
// but every other program sharing the build directory reuses its cached exe
// unless the stamp says the object set moved.
bool linkProgram(BuildState& st, const fs::path& exePath) {
    std::error_code ec;
    const fs::path stampPath =
        st.layout.buildDir / (exePath.filename().string() + ".link-stamp");
    const LinkStep step{st.objects, exePath, st.nativeFlags.link};
    const std::vector<std::string> args =
        linkArgs(st.tc, st.opt.compile.profile, step);
    const std::string stamp = linkStampOf(st, args);
    std::string previous;
    const bool stampMatches = fs::exists(exePath, ec) &&
                              readTextFile(stampPath, previous) &&
                              previous == stamp;
    if (!st.force && !st.anyCompiled && stampMatches) {
        if (st.opt.verbose) {
            std::fprintf(stderr, "cp3c: up to date, reusing %s\n",
                         exePath.string().c_str());
        }
        return true;
    }
    if (st.opt.verbose) {
        std::fprintf(stderr, "cp3c: %s\n", describeCommand(args).c_str());
    }
    const int rc = runProcess(args);
    if (rc != 0) {
        std::fprintf(stderr, "cp3c: linking failed (exit %d)\n", rc);
        return false;
    }
    writeTextFile(stampPath, stamp, nullptr);
    return true;
}

// D68: every std module the program imports that has a vendored native backend
// (third_party/**, D70) contributes include roots and runtime TUs to `layout`
// plus compile/link flags, so a program that never imports std/network pays
// nothing for uWebSockets. Returns nullopt after reporting on a missing packed
// path, or on an MSVC toolchain (the vendored archives are GCC/Clang builds).
//
// A source whose row gives it a marker (NativePackage::runtimeSourceMarkers) is
// only linked when the generated program text names one of its markers, so a
// program that imports a family for one member does not link the whole family.
std::optional<NativeBuildFlags> applyNativePackages(
    const CliOptions& opt, const Toolchain& tc,
    const std::vector<std::string>& importedStdModules, BuildLayout& layout,
    const std::string& programText) {
    NativeBuildFlags flags;
    std::vector<const NativePackage*> natives =
        selectNativePackages(importedStdModules);
    // Core-language backends (D48) join the same list; selected from the
    // generated module's content, not the import graph.
    const std::vector<const NativePackage*> core = selectCoreNativePackages(
        programText.find("cp3/core/regex.hpp") != std::string::npos);
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
        for (std::size_t i = 0; i < pkg->runtimeSources.size(); ++i) {
            const std::string& rel = pkg->runtimeSources[i];
            // A marker-carrying source is linked only when the generated
            // program actually names the family member it implements.
            if (i < pkg->runtimeSourceMarkers.size() &&
                !markerListMatches(programText, pkg->runtimeSourceMarkers[i])) {
                if (opt.verbose)
                    std::fprintf(stderr, "cp3c: skipping unused %s TU: %s\n",
                                 pkg->specifier, rel.c_str());
                continue;
            }
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

// The text every link-selection decision is made from: the entry's generated
// source and header plus every generated worker module. What a program links is
// decided by what its generated C++ names, never by the entry's path.
std::string generatedProgramText(const BuildRequest& request,
                                 const GeneratedModule& mod) {
    std::string text = mod.source;
    text += '\n';
    text += mod.header;
    if (request.extraModules != nullptr) {
        for (const BuildRequest::ExtraModule& extra : *request.extraModules) {
            if (extra.module == nullptr) continue;
            text += '\n';
            text += extra.module->source;
            text += '\n';
            text += extra.module->header;
        }
    }
    return text;
}

// Fills layout.runtimeSources with the core runtime TUs the program names plus
// the native-backend TUs already selected by applyNativePackages (which leaves
// them in the vector). The native TUs' own text is folded into the marker text:
// a native TU that uses the loop (the console pump, std/network) pulls the loop
// in with it.
void selectRuntimeSources(const CliOptions& opt, BuildLayout& layout,
                          const std::string& programText) {
    std::string markers = programText;
    const std::vector<fs::path> nativeSources = layout.runtimeSources;
    for (const fs::path& src : nativeSources) {
        std::string text;
        if (readTextFile(src, text)) {
            markers += '\n';
            markers += text;
        }
    }
    layout.runtimeSources = runtimeSourcePaths(markers);
    layout.runtimeSources.insert(layout.runtimeSources.end(),
                                 nativeSources.begin(), nativeSources.end());
    if (opt.verbose) {
        for (const fs::path& src : layout.runtimeSources) {
            std::fprintf(stderr, "cp3c: runtime TU: %s\n",
                         src.filename().string().c_str());
        }
    }
}

// The toolchain probe plus the native-package flags the generated module
// needs: the imported std backends (D68) and the core regex backend (D48),
// selected from the module text. Reports and returns nullopt when the
// toolchain is missing or a vendored backend is unusable.
std::optional<NativeBuildFlags> resolveNativeFlags(
    const CliOptions& opt, const BuildRequest& request,
    const GeneratedModule& mod, BuildLayout& layout, const Toolchain*& tcOut) {
    const Toolchain& tc = detectToolchain();
    if (!tc.found()) {
        std::fprintf(stderr,
                     "cp3c: no C++ toolchain found on this system "
                     "(looked for cl, clang++, g++ on PATH)\n");
        std::fprintf(stderr,
                     "      install MSVC, Clang, or GCC/g++ and re-run.\n");
        return std::nullopt;
    }
    tcOut = &tc;
    if (opt.verbose) {
        std::fprintf(stderr, "cp3c: toolchain: %s\n",
                     describeToolchain(tc).c_str());
    }
    return applyNativePackages(opt, tc, *request.importedStdModules, layout,
                               generatedProgramText(request, mod));
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

    const std::string programText = generatedProgramText(request, mod);

    const Toolchain* tcPtr = nullptr;
    const std::optional<NativeBuildFlags> nativeFlags =
        resolveNativeFlags(opt, request, mod, layout, tcPtr);
    if (!nativeFlags.has_value()) return kExitCompileError;
    const Toolchain& tc = *tcPtr;

    selectRuntimeSources(opt, layout, programText);

    const std::string headerFingerprint =
        headerFingerprintOf(layout.includeDirs);
    std::vector<CacheEntry> manifest = loadManifest(layout.manifestPath);
    const std::string profile = profileNameOf(opt.compile.profile);
    const bool force = (opt.compile.profile == BuildProfile::Release);

    std::vector<fs::path> objects;
    BuildState state{opt, tc,        layout,   profile,
                     *nativeFlags, headerFingerprint, force, manifest, objects};

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
