// =============================================================================
// src/cli/src/pkg/platform.cpp - platform identity DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/platform.hpp.
//
// The normalization is explicit on purpose: an id that reaches the archive
// layout, a cache manifest or an error message is always the canonical
// spelling, never whatever the author typed.
// =============================================================================
#include "detail/archive/platform.hpp"

#include <algorithm>
#include <cctype>

namespace cp3::cli {

namespace {

// The OS families a platform id may name.
constexpr const char* kFamilies[] = {"windows", "linux", "mac"};

// The architectures an arch-qualified platform id may name.
constexpr const char* kArches[] = {"x64", "arm64", "x86", "arm32"};

std::string lowerTrimmed(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    std::string out;
    out.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(text[i]))));
    }
    return out;
}

// The family an id names, or "" for an unknown one.
std::string normalizeFamily(const std::string& text) {
    if (text == "windows" || text == "win" || text == "win32" ||
        text == "win64" || text == "winnt" || text == "mingw") {
        return "windows";
    }
    if (text == "linux") return "linux";
    if (text == "mac" || text == "macos" || text == "macosx" || text == "osx" ||
        text == "darwin" || text == "apple") {
        return "mac";
    }
    return {};
}

// The architecture an id names, or "" for an unknown one.
std::string normalizeArch(const std::string& text) {
    if (text == "x64" || text == "x86_64" || text == "x86-64" ||
        text == "amd64" || text == "x8664") {
        return "x64";
    }
    if (text == "arm64" || text == "aarch64" || text == "arm64ec") {
        return "arm64";
    }
    if (text == "x86" || text == "i386" || text == "i486" || text == "i586" ||
        text == "i686" || text == "ia32" || text == "x32") {
        return "x86";
    }
    if (text == "arm32" || text == "arm" || text == "armv6" || text == "armv7" ||
        text == "armv7a" || text == "armv7l" || text == "armhf") {
        return "arm32";
    }
    return {};
}

// The family of the machine this build runs on.
const char* hostFamily() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "mac";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

// The architecture of the machine this build runs on.
const char* hostArch() {
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
    return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__) || defined(__i686__)
    return "x86";
#elif defined(_M_ARM) || defined(__arm__)
    return "arm32";
#else
    return "unknown";
#endif
}

}  // namespace

const PlatformId& currentPlatform() {
    static const PlatformId id{hostFamily(), hostArch()};
    return id;
}

std::string platformIdText(const PlatformId& id) {
    if (id.arch.empty()) return id.family;
    return id.family + "-" + id.arch;
}

bool parsePlatformId(const std::string& text, PlatformId& out, std::string& err) {
    const std::string normalized = lowerTrimmed(text);
    if (normalized.empty()) {
        err = "empty platform id";
        return false;
    }
    const std::size_t dash = normalized.find('-');
    const std::string familyText =
        dash == std::string::npos ? normalized : normalized.substr(0, dash);
    const std::string archText =
        dash == std::string::npos ? std::string() : normalized.substr(dash + 1);

    const std::string family = normalizeFamily(familyText);
    if (family.empty()) {
        err = "unknown platform '" + text +
              "' (expected 'windows', 'linux', 'mac' or an arch-qualified id "
              "such as 'windows-x64')";
        return false;
    }
    if (dash == std::string::npos) {
        out.family = family;
        out.arch.clear();
        return true;
    }
    const std::string arch = normalizeArch(archText);
    if (arch.empty()) {
        err = "unknown architecture '" + archText + "' in platform '" + text +
              "' (expected 'x64', 'arm64', 'x86' or 'arm32')";
        return false;
    }
    out.family = family;
    out.arch = arch;
    return true;
}

bool platformSatisfies(const PlatformId& claim, const PlatformId& have) {
    if (claim.family != have.family) return false;
    if (claim.arch.empty()) return true;  // a family claim covers every arch
    return claim.arch == have.arch;
}

std::vector<PlatformId> normalizePlatformIds(const std::vector<std::string>& ids,
                                              std::string& err) {
    std::vector<PlatformId> out;
    for (const std::string& text : ids) {
        PlatformId id;
        if (!parsePlatformId(text, id, err)) {
            out.clear();
            return out;
        }
        const bool duplicate = std::any_of(
            out.begin(), out.end(), [&](const PlatformId& known) {
                return known.family == id.family && known.arch == id.arch;
            });
        if (!duplicate) out.push_back(id);
    }
    return out;
}

}  // namespace cp3::cli
