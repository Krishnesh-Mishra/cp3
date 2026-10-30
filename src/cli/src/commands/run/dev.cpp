// =============================================================================
// src/cli/src/commands/dev.cpp - `cp3c dev` (watch + build + restart).
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdDev.
//
// Polls the entry file's mtime (portable; no inotify requirement), builds and
// runs its native output, then stops and restarts that child after an edit.
// `--once` performs one build/run pass and exits when the program does.
// =============================================================================
#include <chrono>
#include <csignal>
#include <cstdio>
#include <system_error>
#include <thread>
#include <vector>

#include "detail/cache/cache.hpp"
#include "detail/core/commands.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {
namespace {

volatile std::sig_atomic_t g_devStopRequested = 0;

void requestDevStop(int) { g_devStopRequested = 1; }

class ScopedDevSignalHandler {
public:
    ScopedDevSignalHandler() : previous_(std::signal(SIGINT, requestDevStop)) {}
    ~ScopedDevSignalHandler() { std::signal(SIGINT, previous_); }

    ScopedDevSignalHandler(const ScopedDevSignalHandler&) = delete;
    ScopedDevSignalHandler& operator=(const ScopedDevSignalHandler&) = delete;

private:
    void (*previous_)(int);
};

}  // namespace

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
    if (opt.once) {
        const int rc = cmdBuildOrRun(opt, true);
        std::fprintf(stderr, "cp3c: build %s (exit %d)\n",
                     rc == kExitSuccess ? "ok" : "reported problems", rc);
        return rc;
    }

    g_devStopRequested = 0;
    const ScopedDevSignalHandler signalHandler;
    const fs::path exe = makeBuildLayout(file).exePath;
    for (;;) {
        const int rc = cmdBuildOrRun(opt, false);
        std::fprintf(stderr, "cp3c: build %s (exit %d)\n",
                     rc == kExitSuccess ? "ok" : "reported problems", rc);
        if (g_devStopRequested) {
            std::fprintf(stderr, "cp3c: stopped\n");
            return kExitSuccess;
        }
        ChildProcess child;
        if (rc == kExitSuccess) {
            std::vector<std::string> args{exe.string()};
            args.insert(args.end(), opt.programArgs.begin(),
                        opt.programArgs.end());
            if (startChildProcess(args, child)) {
                std::fprintf(stderr, "cp3c: running %s\n", exe.string().c_str());
                std::fflush(stderr);
            } else {
                std::fprintf(stderr, "cp3c: could not start %s\n",
                             exe.string().c_str());
            }
        }

        // Keep the watcher alive whether the app is a long-running server or
        // has exited. An edit stops a live child, rebuilds, then starts it again.
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (g_devStopRequested) {
                stopChildProcess(child);
                std::fprintf(stderr, "cp3c: stopped\n");
                return kExitSuccess;
            }
            const fs::file_time_type now = mtime();
            if (now != last) {
                last = now;
                stopChildProcess(child);
                std::fprintf(stderr, "cp3c: source changed; rebuilding...\n");
                break;
            }
            if (child.native != -1) {
                int exitCode = 0;
                const ChildProcessState state = pollChildProcess(child, exitCode);
                if (state == ChildProcessState::Exited) {
                    std::fprintf(stderr,
                                 "cp3c: program exited (exit %d); waiting for changes\n",
                                 exitCode);
                } else if (state == ChildProcessState::Failed) {
                    std::fprintf(stderr,
                                 "cp3c: could not monitor the running program; "
                                 "waiting for changes\n");
                }
            }
        }
    }
}

}  // namespace cp3::cli
