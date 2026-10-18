// =============================================================================
// cli/private/semver.hpp - semver parsing + npm-style range matching.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/semver.cpp. Used by `cp3 install` to check
// a package's `engines.cp3` range against the running compiler version
// (CLI-README §5.1) - installed-but-unsatisfied engines warn, never fail.
// =============================================================================
#pragma once

#include <string>
#include <string_view>

namespace cp3::cli {

struct SemVer {
    long major = 0;
    long minor = 0;
    long patch = 0;
    std::string prerelease;  // dot-separated identifiers; empty for releases
    bool valid = false;
};

// Parses "1.2.3", "1.2.3-beta.1" (a leading "v" and build metadata are
// tolerated). `valid` is false when the core does not parse.
SemVer parseSemVer(std::string_view text);

// Semver precedence: -1, 0 or 1. Releases outrank their own prereleases.
int compareSemVer(const SemVer& a, const SemVer& b);

// npm-style range match: comparators with ^ ~ = > >= < <=, partial versions
// ("1", "1.2", "1.x"), whitespace-separated intersections, `||` unions and
// hyphen ranges. Fail-open: a range with a construct this matcher does not
// understand returns true, because the only caller is a warning.
bool semverSatisfies(const SemVer& version, std::string_view range);

}  // namespace cp3::cli
