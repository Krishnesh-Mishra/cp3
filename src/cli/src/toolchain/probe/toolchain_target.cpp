// =============================================================================
// src/cli/src/toolchain/toolchain_target.cpp - compiler target selection.
//
// OWNERSHIP: cli module (src/cli/**). Implements the target half of
// detail/toolchain_select.hpp.
//
// A driver is not always usable with its own default target. On Windows a
// stock LLVM install defaults to the MSVC target (`x86_64-pc-windows-msvc`),
// and without an MSVC installation there is no C++ standard library for that
// target at all: `#include <cstdio>` fails and the family is reported as
// "not usable". The very same driver works against a MinGW GCC installation
// once it targets the GNU triple (`x86_64-w64-windows-gnu`).
//
// So the target is not a user concern: this unit asks the driver for its
// default triple, rewrites the Windows MSVC triple to the matching GNU one
// when a GCC installation is reachable, and verifies the result by really
// compiling, linking and running a probe program. The flags that survive are
// the ones used for the probe AND for every real build, so a family that
// probes clean is a family that builds.
//
// Nothing here forces cross-compilation: only the current machine's own
// architecture and its own installed toolchain are considered.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"

#include <map>
#include <string>
#include <vector>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

// The driver prints one triple per line; take the first non-empty one.
std::string defaultTriple(const std::string& program) {
    std::string out;
    if (runProcessCapture({program, "-print-target-triple"}, out) < 0) {
        return {};
    }
    std::size_t begin = 0;
    while (begin < out.size()) {
        std::size_t end = out.find('\n', begin);
        std::string line = out.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' ||
                line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty()) return line;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

// `x86_64-pc-windows-msvc` -> `x86_64-w64-windows-gnu`.
//
// Only the Windows MSVC triple is rewritten: it is the one that needs a
// standard library cp3c cannot assume. Every other triple (a GNU target
// already, or a macOS/Linux target) is left exactly as the driver reports it.
std::string gnuTripleFor(const std::string& triple) {
    const std::string suffix = "-pc-windows-msvc";
    if (triple.size() <= suffix.size()) return {};
    if (triple.compare(triple.size() - suffix.size(), suffix.size(), suffix) !=
        0) {
        return {};
    }
    const std::string arch = triple.substr(0, triple.size() - suffix.size());
    if (arch.empty()) return {};
    return arch + "-w64-windows-gnu";
}

// The GCC installation prefix, derived from the g++ driver on PATH: its
// directory's parent is the MinGW root (…/mingw64/bin/g++ -> …/mingw64).
// Empty when no g++ is reachable.
std::string gccToolchainPrefix() {
    std::string gpp;
    if (!findExecutable("g++", gpp)) return {};
    const fs::path bin = fs::path(gpp).parent_path();
    if (bin.empty()) return {};
    const fs::path prefix = bin.parent_path();
    if (prefix.empty()) return {};
    return prefix.string();
}

bool compilesWith(const std::string& program,
                  const std::vector<std::string>& targetFlags) {
    const fs::path temp =
        fs::temp_directory_path() / "cp3-target-probe.cpp";
    const fs::path exe = fs::temp_directory_path() / "cp3-target-probe.exe";
    std::error_code ec;
    fs::remove(temp, ec);
    fs::remove(exe, ec);
    const std::string source =
        "#include <cstdio>\n"
        "int main() { std::printf(\"ok\\n\"); return 0; }\n";
    if (!writeTextFile(temp, source, nullptr)) return false;
    std::vector<std::string> args{program};
    for (const std::string& flag : targetFlags) args.push_back(flag);
    args.push_back("-std=c++20");
    args.push_back(temp.string());
    args.push_back("-o");
    args.push_back(exe.string());
    std::string log;
    const int rc = runProcessCapture(args, log);
    fs::remove(temp, ec);
    fs::remove(exe, ec);
    return rc == 0;
}

// Flag sets are probed once per driver and remembered: the probe and every
// later build must agree, and re-probing on each call would be slow.
std::map<std::string, std::vector<std::string>>& flagCache() {
    static std::map<std::string, std::vector<std::string>> cache;
    return cache;
}

}  // namespace

std::vector<std::string> toolchainTargetFlags(bool clang, bool msvc,
                                               const std::string& program) {
    if (!clang || msvc || program.empty()) return {};
#if !defined(_WIN32)
    // POSIX drivers find their own standard library; nothing to rewrite.
    return {};
#else
    auto& cache = flagCache();
    const auto it = cache.find(program);
    if (it != cache.end()) return it->second;

    std::vector<std::string> flags;
    const std::string gnu = gnuTripleFor(defaultTriple(program));
    if (!gnu.empty()) {
        // Prefer the plain GNU target: Clang finds the GCC installation on
        // PATH by itself, which is also how cp3c found the sibling g++.
        const std::vector<std::string> gnuFlags{"--target=" + gnu};
        if (compilesWith(program, gnuFlags)) {
            flags = gnuFlags;
        } else {
            // Fall back to naming the GCC installation explicitly.
            const std::string prefix = gccToolchainPrefix();
            if (!prefix.empty()) {
                const std::vector<std::string> explicitFlags{
                    "--target=" + gnu, "--gcc-toolchain=" + prefix};
                if (compilesWith(program, explicitFlags)) {
                    flags = explicitFlags;
                }
            }
        }
    }
    cache[program] = flags;
    return flags;
#endif
}

}  // namespace cp3::cli
