// =============================================================================
// cli/commands_build.cpp — the incremental compile/link/execute driver.
//
// OWNERSHIP: cli module (src/cli/**). Implements
// cp3::cli::detail::buildWithToolchain, used by `cp3c build` and `cp3c run`.
//
// Behaviour (DECISIONS D12 + docs/cli-tooling/01-compilation-pipeline.md):
//   * --release wipes the build directory and ignores the manifest;
//   * otherwise each translation unit (the generated module plus every runtime
//     source) is recompiled only when its content hash or profile changed;
//   * the link step reruns when anything was compiled or the executable is
//     missing; otherwise the cached executable is reused.
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/options.hpp"

#include "private/cache.hpp"
#include "private/commands.hpp"
#include "private/toolchain.hpp"
#include "private/util.hpp"

namespace cp3::cli::detail {

namespace {

// Mutable state threaded through one build's translation units.
struct BuildState {
    const CliOptions& opt;
    const Toolchain& tc;
    const BuildLayout& layout;
    const std::string& profile;
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

    const std::vector<std::string> args = compileArgs(
        st.tc, st.opt.compile.profile, st.layout.includeDirs, cpp, obj);
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

}  // namespace

int buildWithToolchain(const CliOptions& opt, const fs::path& entry,
                       const std::string& moduleName, const GeneratedModule& mod,
                       bool execute) {
    BuildLayout layout = makeBuildLayout(entry);
    std::error_code ec;

    if (opt.compile.profile == BuildProfile::Release) {
        fs::remove_all(layout.buildDir, ec);  // --release: clean whole-program
    }
    fs::create_directories(layout.buildDir, ec);

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

    std::vector<CacheEntry> manifest = loadManifest(layout.manifestPath);
    const std::string profile = profileNameOf(opt.compile.profile);
    const bool force = (opt.compile.profile == BuildProfile::Release);

    std::vector<fs::path> objects;
    BuildState state{opt, tc, layout, profile, force, manifest, objects};

    if (!compileTU(state, entry.string(), cppPath, objPath)) {
        return kExitCompileError;
    }
    for (const fs::path& rt : layout.runtimeSources) {
        const fs::path rtObj =
            layout.buildDir / ("_cp3_runtime_" + rt.stem().string() + ".o");
        if (!compileTU(state, rt.string(), rt, rtObj)) return kExitCompileError;
    }
    saveManifest(layout.manifestPath, manifest);

    const bool needLink =
        state.anyCompiled || force || !fs::exists(layout.exePath, ec);
    if (needLink) {
        const std::vector<std::string> args =
            linkArgs(tc, opt.compile.profile, objects, layout.exePath);
        if (opt.verbose) {
            std::fprintf(stderr, "cp3c: %s\n", describeCommand(args).c_str());
        }
        const int rc = runProcess(args);
        if (rc != 0) {
            std::fprintf(stderr, "cp3c: linking failed (exit %d)\n", rc);
            return kExitCompileError;
        }
    } else if (opt.verbose) {
        std::fprintf(stderr, "cp3c: up to date, reusing %s\n",
                     layout.exePath.string().c_str());
    }

    if (!execute) {
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
