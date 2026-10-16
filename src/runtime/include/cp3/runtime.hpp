// =============================================================================
// cp3/runtime.hpp — placeholder for generated-code runtime support.
//
// OWNERSHIP: runtime module. Intentionally minimal for now: Promise helpers,
// deep-copy helpers and the event-loop bridge will live behind this boundary,
// but Beta One scaffolding only needs the target to exist and link.
// =============================================================================
#pragma once

#include <string>

namespace cp3::runtime {

// Identifies the runtime support library (for diagnostics / `--version`-style
// provenance). Stable within a build.
const char* name() noexcept;

// Human-readable runtime version, independent of the compiler version.
std::string version();

}  // namespace cp3::runtime
