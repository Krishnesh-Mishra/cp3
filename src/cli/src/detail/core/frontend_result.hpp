// =============================================================================
// detail/frontend_result.hpp - the frontend output shared by the run/build
// driver and the dev watcher (RO).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// `cp3c run/build` builds this struct internally; the dev watcher only needs
// the type to receive the moved-out frontend result for the v0.2.5 Phase 4
// Slice 1 classification diagnostic (it never runs the build itself).
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "cp3/cli.hpp"
#include "cp3/core/analysis.hpp"
#include "cp3/sema.hpp"
#include "cp3/meta/module.hpp"

namespace cp3::cli {

// The load+sema half of the pipeline. exitCode is kExitSuccess when `graph`
// carries an analyzed entry unit to hand to codegen. `program` selects the
// whole-program codegen path (a resolved local import exists); in that mode
// every emittable unit is checked and its SemaResult kept in `unitSemas` -
// each unit's parser restarts nodeIds (D6), so the tables are per unit and
// cannot be merged.
struct FrontendResult {
    std::unique_ptr<ModuleGraph> graph;
    SemaResult sema;
    std::unordered_map<std::uint32_t, SemaResult> unitSemas;
    bool program = false;
    int exitCode = kExitSuccess;
};

}  // namespace cp3::cli
