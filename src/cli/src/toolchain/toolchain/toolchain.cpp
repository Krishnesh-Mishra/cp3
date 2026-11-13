// =============================================================================
// src/cli/src/toolchain/toolchain.cpp - Clang detection and driving.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/toolchain.hpp.
//
// cp3c invokes clang++ directly (never CMake) for the compile and link phases;
// this file owns how that driver is invoked. Beta one is Clang-only.
// =============================================================================
#include "detail/toolchain/toolchain.hpp"

#include <string>
#include <utility>
#include <vector>

#include "detail/toolchain/toolchain_select.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::vector<std::string> optimizationFlags(
    BuildProfile profile, const BuildOptimizations& optimizations) {
    std::vector<std::string> flags;
    if (profile == BuildProfile::Debug) {
        flags.push_back("-O0");
        flags.push_back("-g");
        return flags;
    }
    const std::string level =
        optimizations.optLevel.empty()
            ? (profile == BuildProfile::Release ? "3" : "2")
            : optimizations.optLevel;
    flags.push_back("-O" + level);
    if (optimizations.lto) flags.push_back("-flto");
    if (optimizations.nativeIsa) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || \
    defined(_M_IX86)
        flags.push_back("-march=native");
#elif defined(__aarch64__) || defined(_M_ARM64)
        flags.push_back("-mcpu=native");
#endif
    }
    if (optimizations.unrollLoops) flags.push_back("-funroll-loops");
    if (optimizations.noMathErrno) flags.push_back("-fno-math-errno");
    if (optimizations.noPlt) flags.push_back("-fno-plt");
    // Dead-code elimination (binary footprint): one section per function and
    // per global, so the linker's --gc-sections can drop everything not
    // reachable from the entry point. LTO already deletes unreachable code
    // inside cp3's own objects; this catches the rest - non-LTO inputs and
    // code the LTO pipeline had to keep addressable.
    flags.push_back("-ffunction-sections");
    flags.push_back("-fdata-sections");
    if (!optimizations.pgoProfile.empty()) {
        flags.push_back("-fprofile-instr-use=" + optimizations.pgoProfile);
    }
    if (!optimizations.pgoGenerate.empty()) {
        flags.push_back("-fprofile-instr-generate=" +
                        optimizations.pgoGenerate);
    }
    return flags;
}

// Beta one drives Clang to the MinGW-w64 GNU target on Windows. That target
// links against MinGW's libstdc++, whose thread support lives in
// libwinpthread. Clang does not add that library itself, so a fully static
// link names it explicitly.
#if defined(_WIN32)
bool usesMingwTarget(const Toolchain& tc) {
    (void)tc;
    return true;
}
#endif

}  // namespace

Toolchain probeAutoToolchain() {
    const std::vector<ToolchainCandidate> candidates = toolchainCandidates();
    if (candidates.empty()) return Toolchain{};
    // Auto-detection validates the driver: a clang++ that cannot link a C++20
    // program is not a usable toolchain, and the caller should be offered the
    // installer instead of getting an opaque compile failure later.
    const CandidateStatus status =
        probeToolchainCandidate(candidates.front(), true);
    if (!status.usable) return Toolchain{};

    Toolchain tc;
    tc.program = status.program;
    tc.name = status.candidate.name;
    tc.id = status.candidate.id;
    tc.version = status.version;
    tc.policy = "auto";
    tc.targetFlags = toolchainTargetFlags(tc.program);
    return tc;
}

std::string describeToolchain(const Toolchain& tc) {
    if (!tc.found()) return "(none found)";
    std::string text = tc.name.empty() ? "Clang" : tc.name;
    if (!tc.version.empty()) text += " " + tc.version;
    return text + " (" + tc.program + ")";
}

std::string toolchainCacheKey(const Toolchain& tc) {
    if (!tc.found()) return "none";
    std::string key = "clang|" +
                      (tc.version.empty() ? "unknown" : tc.version) + "|" +
                      tc.program;
    // The target is part of the compiler identity: the same driver under a
    // different target produces different objects.
    for (const std::string& flag : tc.targetFlags) key += "|" + flag;
    return key;
}

std::vector<std::string> compileArgs(const Toolchain& tc, BuildProfile profile,
                                     const CompileStep& step,
                                     const BuildOptimizations& optimizations) {
    std::vector<std::string> args;
    args.push_back(tc.program);
    for (const std::string& flag : tc.targetFlags) args.push_back(flag);
    args.push_back("-std=c++20");
    // Generated code may throw (cp3/Error derives from std::exception), so
    // exceptions stay enabled - that is the default for clang++, and we
    // deliberately do not pass -fno-exceptions.
    args.push_back("-c");
    args.push_back(step.cpp.string());
    args.push_back("-o");
    args.push_back(step.obj.string());
    for (const fs::path& inc : step.includes) {
        args.push_back("-I" + inc.string());
    }
    for (const std::string& f : optimizationFlags(profile, optimizations)) {
        args.push_back(f);
    }
    // Native-backend flags last: register-level defines (e.g. UWS_NO_ZLIB)
    // apply to every TU of a program that imports the module.
    for (const std::string& f : step.extraFlags) args.push_back(f);
    return args;
}

// The object list in an argv is bounded by the platform's process-creation
// command-line limit (_spawnv: 32767 on Windows). When the program is a large
// module graph, hand the linker a response file (`@file`) instead; GNU ld,
// lld and MSVC's link all read them.
bool writeObjectsResponseFile(const std::vector<fs::path>& objects,
                              const fs::path& path) {
    std::string text;
    for (const fs::path& obj : objects) {
        text += obj.string();
        text += '\n';
    }
    return writeTextFile(path, text, nullptr);
}

std::vector<std::string> linkArgs(const Toolchain& tc, BuildProfile profile,
                                  const LinkStep& step,
                                  const BuildOptimizations& optimizations) {
    std::vector<std::string> args;
    args.push_back(tc.program);
    for (const std::string& flag : tc.targetFlags) args.push_back(flag);
    args.push_back("-o");
    args.push_back(step.exe.string());
    // The command line is bounded by the platform's process-creation limit
    // (_spawnv: 32767 characters on Windows); when the object list is long
    // the linker reads it from a response file (`@file`) instead, which GNU
    // ld, lld and link.exe all accept.
    std::size_t total = args[1].size() + args[3].size() + step.exe.string().size();
    for (const fs::path& obj : step.objects) total += obj.string().size() + 1;
    const fs::path responseFile =
        step.exe.parent_path().parent_path() / "cache" / "link-objects.rsp";
    if (total > 30000 && !step.objects.empty() &&
        writeObjectsResponseFile(step.objects, responseFile)) {
        args.push_back("@" + responseFile.string());
    } else {
        for (const fs::path& obj : step.objects) args.push_back(obj.string());
    }
    // LTO code generation happens at link time, so the same optimization
    // settings must be visible here, not only in the compile steps.
    for (const std::string& f : optimizationFlags(profile, optimizations)) {
        args.push_back(f);
    }
    // Dead-code elimination: pair with -ffunction-sections/-fdata-sections on
    // the compile side. ld.lld accepts --gc-sections for both COFF (Windows)
    // and ELF (Linux). Debug builds keep every section for the debugger.
    if (profile != BuildProfile::Debug) args.push_back("-Wl,--gc-sections");
#if defined(_WIN32)
    if (usesMingwTarget(tc)) {
        std::error_code ec;
        const fs::path lld = fs::path(tc.program).parent_path() / "ld.lld.exe";
        if (fs::is_regular_file(lld, ec)) args.push_back("-fuse-ld=lld");
    }
    // Startup (startup perf wave): every dynamically linked C++ runtime DLL
    // is one more image the loader maps and one more DllMain that runs before
    // main. On Windows there is no dlopen/NSS contract to keep, so a fully
    // static runtime is safe and measurably cheaper. Linux keeps the dynamic
    // runtime (static glibc breaks getaddrinfo/NSS for std/network).
    args.push_back("-static");
    if (usesMingwTarget(tc)) args.push_back("-lwinpthread");
#else
    // Linux: default linker, unless Clang's ld.lld is beside it; then lld
    // wins on typical-link-latency. Statically linked C++ runtimes stay a
    // Windows thing (D61/Linux keeps the dynamic runtime).
    {
        std::error_code ec;
        const fs::path lld = fs::path(tc.program).parent_path() / "ld.lld";
        if (fs::is_regular_file(lld, ec)) args.push_back("-fuse-ld=lld");
    }
#endif
    // Native-backend libraries last: -L dirs then -l archives, after every
    // object, so static-archive resolution order is correct.
    for (const std::string& f : step.extraFlags) args.push_back(f);
    return args;
}

}  // namespace cp3::cli
