// =============================================================================
// src/cli/src/commands/common.cpp - helpers shared by the command implementations.
//
// OWNERSHIP: cli module (src/cli/**). Implements part of detail/commands.hpp:
// render-option construction and entry-source loading.
// =============================================================================
#include "detail/core/commands.hpp"

#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "detail/core/util.hpp"

namespace cp3::cli {

RenderOptions makeRenderOptions(const CliOptions& opt, std::string fallbackPath) {
    RenderOptions ro;
    ro.pretty = opt.verbose || isTty(stderr);
    ro.color = ro.pretty && isTty(stderr) && std::getenv("NO_COLOR") == nullptr;
    ro.fallbackPath = std::move(fallbackPath);
    return ro;
}

void reportRunProgress(bool execute, bool verbose, const char* phase) {
    if (!execute || verbose || !isTty(stderr)) return;
    std::fprintf(stderr, "cp3c: %s\n", phase);
    std::fflush(stderr);
}

bool loadEntrySource(const std::string& path, SourceManager& sm, SourceFile*& out,
                     std::string& err) {
    if (path.empty()) {
        err = "no input file (expected a .cp3 path)";
        return false;
    }
    std::error_code ec;
    const fs::path p(path);
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
        err = "cannot open '" + path + "'";
        return false;
    }
    std::string text;
    if (!readTextFile(p, text)) {
        err = "cannot read '" + path + "'";
        return false;
    }
    out = sm.addFile(path, std::move(text));
    return out != nullptr;
}

}  // namespace cp3::cli
