// =============================================================================
// src/cli/src/commands/clean.cpp - `cp3c clean [--all]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdClean.
//
// Removes the local .cp3/build intermediate cache and the .cp3/out executable
// (D61); --all also removes .cp3/packages.
// =============================================================================
#include <cstdio>
#include <system_error>

#include "detail/commands.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

int cmdClean(const CliOptions& opt) {
    std::error_code ec;
    const fs::path root = fs::current_path(ec);
    const fs::path dirs[] = {root / ".cp3" / "build", root / ".cp3" / "out"};

    for (const fs::path& dir : dirs) {
        if (fs::exists(dir, ec)) {
            removeTree(dir);
            std::fprintf(stderr, "cp3c: removed %s\n", dir.string().c_str());
        } else if (opt.verbose) {
            std::fprintf(stderr, "cp3c: nothing to remove (%s)\n",
                         dir.string().c_str());
        }
    }

    if (opt.all) {
        const fs::path packages = root / ".cp3" / "packages";
        if (fs::exists(packages, ec)) {
            removeTree(packages);
            std::fprintf(stderr, "cp3c: removed %s\n",
                         packages.string().c_str());
        }
        // The lock describes installed packages; wiping the packages without
        // the lock would leave a record of state that no longer exists.
        const fs::path lock = root / ".cp3" / "lock.json";
        if (fs::exists(lock, ec)) {
            std::error_code rmEc;
            fs::remove(lock, rmEc);
            if (!rmEc) {
                std::fprintf(stderr, "cp3c: removed %s\n", lock.string().c_str());
            }
        }
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
