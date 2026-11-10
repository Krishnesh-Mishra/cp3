// =============================================================================
// src/cli/src/detail/pkg/update_banner.hpp - the per-command update reminder.
//
// OWNERSHIP: cli module (src/cli/**). DECLARATIONS live here; DEFINITIONS in
// src/pkg/remote/update_banner.cpp.
//
// `maybeEmitUpdateReminder` is called once at the end of every `cp3` command.
// It only prints when the cached check (at most one network fetch per 24 h)
// says a newer release exists. Any failure is silent by design.
// =============================================================================
#pragma once

#include <string>

namespace cp3::cli {

// Prints "cp3: <current> -> <latest> available; run `cp3 upgrade`" to stderr
// when the cached self-update check finds a newer release. `command` is the
// command word the user ran ("upgrade", "install", ...); server commands
// (lsp/dap/docs) never print. Cheap on the hot path: one file read and, at
// most, one 24 h-cadenced silent network refresh.
void maybeEmitUpdateReminder(const std::string& command);

}  // namespace cp3::cli
