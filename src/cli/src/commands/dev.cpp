// =============================================================================
// src/cli/src/commands/dev.cpp - `cp3c dev` (watch + re-check).
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdDev.
//
// Polls the entry file's mtime (portable; no inotify requirement) and re-runs
// `check` on every change. `--once` performs a single pass.
// =============================================================================
#include <chrono>
#include <cstdio>
#include <system_error>
#include <thread>

#include "detail/commands.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

int cmdDev(const CliOptions& opt) {
    if (opt.file.empty()) {
        std::fprintf(stderr, "cp3c: dev needs a .cp3 file to watch\n");
        return kExitUsageError;
    }
    std::error_code ec;
    const fs::path file(opt.file);
    if (!fs::exists(file, ec)) {
        std::fprintf(stderr, "cp3c: cannot open '%s'\n", opt.file.c_str());
        return kExitUsageError;
    }

    auto mtime = [&file, &ec]() { return fs::last_write_time(file, ec); };
    std::fprintf(stderr, "cp3c: watching %s (Ctrl+C to stop)\n",
                 opt.file.c_str());

    fs::file_time_type last = mtime();
    for (;;) {
        const int rc = cmdCheck(opt);
        std::fprintf(stderr, "cp3c: check %s (exit %d)\n",
                     rc == kExitSuccess ? "ok" : "reported problems", rc);
        if (opt.once) return rc;

        // Poll for a modification (simple, portable; no inotify requirement).
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const fs::file_time_type now = mtime();
            if (now != last) {
                last = now;
                break;
            }
        }
    }
}

}  // namespace cp3::cli
