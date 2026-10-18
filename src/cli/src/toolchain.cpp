// =============================================================================
// cli/toolchain.cpp - system C++ toolchain detection and driving (DECISIONS D12).
//
// OWNERSHIP: cli module (src/cli/**). Implements private/toolchain.hpp.
//
// cp3c invokes a compiler driver directly (never CMake) for the compile and
// link phases; this file owns which driver that is and how it is invoked.
// =============================================================================
#include "private/toolchain.hpp"

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
                                     const std::vector<fs::path>& includes,
                                     const fs::path& cpp, const fs::path& obj) {
    std::vector<std::string> args;
    args.push_back(tc.program);
    if (tc.msvc) {
        args.push_back("/nologo");
        args.push_back("/std:c++20");
        args.push_back("/EHsc");
        args.push_back("/c");
        args.push_back(cpp.string());
        args.push_back("/Fo" + obj.string());
        for (const fs::path& inc : includes) args.push_back("/I" + inc.string());
        for (std::string& f : profileCompileFlags(profile, true)) {
            args.push_back(std::move(f));
        }
    } else {
        args.push_back("-std=c++20");
        // Generated code may throw (cp3/Error derives from std::exception), so
        // exceptions stay enabled - that is the default for g++/clang++, and we
        // deliberately do not pass -fno-exceptions.
        args.push_back("-c");
        args.push_back(cpp.string());
        args.push_back("-o");
        args.push_back(obj.string());
        for (const fs::path& inc : includes) args.push_back("-I" + inc.string());
        for (std::string& f : profileCompileFlags(profile, false)) {
            args.push_back(std::move(f));
        }
    }
    return args;
}

std::vector<std::string> linkArgs(const Toolchain& tc, BuildProfile profile,
                                  const std::vector<fs::path>& objects,
                                  const fs::path& exe) {
    std::vector<std::string> args;
    if (tc.msvc) {
        args.push_back("link");
        args.push_back("/nologo");
        args.push_back("/OUT:" + exe.string());
        for (const fs::path& obj : objects) args.push_back(obj.string());
        if (profile == BuildProfile::Release) args.push_back("/LTCG");
    } else {
        args.push_back(tc.program);
        args.push_back("-o");
        args.push_back(exe.string());
        for (const fs::path& obj : objects) args.push_back(obj.string());
        if (profile == BuildProfile::Release) args.push_back("-flto");
    }
    return args;
}

}  // namespace cp3::cli
