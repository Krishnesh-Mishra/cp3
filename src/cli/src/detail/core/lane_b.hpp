// =============================================================================
// src/cli/src/detail/core/lane_b.hpp - the two-lane build pipeline
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// cmdBuildOrRun() starts Lane B at command entry: everything the dev build
// needs that is independent of the user's program text - toolchain
// resolution (cached), the dev PCH (depends only on the runtime/std
// headers) and the clang driver image - runs in parallel with Lane A
// (frontend + codegen). Lane B writes only the idempotent PCH/prefix/stamp
// files in the project's .cp3/build directory; any failure degrades to the
// ordinary serial path, which redoes everything. The join for Lane B
// happens right before the per-TU stage needs the PCH.
//
// Lane B assumes no native-backend compile flags (its PCH is stamped with a
// fingerprint of the empty flag set). Programs whose native backends add
// -I/-D flags rebuild the PCH once on the serial path, which is correct and
// bounded; console-only programs (the hot dev loop) hit the stamp and
// reuse the Lane-B PCH. The runtime TU objects are already cached in
// .cp3/cache/_runtime and are not Lane B's concern.
// =============================================================================
#pragma once

#include <string>
#include <thread>

#include "detail/core/commands.hpp"

namespace cp3::cli::detail {

// Starts Lane B on a background thread. Best effort; never throws.
std::thread laneBStart(const CliOptions& opt, const std::string& entry);

// Joins the Lane B thread (returns immediately when it already finished).
// Called before the per-TU stage.
void laneBJoin(std::thread& lane);

}  // namespace cp3::cli::detail
