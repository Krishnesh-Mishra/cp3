// =============================================================================
// src/cli/src/toolchain/toolchain.cpp - system C++ toolchain detection and driving (DECISIONS D12).
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/toolchain.hpp.
//
// cp3c invokes a compiler driver directly (never CMake) for the compile and
// link phases; this file owns which driver that is and how it is invoked.
// =============================================================================
#include "detail/toolchain/toolchain.hpp"

#include <string>
#include <vector>

#include "detail/toolchain/toolchain_select.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string firstOutputLine(const std::string& text) {
    const std::size_t end = text.find('\n');
    std::string line = text.substr(0, end);
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    return line;
}

std::string programVersion(const std::string& program, bool msvc) {
    std::vector<std::string> args{program};
    if (!msvc) args.push_back("--version");
    std::string out;
    if (runProcessCapture(args, out) < 0) return {};
    return normalizeCompilerVersion(firstOutputLine(out));
}

struct Candidate {
    const char* exe;
    const char* name;
    bool msvc;
    bool clang;
};

// D12 order, first present wins. Duplicate entries are deduplicated by exe.
#if defined(_WIN32)
constexpr Candidate kOrder[] = {
    {"cl", "MSVC cl", true, false},
    {"clang++", "Clang", false, true},
    {"g++", "GCC", false, false},
    {"clang++", "Clang", false, true},  // last-resort repeat (deduped below)
};
#else
constexpr Candidate kOrder[] = {
    {"clang++", "Clang", false, true},
    {"g++", "GCC", false, false},
    {"g++", "GCC", false, false},        // last-resort repeat (deduped below)
    {"clang++", "Clang", false, true},
};
#endif

bool alreadyTried(const std::vector<std::string>& tried, const char* exe) {
    for (const std::string& t : tried) {
        if (t == exe) return true;
    }
    return false;
}

Toolchain probeToolchain() {
    std::vector<std::string> tried;
    for (const Candidate& c : kOrder) {
        if (alreadyTried(tried, c.exe)) continue;
        tried.emplace_back(c.exe);

        std::string found;
        if (findExecutable(c.exe, found)) {
            Toolchain tc;
            tc.program = found;
            tc.name = c.name;
            tc.msvc = c.msvc;
            tc.clang = c.clang;
            tc.id = c.msvc ? "msvc" : (c.clang ? "clang" : "gcc");
            tc.version = programVersion(found, c.msvc);
            tc.policy = "auto";
            // The target flags the driver needs on this machine (a Windows
            // Clang without MSVC needs the GNU triple to find a C++
            // standard library at all).
            tc.targetFlags =
                toolchainTargetFlags(tc.clang, tc.msvc, tc.program);
            return tc;
        }
    }
    return Toolchain{};
}

std::vector<std::string> profileCompileFlags(BuildProfile profile, bool msvc) {
    std::vector<std::string> flags;
    switch (profile) {
        case BuildProfile::Debug:
            flags.push_back(msvc ? "/Od" : "-O0");
            flags.push_back(msvc ? "/Zi" : "-g");
            break;
        case BuildProfile::Release:
            flags.push_back(msvc ? "/O2" : "-O2");
            flags.push_back(msvc ? "/GL" : "-flto");
            break;
        case BuildProfile::Incremental:
        default:
            flags.push_back(msvc ? "/O2" : "-O2");
            break;
    }
    return flags;
}

}  // namespace

Toolchain probeAutoToolchain() { return probeToolchain(); }

std::string describeToolchain(const Toolchain& tc) {
    if (!tc.found()) return "(none found)";
    std::string text = tc.name;
    if (!tc.version.empty()) text += " " + tc.version;
    return text + " (" + tc.program + ")";
}

std::string toolchainCacheKey(const Toolchain& tc) {
    if (!tc.found()) return "none";
    std::string key = (tc.id.empty() ? tc.name : tc.id) + "|" +
                      (tc.version.empty() ? "unknown" : tc.version) + "|" +
                      tc.program;
    // The target is part of the compiler identity: the same driver under a
    // different target produces different objects.
    for (const std::string& flag : tc.targetFlags) key += "|" + flag;
    return key;
}

// Does this toolchain compile for a MinGW-style Windows target?
//
// A Clang driver under the GNU target links against MinGW's libstdc++, whose
// thread support lives in libwinpthread. The GCC driver adds that library
// itself; the Clang driver does not, so a fully static link would fail with
// undefined pthread symbols. Naming it explicitly keeps the static runtime
// identical to the GCC path.
bool usesMingwTarget(const Toolchain& tc) {
    for (const std::string& flag : tc.targetFlags) {
        if (flag.rfind("--target=", 0) == 0 &&
            flag.find("windows-gnu") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> compileArgs(const Toolchain& tc, BuildProfile profile,
                                     const CompileStep& step) {
    std::vector<std::string> args;
    args.push_back(tc.program);
    if (tc.msvc) {
        args.push_back("/nologo");
        args.push_back("/std:c++20");
        args.push_back("/EHsc");
        args.push_back("/c");
        args.push_back(step.cpp.string());
        args.push_back("/Fo" + step.obj.string());
        for (const fs::path& inc : step.includes) {
            args.push_back("/I" + inc.string());
        }
        for (std::string& f : profileCompileFlags(profile, true)) {
            args.push_back(std::move(f));
        }
    } else {
        for (const std::string& flag : tc.targetFlags) args.push_back(flag);
        args.push_back("-std=c++20");
        // Generated code may throw (cp3/Error derives from std::exception), so
        // exceptions stay enabled - that is the default for g++/clang++, and we
        // deliberately do not pass -fno-exceptions.
        args.push_back("-c");
        args.push_back(step.cpp.string());
        args.push_back("-o");
        args.push_back(step.obj.string());
        for (const fs::path& inc : step.includes) {
            args.push_back("-I" + inc.string());
        }
        for (std::string& f : profileCompileFlags(profile, false)) {
            args.push_back(std::move(f));
        }
    }
    // Native-backend flags last: register-level defines (e.g. UWS_NO_ZLIB)
    // apply to every TU of a program that imports the module.
    for (const std::string& f : step.extraFlags) args.push_back(f);
    return args;
}

std::vector<std::string> linkArgs(const Toolchain& tc, BuildProfile profile,
                                  const LinkStep& step) {
    std::vector<std::string> args;
    if (tc.msvc) {
        args.push_back("link");
        args.push_back("/nologo");
        args.push_back("/OUT:" + step.exe.string());
        for (const fs::path& obj : step.objects) args.push_back(obj.string());
        if (profile == BuildProfile::Release) args.push_back("/LTCG");
    } else {
        args.push_back(tc.program);
        for (const std::string& flag : tc.targetFlags) args.push_back(flag);
        args.push_back("-o");
        args.push_back(step.exe.string());
        for (const fs::path& obj : step.objects) args.push_back(obj.string());
        if (profile == BuildProfile::Release) args.push_back("-flto");
#if defined(_WIN32)
        // Startup (startup perf wave): every dynamically linked C++ runtime DLL
        // is one more image the loader maps and one more DllMain that runs
        // before main. On Windows there is no dlopen/NSS contract to keep, so a
        // fully static runtime is safe and measurably cheaper: libstdc++-6.dll,
        // libgcc_s_seh-1.dll and libwinpthread-1.dll disappear from the import
        // table. Linux keeps the dynamic runtime (static glibc breaks
        // getaddrinfo/NSS for std/network).
        args.push_back("-static");
#  if defined(_WIN32)
        if (tc.clang && usesMingwTarget(tc)) args.push_back("-lwinpthread");
#  endif
#endif
    }
    // Native-backend libraries last: -L dirs then -l archives, after every
    // object, so static-archive resolution order is correct.
    for (const std::string& f : step.extraFlags) args.push_back(f);
    return args;
}

}  // namespace cp3::cli
