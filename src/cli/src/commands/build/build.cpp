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
// DECISIONS D68: std modules with a vendored native backend (vendor/third_party/**)
// add include roots, compile flags, runtime TUs and link libraries, selected by
// the program's `std/...` imports (applyNativePackages below).
// =============================================================================
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#endif

#include "cp3/host/options.hpp"

#include "detail/cache/cache.hpp"
#include "detail/core/commands.hpp"
#include "detail/native/native_build.hpp"
#include "detail/native/native_packages.hpp"
#include "detail/toolchain/toolchain.hpp"
#include "detail/toolchain/toolchain_select.hpp"
#include "detail/core/runtime_snapshot.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli::detail {

namespace {

// Mutable state threaded through one build's translation units.
struct BuildState {
    const CliOptions& opt;
    const Toolchain& tc;
    const BuildLayout& layout;
    const std::string& profile;
    const BuildOptimizations& optimizations;
    const NativeBuildFlags& nativeFlags;
    const std::string& headerFingerprint;  // of the current include roots
    const std::string& compilerKey;        // family, exact version, driver path
    bool force;
    std::vector<CacheEntry>& manifest;
    std::vector<fs::path>& objects;
    bool anyCompiled = false;
    bool showProgress = false;
    bool compileProgressShown = false;
    // Incremental profile only: the ready-made precompiled header for the
    // shared generated prologue (empty when the PCH is unavailable).
    std::string pchInclude;
};

// One clang++ invocation resolved to a unit the build must produce or reuse.
// The manifest-tied decisions all happen on the caller thread before the pool
// starts, so clang processes fan out without touching shared state; the
// manifest is updated on the caller thread afterwards, in job order.
struct ObjectJob {
    std::string sourceKey;    // manifest source key (the .cp3 path or the real .cpp)
    fs::path cpp;             // .cpp to compile (generated or runtime/extras)
    fs::path obj;             // .o output
    std::vector<std::string> extraFlags;
    // Phase A decides these single-threaded.
    bool needsCompile = false;
    std::string hash;
    bool objReusable = false;
    // Filled by the pool (or -2 when phase A fails before compiling).
    int clangRc = -2;
    // When set, this TU's manifest fingerprint replaces the whole-tree one:
    // the plural (per-module) pipeline keys its unit TUs on the shared
    // header's content hash plus the runtime fingerprint, so an interface
    // edit retires them but a body-only edit does not. Runtime TUs leave it
    // empty and keep the whole-tree fingerprint.
    std::string dependencyHash;
};

// The release profile's default switches. `--no-*` turns one off without
// leaving the release profile; `--opt-level` overrides the -O level.
// The incremental (dev/run) profile is deliberately the compiler's cheapest
// setting: -O0 -g0, no LTO/PGO/native-ISA/unroll - EA semantics unchanged,
// only compile time. The CLI does not expose an optimization knob for dev
// today; a dev that waits is a dev that the user does not use.
BuildOptimizations buildOptimizationsFor(const CliOptions& opt) {
    BuildOptimizations out;
    if (opt.compile.profile == BuildProfile::Debug) return out;
    if (opt.compile.profile == BuildProfile::Incremental) {
        out.optLevel = "0";
        return out;
    }
    out.optLevel = opt.optLevel;
    if (opt.compile.profile == BuildProfile::Release) {
        out.lto = !opt.noLto;
        out.nativeIsa = !opt.noNativeIsa;
        out.unrollLoops = !opt.noUnrollLoops;
        out.noMathErrno = !opt.strictFp;
#if defined(__linux__)
        // ELF-only: Windows/COFF has no PLT. This only removes an indirect
        // jump; it cannot change program results.
        out.noPlt = true;
#endif
    }
    out.pgoProfile = opt.pgoProfile;
    if (opt.pgoGenerate && !opt.pgoGenerateDir.empty()) {
        out.pgoGenerate =
            (fs::path(opt.pgoGenerateDir) / "default_%m_%p.profraw").string();
    }
    return out;
}

// A profile file's identity for the object cache. The path alone is not
// enough: retraining replaces the contents in place.
std::string pgoCacheKey(const std::string& profilePath) {
    if (profilePath.empty()) return {};
    std::error_code ec;
    const auto size = fs::file_size(profilePath, ec);
    const auto stamp = fs::last_write_time(profilePath, ec);
    std::string key = profilePath;
    if (!ec) {
        key += "|" + std::to_string(size) + "|" +
               std::to_string(stamp.time_since_epoch().count());
    }
    return key;
}

#if defined(_WIN32)
// Clang's MinGW target needs compiler-rt's profile runtime to link an
// instrumented binary. Some LLVM installs ship only the MSVC runtime; detect
// that before spending time compiling every TU, and point at the supported
// alternative (an existing profile with --pgo).
bool hasMinGwProfileRuntime(const Toolchain& tc) {
    std::string triple;
    for (const std::string& flag : tc.targetFlags) {
        if (flag.rfind("--target=", 0) == 0) triple = flag.substr(9);
    }
    if (triple.find("windows-gnu") == std::string::npos) return true;
    std::string major = tc.version;
    const std::size_t dot = major.find('.');
    if (dot != std::string::npos) major = major.substr(0, dot);
    const fs::path binDir = fs::path(tc.program).parent_path();
    const fs::path runtime = binDir.parent_path() / "lib" / "clang" / major /
                             "lib" / triple / "libclang_rt.profile.a";
    std::error_code ec;
    return fs::is_regular_file(runtime, ec);
}
#endif

// The cache key for a compilation: profile plus every flag that can change
// the object code. Changing a switch retires the old objects instead of
// silently reusing a binary built under different optimization settings.
std::string buildProfileKey(const CliOptions& opt,
                            const BuildOptimizations& optimizations) {
    std::string key = profileNameOf(opt.compile.profile);
    key += "|O" + (optimizations.optLevel.empty() ? "default"
                                                   : optimizations.optLevel);
    key += optimizations.lto ? "|lto" : "|nolto";
    key += optimizations.nativeIsa ? "|native" : "|nonative";
    key += optimizations.unrollLoops ? "|unroll" : "|nounroll";
    key += optimizations.noMathErrno ? "|no-math-errno" : "|math-errno";
    if (!optimizations.pgoProfile.empty()) {
        key += "|pgo:" + pgoCacheKey(optimizations.pgoProfile);
    }
    if (!optimizations.pgoGenerate.empty()) {
        key += "|pgo-gen:" + optimizations.pgoGenerate;
    }
    return key;
}

// Object files come in the formats our targets produce (COFF, ELF, Mach-O,
// wasm). The incremental cache reuses same-path objects across runs, including
// runs of a *different* toolchain that shares the .cp3 directory - a Linux
// build leaves ELF objects where a Windows build expects COFF, and lld refuses
// to link them ("unknown file type"). Sniff the object before trusting it.
enum class ObjFamily { Unknown, Coff, Elf, MachO, Wasm };

// The target family implied by the toolchain's target flags, or the platform
// default when no target is named (POSIX clang targets the host).
ObjFamily expectedObjFamily(const Toolchain& tc) {
    for (const std::string& flag : tc.targetFlags) {
        std::string target = flag;
        std::transform(target.begin(), target.end(), target.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (target.rfind("--target=", 0) == 0) target = target.substr(9);
        const auto has = [&target](const char* needle) {
            return target.find(needle) != std::string::npos;
        };
        if (has("windows") || has("mingw") || has("msvc") || has("win32")) return ObjFamily::Coff;
        if (has("darwin") || has("macos") || has("apple") || has("ios") ||
            has("tvos") || has("watchos") || has("visionos")) return ObjFamily::MachO;
        if (has("wasm") || has("wasi")) return ObjFamily::Wasm;
        if (has("linux") || has("android") || has("freebsd") || has("openbsd") ||
            has("netbsd") || has("solaris") || has("haiku")) return ObjFamily::Elf;
    }
#if defined(_WIN32)
    return ObjFamily::Coff;
#elif defined(__APPLE__)
    return ObjFamily::MachO;
#else
    return ObjFamily::Elf;
#endif
}

// True when the object's first bytes match the family the toolchain produces.
// An unrecognised or truncated object is not reusable either.
bool objectMatchesFamily(const fs::path& obj, ObjFamily family) {
    if (family == ObjFamily::Unknown) return true;
    std::ifstream in(obj, std::ios::binary);
    unsigned char head[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    if (in.gcount() < static_cast<std::streamsize>(sizeof(head))) return false;
    const bool elf = head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F';
    const bool macho =
        (head[0] == 0xFE && head[1] == 0xED && head[2] == 0xFA) ||  // big-endian 32/64
        (head[0] == 0xCE && head[1] == 0xFA && head[2] == 0xED) ||  // little-endian 32
        (head[0] == 0xCF && head[1] == 0xFA && head[2] == 0xED) ||  // little-endian 64
        (head[0] == 0xCA && head[1] == 0xFE && head[2] == 0xBA) ||  // fat binary
        (head[0] == 0xBE && head[1] == 0xBA && head[2] == 0xFE);
    const bool wasm = head[0] == 0x00 && head[1] == 0x61 && head[2] == 0x73 && head[3] == 0x6D;
    const unsigned machine = static_cast<unsigned>(head[0]) |
                             (static_cast<unsigned>(head[1]) << 8);
    const bool coff = machine == 0x014C ||  // i386
                      machine == 0x8664 ||  // AMD64
                      machine == 0xAA64 ||  // ARM64
                      machine == 0x01C2 ||  // ARM Thumb-2
                      machine == 0x01C4 ||  // ARMNT
                      machine == 0x0200;    // IA64
    switch (family) {
        case ObjFamily::Coff: return coff;
        case ObjFamily::Elf: return elf;
        case ObjFamily::MachO: return macho;
        case ObjFamily::Wasm: return wasm;
        case ObjFamily::Unknown:
        default: return true;
    }
}

CacheEntry* findEntry(std::vector<CacheEntry>& manifest, const std::string& obj) {
    for (CacheEntry& e : manifest) {
        if (e.obj == obj) return &e;
    }
    return nullptr;
}

// Phase A (single-threaded): read, hash, decide up-to-date vs recompile.
// A true return means the decision is made; job.needsCompile is set for each
// TU that must be compiled. Side effects on cached TUs (manifest/objects)
// happen here, not in the pool.
bool inspectTU(BuildState& st, ObjectJob& job) {
    std::string content;
    if (!readTextFile(job.cpp, content)) {
        std::fprintf(stderr, "cp3c: cannot read '%s'\n", job.cpp.string().c_str());
        return false;
    }
    job.hash = std::to_string(fnv1a64(content));
    std::error_code ec;
    CacheEntry* existing = findEntry(st.manifest, job.obj.string());
    // The object on disk must also match what this toolchain produces - the
    // .cp3 directory can be shared with a different OS's toolchain, whose
    // objects are not linkable here (see objectMatchesFamily).
    const bool objReusable = fs::exists(job.obj, ec) &&
                             objectMatchesFamily(job.obj, expectedObjFamily(st.tc));
    job.objReusable = objReusable;
    // The TU's manifest fingerprint: the per-module pipeline keys user TUs on
    // the shared header's content hash plus the runtime fingerprint; every
    // other TU keeps the whole-tree fingerprint (runtime headers included).
    const std::string tuFingerprint = job.dependencyHash.empty()
                                          ? st.headerFingerprint
                                          : job.dependencyHash;
    // The entry also carries the include-tree fingerprint it was built under:
    // the manifest does not track which headers a TU includes, so an include
    // change retires every entry on its next build (detail/cache.hpp).
    const bool upToDate = !st.force && existing != nullptr &&
                          existing->hash == job.hash &&
                          existing->profile == st.profile &&
                          existing->header == tuFingerprint &&
                          existing->compiler == st.compilerKey &&
                          objReusable;
    if (upToDate) {
        existing->source = job.sourceKey;
        st.objects.push_back(job.obj);
        job.clangRc = -1;  // reused
        return true;
    }
    job.needsCompile = true;
    return true;
}

// True when any job needs a clang++ pass; used once by the caller to print the
// progress line before the pool starts.
bool anyTUNeedsCompile(const std::vector<ObjectJob>& jobs) {
    for (const ObjectJob& job : jobs) {
        if (job.needsCompile) return true;
    }
    return false;
}

// Worker body: build the args and spawn clang++ for one job. Safe to call from
// as many threads as jobs remain; each call owns its job's writes only.
void compileOneJob(BuildState& st, ObjectJob& job) {
    std::vector<std::string> flags = st.nativeFlags.compile;
    if (!job.extraFlags.empty()) {
        flags.insert(flags.end(), job.extraFlags.begin(), job.extraFlags.end());
    }
    if (!st.pchInclude.empty()) {
        flags.push_back("-include-pch");
        flags.push_back(st.pchInclude);
    }
    const CompileStep step{st.layout.includeDirs, job.cpp, job.obj,
                           std::move(flags)};
    const std::vector<std::string> args =
        compileArgs(st.tc, st.opt.compile.profile, step, st.optimizations);
    if (st.opt.verbose) {
        std::fprintf(stderr, "cp3c: %s\n", describeCommand(args).c_str());
    }
    job.clangRc = runProcess(args);
    if (job.clangRc != 0) {
        // Both clang and the C++ linker print clang's own diagnostic on
        // stderr; no CP3-specific wrapper keeps the first error visible.
        std::fprintf(stderr, "cp3c: C++ compilation failed (exit %d)\n",
                     job.clangRc);
    }
}

// Phase B: fan out the dirty TUs onto a pool bounded by cores (overridable
// with CP3C_BUILD_PARALLEL). The pool drains even when a job fails so every
// error is reported once; callers read clangRc per job afterwards.
void compileDirtyJobs(BuildState& st, std::vector<ObjectJob>& jobs) {
    std::size_t threads = std::thread::hardware_concurrency();
    if (const char* env = std::getenv("CP3C_BUILD_PARALLEL")) {
        try {
            const std::size_t override = std::stoull(env);
            if (override > 0) threads = override;
        } catch (const std::exception&) {
            std::fprintf(stderr, "cp3c: CP3C_BUILD_PARALLEL='%s' is not a number; using %zu threads\n",
                         env, threads);
        }
    }
    if (threads > 1) {
        std::size_t dirty = 0;
        for (const ObjectJob& job : jobs) if (job.needsCompile) ++dirty;
        if (dirty == 0) return;
        threads = std::min(threads, dirty);
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> pool;
        pool.reserve(threads);
        auto worker = [&st, &jobs, &next]() {
            for (;;) {
                const std::size_t i = next.fetch_add(1);
                if (i >= jobs.size()) break;
                if (!jobs[i].needsCompile) continue;
                compileOneJob(st, jobs[i]);
            }
        };
        for (std::size_t i = 0; i < threads; ++i) pool.emplace_back(worker);
        for (std::thread& t : pool) t.join();
        return;
    }
    for (ObjectJob& job : jobs) {
        if (job.needsCompile) compileOneJob(st, job);
    }
}

// Phase C (single-threaded): manifest insert/update + objects registration.
// Called once per job in build order, after the pool drains.
bool finalizeTU(BuildState& st, ObjectJob& job) {
    if (job.clangRc == -1) {
        return true;  // cache reuse already registered
    }
    if (job.clangRc != 0) {
        return false;
    }
    st.anyCompiled = true;
    const std::string tuFingerprint = job.dependencyHash.empty()
                                          ? st.headerFingerprint
                                          : job.dependencyHash;
    CacheEntry* existing = findEntry(st.manifest, job.obj.string());
    if (existing == nullptr) {
        st.manifest.push_back(CacheEntry{st.profile, job.sourceKey, job.hash,
                                         tuFingerprint, st.compilerKey,
                                         job.cpp.string(), job.obj.string()});
    } else {
        existing->profile = st.profile;
        existing->source = job.sourceKey;
        existing->hash = job.hash;
        existing->header = tuFingerprint;
        existing->compiler = st.compilerKey;
        existing->cpp = job.cpp.string();
    }
    st.objects.push_back(job.obj);
    return true;
}

// The up-to-date-decision + pool + finalize for one job list. A false return
// means at least one TU failed; all TUs still compiled, so diagnostics are
// complete before the caller aborts.
bool compileJobList(BuildState& st, std::vector<ObjectJob>& jobs) {
    bool inspectOk = true;
    for (ObjectJob& job : jobs) {
        if (!inspectTU(st, job)) inspectOk = false;
    }
    if (!inspectOk) return false;
    if (!anyTUNeedsCompile(jobs)) return true;
    if (st.showProgress && !st.compileProgressShown) {
        reportRunProgress(true, false, "compiling native C++ sources...");
        st.compileProgressShown = true;
    }
    compileDirtyJobs(st, jobs);
    bool ok = true;
    for (ObjectJob& job : jobs) {
        if (!finalizeTU(st, job)) ok = false;
    }
    return ok;
}

// ---- Precompiled header for the shared generated prologue (dev profile) ---
// Every generated TU re-parses the same standard C++ headers and the core
// runtime support headers; clang spends most per-TU time there. A single PCH
// per (toolchain, dev-profile, header fingerprint, prefix) covers all of them.
// Local probe: one runtime TU went from ~740ms to ~280ms with this prefix.
// Release is a single whole-program TU where a PCH does not pay, so this is
// Incremental-only. If the PCH build fails the TUs compile without it - never
// a hard error.
// The prefix is the generated prologue plus the std-family headers that pull
// in the heavy libstdc++ machinery (<chrono> alone costs ~1s of clang time).
// cp3/async/promise.hpp is the async ABI every std module that uses promises
// includes; it is under src/runtime/include, so the include-tree fingerprint
// already invalidates the PCH when it changes.
const char* kPchPrefixSource = R"hpp(// cp3c dev-profile PCH prefix - the shared prologue + heavy std headers.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "cp3/core/cycle.hpp"
#include "cp3/core/checked.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"
#include "cp3/core/value.hpp"
#include "cp3/async/promise.hpp"
)hpp";

// Builds (or reuses) the PCH. Returns false when the profile is not the dev
// profile or the PCH could not be produced; outPch is set on success. The
// stamp captures everything the PCH depends on: the toolchain key, the dev
// profile, the include-tree fingerprint and the prefix content - any of them
// changing rebuilds it.
bool buildOrReusePch(BuildState& st, std::string& outPch) {
    if (st.opt.compile.profile != BuildProfile::Incremental) {
        return false;
    }
    const fs::path prefixPath = st.layout.buildDir / "_cp3_pch_prefix.hpp";
    const fs::path pchPath = st.layout.buildDir / "_cp3_pch.pch";
    const fs::path stampPath = st.layout.buildDir / "_cp3_pch.stamp";
    std::string current;
    if (!readTextFile(prefixPath, current) || current != kPchPrefixSource) {
        writeTextFile(prefixPath, kPchPrefixSource, nullptr);
    }
    const std::string prefixHash = std::to_string(fnv1a64(kPchPrefixSource));
    const std::string expected = st.compilerKey + "|" + st.profile + "|" +
                                 st.headerFingerprint + "|" + prefixHash;
    std::string stamp;
    std::error_code ec;
    if (readTextFile(stampPath, stamp) && stamp == expected &&
        fs::is_regular_file(pchPath, ec)) {
        outPch = pchPath.string();
        return true;
    }
    std::vector<std::string> args;
    args.push_back(st.tc.program);
    for (const std::string& flag : st.tc.targetFlags) args.push_back(flag);
    args.push_back("-std=c++20");
    for (const fs::path& inc : st.layout.includeDirs) {
        args.push_back("-I" + inc.string());
    }
    // The defining preview: same macros the TUs see, or the PCH is not
    // equivalent to the headers it replaces.
    for (const std::string& f : st.nativeFlags.compile) args.push_back(f);
    args.push_back("-x");
    args.push_back("c++-header");
    args.push_back(prefixPath.string());
    args.push_back("-o");
    args.push_back(pchPath.string());
    if (st.opt.verbose) {
        std::fprintf(stderr, "cp3c: %s\n", describeCommand(args).c_str());
    }
    if (runProcess(args) != 0) {
        std::fprintf(stderr,
                     "cp3c: PCH build failed; continuing without it\n");
        return false;
    }
    writeTextFile(stampPath, expected, nullptr);
    outPch = pchPath.string();
    return true;
}

// Forward declaration - defined below with the link-stamp helpers.
std::string fileStamp(const fs::path& path);

// Builds the job list for the extra TU modules (spec 13), the entry TU, the
// runtime TUs, and the package implementations. The manifest is NOT saved
// here; compileAll saves it after every TU settles.
bool buildJobs(BuildState& st, const BuildRequest& request,
               const fs::path& cppPath, const fs::path& objPath,
               const std::string& entryKey, std::vector<ObjectJob>& jobs) {
    // The plural pipeline's shared header goes beside the unit TUs; they
    // quote-include it relative to the build directory. User TUs (units and
    // the entry) key their manifest fingerprint on it plus the runtime
    // fingerprint: a body-only edit leaves it identical, so dependent TUs
    // stay cached and only the edited module recompiles. The entry TU lives
    // under a module-name subdirectory (e.g. .cp3/build/src/index.cpp),
    // where the quoted include cannot see the header beside the unit TUs, so
    // user TUs also get -I<buildDir> explicitly.
    std::string unitFingerprint;
    const std::vector<std::string> userInclude{
        "-I" + st.layout.buildDir.string()};
    if (!request.sharedProgramHeader.empty()) {
        if (request.sharedProgramHeaderName.empty()) {
            return false;
        }
        const fs::path headerPath =
            st.layout.buildDir / request.sharedProgramHeaderName;
        writeTextFile(headerPath, request.sharedProgramHeader, nullptr);
        unitFingerprint = std::to_string(fnv1a64(request.sharedProgramHeader)) +
                          ":" + st.headerFingerprint;
    }
    if (request.extraModules != nullptr) {
        for (const BuildRequest::ExtraModule& extra : *request.extraModules) {
            if (extra.module == nullptr) continue;
            const fs::path cpp = st.layout.buildDir / (extra.moduleName + ".cpp");
            const fs::path hpp = st.layout.buildDir / (extra.moduleName + ".hpp");
            const fs::path obj = st.layout.buildDir / (extra.moduleName + ".o");
            writeTextFile(cpp, extra.module->source, nullptr);
            if (!extra.module->header.empty()) {
                writeTextFile(hpp, extra.module->header, nullptr);
            }
            ObjectJob job;
            job.sourceKey = extra.sourceKey;
            job.cpp = cpp;
            job.obj = obj;
            job.dependencyHash = unitFingerprint;
            job.extraFlags = userInclude;
            jobs.push_back(std::move(job));
        }
    }
    // Entry TU first, then the runtime TUs the program actually needs.
    ObjectJob entryJob;
    entryJob.sourceKey = entryKey;
    entryJob.cpp = cppPath;
    entryJob.obj = objPath;
    entryJob.dependencyHash = unitFingerprint;
    entryJob.extraFlags = userInclude;
    jobs.push_back(std::move(entryJob));
    // D72: parent-qualify the object name - the std families bring TUs that
    // share a stem with the core ones (src/std/crypto/runtime.cpp and
    // src/runtime/src/core/runtime.cpp both stem to "runtime").
    for (const fs::path& rt : st.layout.runtimeSources) {
        const fs::path rtObj =
            st.layout.buildDir /
            ("_cp3_runtime_" + rt.parent_path().filename().string() + "_" +
             rt.stem().string() + ".o");
        ObjectJob rtJob;
        rtJob.sourceKey = rt.string();
        rtJob.cpp = rt;
        rtJob.obj = rtObj;
        jobs.push_back(std::move(rtJob));
    }
    // Packages: cached objects (if available) are linked directly and recorded
    // in the manifest; source-only packages compile here.
    if (request.packageImplementations != nullptr) {
        for (const BuildRequest::PackageImplementation& impl :
             *request.packageImplementations) {
            const fs::path objKey = impl.object;
            std::error_code ec;
            if (impl.cached) {
                if (!fs::is_regular_file(impl.object, ec)) {
                    std::fprintf(stderr,
                                 "cp3c: cached package artifact '%s' is missing; "
                                 "reinstall the package\n",
                                 impl.object.string().c_str());
                    return false;
                }
                const std::string hash = fileStamp(impl.object);
                CacheEntry* existing = findEntry(st.manifest, objKey.string());
                if (existing == nullptr) {
                    st.manifest.push_back(CacheEntry{st.profile,
                                                     impl.sourceKey,
                                                     hash,
                                                     st.headerFingerprint,
                                                     st.compilerKey,
                                                     impl.object.string(),
                                                     impl.object.string()});
                } else {
                    existing->profile = st.profile;
                    existing->source = impl.sourceKey;
                    existing->hash = hash;
                    existing->header = st.headerFingerprint;
                    existing->compiler = st.compilerKey;
                    existing->cpp = impl.object.string();
                }
                st.objects.push_back(impl.object);
                continue;
            }
            // A beta-one package was generated as an entry program, so its C++
            // carries an `int main()`. The package is linked as a library;
            // rename that symbol so it cannot collide with the consumer's main.
            // The package initializer is not called (packages hold no top-level
            // state in the beta-one surface).
            const std::string stem = impl.source.stem().string();
            const fs::path obj =
                st.layout.buildDir /
                ("pkg_" + std::to_string(fnv1a64(impl.sourceKey)) + "_" + stem +
                 ".o");
            std::vector<std::string> flags{
                "-Dmain=cp3_pkg_main_" + std::to_string(fnv1a64(impl.sourceKey))};
            ObjectJob pkgJob;
            pkgJob.sourceKey = impl.sourceKey;
            pkgJob.cpp = impl.source;
            pkgJob.obj = obj;
            pkgJob.extraFlags = std::move(flags);
            jobs.push_back(std::move(pkgJob));
        }
    }
    return true;
}

// Runs the full compile pipeline: builds the job list, finishes the
// up-to-date decisions, fans out clang++ across the pool, and only then
// saves the manifest.
bool compileAll(BuildState& st, const BuildRequest& request,
                const fs::path& cppPath, const fs::path& objPath,
                const std::string& entryKey) {
    std::vector<ObjectJob> jobs;
    if (!buildJobs(st, request, cppPath, objPath, entryKey, jobs))
        return false;
    if (!compileJobList(st, jobs)) return false;
    saveManifest(st.layout.manifestPath, st.manifest);
    return true;
}

// A cheap identity for a file we do not compile ourselves (a package's cached
// object): size plus modification time. The link stamp carries it, so replacing
// the artifact on a reinstall relinks the program even when no local TU changed.
std::string fileStamp(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) return std::string();
    const auto time = fs::last_write_time(path, ec);
    if (ec) return std::to_string(size);
    return std::to_string(size) + ":" +
           std::to_string(static_cast<long long>(
               time.time_since_epoch().count()));
}

// The link step's vendored-archive identity: every "-l<name>" resolved
// against the "-L<dir>" directories in order, stamped with size plus
// modification time. The archive file is what the linker reads, so swapping
// one (a rebuilt uSockets with real code sections, a newly downloaded
// OpenSSL) must relink the program even when no local TU changed - the
// objects, profile and flags would otherwise look unchanged and a stale
// executable would be reused. Resolution follows the driver's own rule
// (first "-L" wins); an unresolvable -l is skipped silently, matching how
// shared system libraries are satisfied without a static archive.
std::string vendoredArchiveStamps(const std::vector<std::string>& linkFlags) {
    std::string stamp;
    std::vector<fs::path> dirs;
    std::vector<std::string> libs;
    for (const std::string& flag : linkFlags) {
        if (flag.rfind("-L", 0) == 0 && flag.size() > 2) {
            dirs.emplace_back(flag.substr(2));
        } else if (flag.rfind("-l", 0) == 0 && flag.size() > 2) {
            libs.push_back(flag.substr(2));
        }
    }
    std::error_code ec;
    for (const std::string& lib : libs) {
        for (const fs::path& dir : dirs) {
            const fs::path candidate = dir / ("lib" + lib + ".a");
            if (fs::is_regular_file(candidate, ec)) {
                stamp += '\n';
                stamp += candidate.string();
                stamp += '\t';
                stamp += fileStamp(candidate);
                break;
            }
        }
    }
    return stamp;
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
    material += '\n';
    material += st.compilerKey;
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
    material += vendoredArchiveStamps(st.nativeFlags.link);
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
        linkArgs(st.tc, st.opt.compile.profile, step, st.optimizations);
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
    if (st.showProgress) {
        reportRunProgress(true, false, "linking native executable...");
    }
    // A still-running dev child keeps the previous exe image mapped on
    // Windows; a fresh link into that path would fail with a sharing
    // violation. Link into a sibling temp file and replace atomically: on
    // Windows, replace-rename (MoveFileEx REPLACE_EXISTING) succeeds on a
    // mapped image, and on POSIX rename over a running binary is fine.
    const fs::path tmpPath =
        st.layout.outDir / (exePath.filename().string() + ".link-tmp");
    const LinkStep tmpStep{st.objects, tmpPath, st.nativeFlags.link};
    const std::vector<std::string> tmpArgs =
        linkArgs(st.tc, st.opt.compile.profile, tmpStep, st.optimizations);
    if (st.opt.verbose) {
        std::fprintf(stderr, "cp3c: %s\n", describeCommand(tmpArgs).c_str());
    }
    const int rc = runProcess(tmpArgs);
    if (rc != 0) {
        std::remove(tmpPath.string().c_str());
        std::fprintf(stderr, "cp3c: linking failed (exit %d)\n", rc);
        return false;
    }

    std::error_code renameEc;
    const bool replaced = [&]() {
#if defined(_WIN32)
        const std::wstring from = tmpPath.wstring();
        const std::wstring to = exePath.wstring();
        if (MoveFileExW(from.c_str(), to.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        return false;
#else
        fs::rename(tmpPath, exePath, renameEc);
        return !renameEc;
#endif
    }();
    if (!replaced) {
        if (st.opt.internalDeferExeReplace) {
            // The dev session completes the swap after the child exits; the
            // fresh binary stays in the .link-tmp beside the exe.
            if (st.opt.verbose) {
                std::fprintf(stderr,
                             "cp3c: replacement of '%s' deferred (the running app is mapped; the swap happens on restart)\n",
                             exePath.string().c_str());
            }
            writeTextFile(stampPath, stamp, nullptr);
            return true;
        }
        std::remove(tmpPath.string().c_str());
        std::fprintf(stderr, "cp3c: cannot replace '%s' (the image is mapped "
                             "or the file is read-only)\n",
                     exePath.string().c_str());
        return false;
    }
    writeTextFile(stampPath, stamp, nullptr);
    return true;
}

// D68: every std module the program imports that has a vendored native backend
// (vendor/third_party/**, D70) contributes include roots and runtime TUs to `layout`
// plus compile/link flags, so a program that never imports std/network pays
// nothing for uWebSockets. Returns nullopt after reporting on a missing packed
// path, or on a native backend that is incomplete.
//
// A source whose row gives it a marker (NativePackage::runtimeSourceMarkers) is
// only linked when the generated program text names one of its markers, so a
// program that imports a family for one member does not link the whole family.
std::optional<NativeBuildFlags> applyNativePackages(
    const CliOptions& opt, const std::vector<std::string>& importedStdModules,
    BuildLayout& layout, const std::string& programText,
    const Toolchain& toolchain) {
    NativeBuildFlags flags;
    std::vector<const NativePackage*> natives =
        selectNativePackages(importedStdModules);
    // The `std/<mod>/types` facade modules lower their nominal classes onto
    // the runtime spellings (`::cp3::runtime::url::URL`), so the generated
    // text carries the family headers without the program importing the
    // runtime module at all. Select the family rows by content (the regex/
    // collections precedent) or a facade-only compile cannot find the
    // header it was emitted.
    if (programText.find("cp3/std/url.hpp") != std::string::npos) {
        const std::vector<const NativePackage*> row =
            selectNativePackages({"std/url"});
        natives.insert(natives.end(), row.begin(), row.end());
    }
    if (programText.find("cp3/std/buffer.hpp") != std::string::npos) {
        const std::vector<const NativePackage*> row =
            selectNativePackages({"std/buffer"});
        natives.insert(natives.end(), row.begin(), row.end());
    }
    if (programText.find("cp3/std/text.hpp") != std::string::npos) {
        const std::vector<const NativePackage*> row =
            selectNativePackages({"std/text"});
        natives.insert(natives.end(), row.begin(), row.end());
    }
    if (programText.find("cp3/std/date.hpp") != std::string::npos) {
        const std::vector<const NativePackage*> row =
            selectNativePackages({"std/date"});
        natives.insert(natives.end(), row.begin(), row.end());
    }
    if (programText.find("cp3/std/time.hpp") != std::string::npos) {
        const std::vector<const NativePackage*> row =
            selectNativePackages({"std/time"});
        natives.insert(natives.end(), row.begin(), row.end());
    }
    if (programText.find("cp3/std/hash.hpp") != std::string::npos) {
        const std::vector<const NativePackage*> row =
            selectNativePackages({"std/hash"});
        natives.insert(natives.end(), row.begin(), row.end());
    }
    if (programText.find("cp3/std/network.hpp") != std::string::npos) {
        const std::vector<const NativePackage*> row =
            selectNativePackages({"std/network"});
        natives.insert(natives.end(), row.begin(), row.end());
    }
    std::sort(natives.begin(), natives.end());
    natives.erase(std::unique(natives.begin(), natives.end()), natives.end());
    // Core-language backends (D48) join the same list; selected from the
    // generated module's content, not the import graph. The network
    // regex-routing TUs call `Regex::compile`/`exec` too; a regex-routed
    // program names `addRegexRoute(` / `PathSpec` instead of the header, so
    // both spellings select the regex backend.
    const bool usesRegex =
        programText.find("cp3/core/regex.hpp") != std::string::npos ||
        programText.find("addRegexRoute(") != std::string::npos ||
        programText.find("PathSpec") != std::string::npos;
    // `Object.fromEntries` / `Object.groupBy` (global builtins, no import)
    // lower onto the collections runtime, so the generated text names
    // cp3/std/collections.hpp even when the program never imports
    // "std/collections"; select that row's backend (include roots for the
    // vendored unordered_dense/sodium/xxhash, the bucket_hash TU and its
    // link) the same content-driven way as the regex row.
    const bool usesCollections =
        programText.find("cp3/std/collections.hpp") != std::string::npos;
    const std::vector<const NativePackage*> core =
        selectCoreNativePackages(usesRegex, usesCollections);
    for (const NativePackage* row : core) {
        if (std::find(natives.begin(), natives.end(), row) == natives.end()) {
            natives.push_back(row);
        }
    }
    if (natives.empty()) return flags;

    auto reportBroken = [](const char* spec, const char* what,
                           const fs::path& path) {
        std::fprintf(stderr,
                     "cp3c: native backend for '%s' is incomplete: missing "
                     "%s '%s'\n",
                     spec, what, path.string().c_str());
    };
    std::error_code ec;
    const fs::path repo = repositoryRoot();
    // Native archives follow the compilation target. Prefer an explicit
    // target triple over cp3c's host architecture for cross builds.
    std::string triple;
    for (const std::string& flag : toolchain.targetFlags) {
        if (flag.rfind("--target=", 0) == 0) {
            triple = flag.substr(9);
            break;
        }
    }
    std::transform(triple.begin(), triple.end(), triple.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::string targetPlatform;
    bool targetIsLinux = false;
    bool targetIsWindows = false;
    if (triple.find("linux") != std::string::npos) {
        targetIsLinux = true;
        if (triple.find("aarch64") != std::string::npos ||
            triple.find("arm64") != std::string::npos) {
            targetPlatform = "linux-arm64";
        } else if (triple.find("x86_64") != std::string::npos ||
                   triple.find("amd64") != std::string::npos) {
            targetPlatform = "linux-x64";
        }
    } else if (triple.find("windows") != std::string::npos ||
               triple.find("mingw") != std::string::npos) {
        targetIsWindows = true;
        if (triple.find("aarch64") != std::string::npos ||
            triple.find("arm64") != std::string::npos) {
            targetPlatform = "windows-arm64";
        } else if (triple.find("x86_64") != std::string::npos ||
                   triple.find("amd64") != std::string::npos) {
            targetPlatform = "windows-x64";
        }
    } else if (triple.empty()) {
#if defined(__linux__)
        targetIsLinux = true;
#  if defined(__aarch64__)
        targetPlatform = "linux-arm64";
#  elif defined(__x86_64__)
        targetPlatform = "linux-x64";
#  endif
#elif defined(_WIN32)
        targetIsWindows = true;
#  if defined(_M_ARM64)
        targetPlatform = "windows-arm64";
#  elif defined(_M_X64) || defined(__x86_64__)
        targetPlatform = "windows-x64";
#  endif
#endif
    }
    for (const NativePackage* pkg : natives) {
        const NativePackagePlatform* platform = nullptr;
        for (const NativePackagePlatform& candidate : pkg->platforms) {
            if (targetPlatform == candidate.id) {
                platform = &candidate;
                break;
            }
        }
        if (!pkg->platforms.empty() && (targetIsLinux || targetIsWindows) &&
            platform == nullptr) {
            std::fprintf(stderr,
                         "cp3c: native backend for '%s' has no vendor archives "
                         "for target '%s'\n",
                         pkg->specifier,
                         targetPlatform.empty() ? triple.c_str()
                                                : targetPlatform.c_str());
            return std::nullopt;
        }
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
        const std::vector<std::string>& compileFlags =
            (platform != nullptr && !platform->compileFlagsOverride.empty())
                ? platform->compileFlagsOverride
                : pkg->compileFlags;
        for (const std::string& flag : compileFlags) {
            flags.compile.push_back(flag);
        }
        const std::vector<std::string>& libDirs =
            platform ? platform->libDirs : pkg->libDirs;
        const std::vector<std::string>& linkFlags =
            platform ? platform->linkFlags : pkg->linkFlags;
        for (const std::string& rel : libDirs) {
            const fs::path dir = resolve(rel);
            if (!fs::is_directory(dir, ec)) {
                reportBroken(pkg->specifier, "library directory", dir);
                return std::nullopt;
            }
            flags.link.push_back("-L" + dir.string());
        }
        for (const std::string& flag : linkFlags) {
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
    if (!request.sharedProgramHeader.empty()) {
        // The plural pipeline keeps the include superset in the shared header;
        // fold it in so the runtime-TU marker scan sees the same markers the
        // single-TU text would carry.
        text += '\n';
        text += request.sharedProgramHeader;
    }
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
    // A TU named by two native rows (for example bucket_hash.cpp via the
    // std/collections and std/process rows) would be compiled into the same
    // object twice and fail the link with a duplicate definition. Keep the
    // first occurrence; order is otherwise preserved.
    {
        std::vector<fs::path> unique;
        unique.reserve(layout.runtimeSources.size());
        for (const fs::path& src : layout.runtimeSources) {
            bool seen = false;
            for (const fs::path& kept : unique) {
                if (kept == src) { seen = true; break; }
            }
            if (!seen) unique.push_back(src);
        }
        layout.runtimeSources = std::move(unique);
    }
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
    const Toolchain* tc = &detectToolchain();
    if (!tc->found()) {
        if (toolchainStrictFailed()) {
            // detectToolchain already printed the strict warning with the
            // source and the missing pin. Return without repeating it.
            if (toolchainWarning().empty()) {
                std::fputs("cp3c: --strict-toolchain: no saved exact pin "
                           "could be resolved\n", stderr);
            }
            return std::nullopt;
        }
        if (!ensureClangAvailable(true)) {
            std::fprintf(stderr,
                         "cp3c: no usable Clang compiler found on this system "
                         "(looked for clang++ in PATH and standard locations)\n");
            std::fprintf(stderr,
                         "      run 'cp3c toolchain' to install one.\n");
            return std::nullopt;
        }
        tc = &detectToolchain();
    }
    if (!tc->found()) {
        std::fprintf(stderr,
                     "cp3c: no usable Clang compiler found on this system\n");
        std::fprintf(stderr, "      run 'cp3c toolchain' to install one.\n");
        return std::nullopt;
    }
    tcOut = tc;
    if (opt.verbose) {
        std::fprintf(stderr, "cp3c: toolchain: %s\n",
                     describeToolchain(*tc).c_str());
        std::fprintf(stderr, "cp3c: toolchain source: %s\n",
                     toolchainSourceDescription().c_str());
        std::fprintf(stderr, "cp3c: toolchain versions: %s\n",
                     toolchainVersionDescription().c_str());
    }
    return applyNativePackages(opt, *request.importedStdModules, layout,
                               generatedProgramText(request, mod), *tc);
}

// The native bindings the program imports: their include roots, defines,
// libraries and runtime DLLs join the build (detail/native_build.hpp).
bool applyRequestNativeBindings(const BuildRequest& request, BuildLayout& layout,
                                 NativeBuildFlags& flags) {
    return applyNativeBindings(request.opt->compile.nativeBindings, layout,
                                flags);
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

    const BuildOptimizations optimizations = buildOptimizationsFor(opt);
    if (opt.pgoGenerate && !opt.pgoGenerateDir.empty()) {
        std::error_code pgoEc;
        fs::create_directories(opt.pgoGenerateDir, pgoEc);
        if (pgoEc) {
            std::fprintf(stderr,
                         "cp3c: cannot create PGO output directory '%s'\n",
                         opt.pgoGenerateDir.c_str());
            return kExitCompileError;
        }
    }

    const fs::path cppPath = layout.buildDir / fs::path(moduleName + ".cpp");
    const fs::path hppPath = layout.buildDir / fs::path(moduleName + ".hpp");
    const fs::path objPath = layout.buildDir / fs::path(moduleName + ".o");
    writeTextFile(cppPath, mod.source, nullptr);
    writeTextFile(hppPath, mod.header, nullptr);

    const std::string programText = generatedProgramText(request, mod);

    const Toolchain* tcPtr = nullptr;
    std::optional<NativeBuildFlags> nativeFlags =
        resolveNativeFlags(opt, request, mod, layout, tcPtr);
    if (!nativeFlags.has_value()) return kExitCompileError;
    const Toolchain& tc = *tcPtr;

#if defined(_WIN32)
    if (opt.pgoGenerate && !hasMinGwProfileRuntime(tc)) {
        std::fprintf(stderr,
                     "cp3c: PGO instrumentation is unavailable: this Clang "
                     "install has no MinGW profile runtime "
                     "(libclang_rt.profile.a).\n"
                     "cp3c: use --pgo <file> with an existing profile, or "
                     "install compiler-rt for the MinGW target.\n");
        return kExitCompileError;
    }
#endif

    // The native bindings the program imports: their include roots, defines,
    // libraries and runtime DLLs join the build (detail/native_build.hpp).
    if (!applyRequestNativeBindings(request, layout, *nativeFlags)) {
        return kExitCompileError;
    }

    // Package implementations include their own generated headers by relative
    // path (`#include "index.hpp"`, `#include "src/index.hpp"`). Add every
    // directory that holds one before the header fingerprint is taken, so a
    // package update retires the objects built against the old headers.
    if (request.packageImplementations != nullptr) {
        for (const BuildRequest::PackageImplementation& impl :
             *request.packageImplementations) {
            for (const fs::path& dir : impl.includeDirs) {
                bool seen = false;
                for (const fs::path& existing : layout.includeDirs) {
                    if (existing == dir) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) layout.includeDirs.push_back(dir);
            }
        }
    }

    selectRuntimeSources(opt, layout, programText);

    // --show-runtime: leave the generated C++ and the exact runtime support it
    // calls in .cp3/build for inspection. The normal compile/link path below
    // still uses the shipped headers and sources.
    if (opt.showRuntime) {
        const fs::path snapshot = copyRuntimeSnapshot(layout, opt.verbose);
        if (snapshot.empty()) return kExitCompileError;
        std::fprintf(stderr, "cp3c: generated C++: %s\n",
                     cppPath.string().c_str());
        std::fprintf(stderr, "cp3c: runtime snapshot: %s\n",
                     snapshot.string().c_str());
    }

    if (opt.verbose && !optimizations.pgoProfile.empty()) {
        std::fprintf(stderr, "cp3c: PGO profile: %s\n",
                     optimizations.pgoProfile.c_str());
    }
    if (opt.verbose && !optimizations.pgoGenerate.empty()) {
        std::fprintf(stderr, "cp3c: PGO instrumentation: %s\n",
                     optimizations.pgoGenerate.c_str());
    }

    const std::string headerFingerprint =
        headerFingerprintOf(layout.includeDirs);
    std::vector<CacheEntry> manifest = loadManifest(layout.manifestPath);
    const std::string profile = buildProfileKey(opt, optimizations);
    const std::string compilerKey = toolchainCacheKey(tc);
    const bool force = (opt.compile.profile == BuildProfile::Release);

    std::vector<fs::path> objects;
    BuildState state{opt,          tc,           layout,       profile,
                     optimizations, *nativeFlags, headerFingerprint,
                     compilerKey,  force,        manifest,     objects,
                     /*anyCompiled=*/false, /*showProgress=*/false,
                     /*compileProgressShown=*/false, /*pchInclude=*/std::string{}};
    state.showProgress = request.execute && !opt.verbose && isTty(stderr);

    // Dev profile only: the PCH must be built before the TUs fan out. Any
    // include-tree or toolchain change is already covered by the stamp.
    if (std::string pch; buildOrReusePch(state, pch)) {
        state.pchInclude = std::move(pch);
    }

    if (!compileAll(state, request, cppPath, objPath, entry.string())) {
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

    reportRunProgress(true, opt.verbose, "starting program...");
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
