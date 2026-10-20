// =============================================================================
// src/cli/src/toolchain/toolchain.cpp - system C++ toolchain detection and driving (DECISIONS D12).
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/toolchain.hpp.
//
// cp3c invokes a compiler driver directly (never CMake) for the compile and
// link phases; this file owns which driver that is and how it is invoked.
// =============================================================================
#include "detail/toolchain.hpp"

#include <string>
#include <vector>

namespace cp3::cli {

namespace {

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

const Toolchain& detectToolchain() {
    static const Toolchain cached = probeToolchain();
    return cached;
}

std::string describeToolchain(const Toolchain& tc) {
    if (!tc.found()) return "(none found)";
    return tc.name + " (" + tc.program + ")";
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
#endif
    }
    // Native-backend libraries last: -L dirs then -l archives, after every
    // object, so static-archive resolution order is correct.
    for (const std::string& f : step.extraFlags) args.push_back(f);
    return args;
}

}  // namespace cp3::cli
