// =============================================================================
// src/cli/src/commands/common.cpp - helpers shared by the command implementations.
//
// OWNERSHIP: cli module (src/cli/**). Implements part of detail/commands.hpp:
// the not-implemented convention, render-option construction, and entry-source
// loading.
// =============================================================================
#include "detail/core/commands.hpp"

#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"

#include "detail/core/util.hpp"

namespace cp3::cli {

int notImplemented(const std::string& command) {
    std::fprintf(stderr, "cp3c: the '%s' command is %s in this build yet (CP3002)\n",
                 command.c_str(), kNotImplementedMarker);
    return kExitNotImplemented;
}

bool onlyNotImplemented(const std::vector<Diagnostic>& diagnostics) {
    bool any = false;
    for (const Diagnostic& d : diagnostics) {
        if (d.severity == Severity::Error || d.severity == Severity::Fatal) {
            any = true;
            if (d.code != diagcode::kNotImplemented) return false;
        }
    }
    return any;
}

RenderOptions makeRenderOptions(const CliOptions& opt, std::string fallbackPath) {
    RenderOptions ro;
    ro.pretty = opt.verbose || isTty(stderr);
    ro.color = ro.pretty && isTty(stderr) && std::getenv("NO_COLOR") == nullptr;
    ro.fallbackPath = std::move(fallbackPath);
    return ro;
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
