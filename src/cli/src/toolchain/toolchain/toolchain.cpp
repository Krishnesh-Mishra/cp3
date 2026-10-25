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

std::vector<std::string> profileCompileFlags(BuildProfile profile) {
    std::vector<std::string> flags;
    switch (profile) {
        case BuildProfile::Debug:
            flags.push_back("-O0");
            flags.push_back("-g");
            break;
        case BuildProfile::Release:
            flags.push_back("-O2");
            flags.push_back("-flto");
            break;
        case BuildProfile::Incremental:
        default:
            flags.push_back("-O2");
            break;
    }
    return flags;
}

// Beta one drives Clang to the MinGW-w64 GNU target on Windows. That target
// links against MinGW's libstdc++, whose thread support lives in
// libwinpthread. Clang does not add that library itself, so a fully static
// link names it explicitly.
bool usesMingwTarget(const Toolchain& tc) {
#if defined(_WIN32)
    (void)tc;
    return true;
#else
    (void)tc;
    return false;
#endif
}

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
                                     const CompileStep& step) {
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
    for (std::string& f : profileCompileFlags(profile)) {
        args.push_back(std::move(f));
    }
    // Native-backend flags last: register-level defines (e.g. UWS_NO_ZLIB)
    // apply to every TU of a program that imports the module.
    for (const std::string& f : step.extraFlags) args.push_back(f);
    return args;
}

std::vector<std::string> linkArgs(const Toolchain& tc, BuildProfile profile,
                                  const LinkStep& step) {
    std::vector<std::string> args;
    args.push_back(tc.program);
    for (const std::string& flag : tc.targetFlags) args.push_back(flag);
    args.push_back("-o");
    args.push_back(step.exe.string());
    for (const fs::path& obj : step.objects) args.push_back(obj.string());
    if (profile == BuildProfile::Release) args.push_back("-flto");
#if defined(_WIN32)
    // Startup (startup perf wave): every dynamically linked C++ runtime DLL
    // is one more image the loader maps and one more DllMain that runs before
    // main. On Windows there is no dlopen/NSS contract to keep, so a fully
    // static runtime is safe and measurably cheaper. Linux keeps the dynamic
    // runtime (static glibc breaks getaddrinfo/NSS for std/network).
    args.push_back("-static");
    if (usesMingwTarget(tc)) args.push_back("-lwinpthread");
#endif
    // Native-backend libraries last: -L dirs then -l archives, after every
    // object, so static-archive resolution order is correct.
    for (const std::string& f : step.extraFlags) args.push_back(f);
    return args;
}

}  // namespace cp3::cli
