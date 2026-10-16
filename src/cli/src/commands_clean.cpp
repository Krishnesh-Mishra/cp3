// =============================================================================
// cli/commands_clean.cpp — `cp3c clean [--all]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdClean.
//
// Removes the local .cp3/build cache; --all also removes .cp3/packages.
// =============================================================================
#include <cstdio>
#include <system_error>

#include "private/commands.hpp"
#include "private/util.hpp"

namespace cp3::cli {

int cmdClean(const CliOptions& opt) {
    std::error_code ec;
    const fs::path root = fs::current_path(ec);
    const fs::path buildDir = root / ".cp3" / "build";

    if (fs::exists(buildDir, ec)) {
        removeTree(buildDir);
        std::fprintf(stderr, "cp3c: removed %s\n", buildDir.string().c_str());
    } else if (opt.verbose) {
        std::fprintf(stderr, "cp3c: nothing to remove (%s)\n",
                     buildDir.string().c_str());
    }

    if (opt.all) {
        const fs::path packages = root / ".cp3" / "packages";
        if (fs::exists(packages, ec)) {
            removeTree(packages);
            std::fprintf(stderr, "cp3c: removed %s\n",
                         packages.string().c_str());
        }
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
