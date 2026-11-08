// =============================================================================
// src/cli/src/toolchain/toolchain_available.cpp - package manager versions.
//
// OWNERSHIP: cli module (src/cli/**). Implements the available-version half of
// detail/toolchain_select.hpp.
//
// These queries are read-only. cp3c never downloads or extracts a compiler;
// the wizard delegates installation to the same system package manager whose
// Clang version list is shown here.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string trimCopy(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void addAvailable(std::vector<AvailableToolchainVersion>& versions,
                  const std::string& packageVersion) {
    const std::string display = normalizeCompilerVersion(packageVersion);
    if (display.empty()) return;
    for (const AvailableToolchainVersion& existing : versions) {
        if (compareCompilerVersions(existing.version, display) == 0) return;
    }
    versions.push_back({display, packageVersion});
}

void sortAvailable(std::vector<AvailableToolchainVersion>& versions) {
    std::sort(versions.begin(), versions.end(),
              [](const AvailableToolchainVersion& a,
                 const AvailableToolchainVersion& b) {
                  return compareCompilerVersions(a.version, b.version) > 0;
              });
}

#if defined(_WIN32)
std::vector<AvailableToolchainVersion> wingetVersions(const char* id) {
    std::vector<AvailableToolchainVersion> versions;
    std::string out;
    if (runProcessCapture({"winget", "show", "--id", id, "--versions"}, out) <
        0) {
        return versions;
    }
    std::size_t start = 0;
    while (start <= out.size()) {
        const std::size_t end = out.find('\n', start);
        const std::string line = trimCopy(out.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start));
        if (!line.empty() && line[0] >= '0' && line[0] <= '9') {
            const std::size_t space = line.find_first_of(" \t");
            addAvailable(versions, line.substr(0, space));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    sortAvailable(versions);
    return versions;
    sortAvailable(versions);
    return versions;
}
#endif  // _WIN32

#if !defined(_WIN32) && !defined(__APPLE__)
std::vector<AvailableToolchainVersion> aptVersions(const char* package) {
    std::vector<AvailableToolchainVersion> versions;
    std::string out;
    if (runProcessCapture({"apt-cache", "madison", package}, out) < 0) {
        return versions;
    }
    std::size_t start = 0;
    while (start <= out.size()) {
        const std::size_t end = out.find('\n', start);
        const std::string line = out.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start);
        const std::size_t first = line.find('|');
        const std::size_t second =
            first == std::string::npos ? std::string::npos
                                       : line.find('|', first + 1);
        if (first != std::string::npos && second != std::string::npos) {
            addAvailable(versions,
                         trimCopy(line.substr(first + 1, second - first - 1)));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    sortAvailable(versions);
    return versions;
}

std::vector<AvailableToolchainVersion> dnfVersions(const char* package) {
    std::vector<AvailableToolchainVersion> versions;
    std::string out;
    if (runProcessCapture({"dnf", "--showduplicates", "list", package}, out) <
        0) {
        return versions;
    }
    std::size_t start = 0;
    while (start <= out.size()) {
        const std::size_t end = out.find('\n', start);
        const std::string line = trimCopy(out.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start));
        if (line.find('.') != std::string::npos &&
            line.find(' ') != std::string::npos) {
            const std::size_t space = line.find(' ');
            addAvailable(versions, trimCopy(line.substr(space + 1)));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    sortAvailable(versions);
    return versions;
}
#endif

#if defined(__APPLE__)
std::vector<AvailableToolchainVersion> brewVersions(const char* formula) {
    std::vector<AvailableToolchainVersion> versions;
    std::string out;
    if (runProcessCapture({"brew", "info", formula}, out) < 0) return versions;
    const std::size_t stable = out.find("stable");
    if (stable != std::string::npos) {
        const std::string version =
            normalizeCompilerVersion(out.substr(stable));
        if (!version.empty()) versions.push_back({version, version});
    }
    return versions;
}
#endif

}  // namespace

std::vector<AvailableToolchainVersion> availableToolchainVersions(
    const ToolchainCandidate& candidate) {
    if (candidate.id != "clang") return {};
#if defined(_WIN32)
    return wingetVersions("LLVM.LLVM");
#elif defined(__APPLE__)
    return brewVersions("llvm");
#else
    std::vector<AvailableToolchainVersion> versions = aptVersions("clang");
    if (versions.empty()) versions = dnfVersions("clang");
    return versions;
#endif
}

}  // namespace cp3::cli
