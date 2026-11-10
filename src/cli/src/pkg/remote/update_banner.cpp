// =============================================================================
// src/pkg/remote/update_banner.cpp - the per-command update reminder.
// =============================================================================
#include "detail/pkg/update_banner.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "cp3/meta/version.hpp"

#include "detail/core/util.hpp"
#include "detail/pkg/update_check.hpp"

namespace cp3::cli {

void maybeEmitUpdateReminder(const std::string& command) {
    // Long-lived protocol servers must never speak "upgrade available" into
    // their streams, and the reminder is noise at the end of one-liners.
    if (command == "lsp" || command == "dap" || command == "docs") return;
    if (std::getenv("CP3_NO_UPDATE_CHECK") != nullptr &&
        std::getenv("CP3_NO_UPDATE_CHECK")[0] != '\0') {
        return;
    }

    UpdateCheckState state = readUpdateCheck();
    if (!state.present || state.stale) {
        // At most one silent refresh per 24 h. Failure is invisible by design.
        UpdateCheckState refreshed;
        std::string ignored;
        if (refreshUpdateCheck(/*silent=*/true, refreshed, ignored)) {
            state = refreshed;
        } else {
            return;  // no cache, no network, no noise
        }
    }
    if (!state.present) return;
    if (!isNewerVersion(state.latestTag, kVersion)) return;  // latest: silent
    std::fprintf(stderr, "cp3: %s -> %s available; run `cp3 upgrade`\n",
                 kVersion, state.latestTag.c_str());
}

}  // namespace cp3::cli
