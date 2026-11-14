// =============================================================================
// src/cli/src/detail/hotpatch/state_map.hpp - module-scope mutable bindings
// (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Computes, for one checked unit of a local-module program, the set of
// module-scope mutable bindings (`let`/`var`) and their canonical type text.
// The result is the input the hot-patch descriptor needs to mint a state
// block: a declaration-ordered field list whose layout hash retires every
// module whose state must reload when the set changes.
//
// const bindings with a literal/constant-foldable initializer stay in the
// lib (they are emitted as inline namespace-scope globals there) so they
// never enter the state block. A let/var binding with a non-literal
// initializer, including every destructured let/var binding, enters it.
// Destructured bindings are admitted with `unknown` types and flag the unit
// as needing a rebuild on any binding change, which is the correct,
// conservative swap verdict.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/meta/module.hpp"

namespace cp3::cli::hotpatch {

// One module-scope mutable binding as the descriptor needs to see it.
struct StateFieldEntry {
    std::string name;      // binding name, as written (e.g. "counter")
    std::string typeText;  // canonical print of the binding's type
};

struct ModuleStateLayout {
    std::vector<StateFieldEntry> fields;
    std::string layoutHash;             // hex fnv1a64 of the field list
    std::uint64_t stateSize = 0;        // reserved; 0 = unknown until emit time
    std::uint64_t stateAlign = 8;
    bool hasDestructuredModuleBindings = false;
    bool isSwappable = true;            // false when destructured state is present
};

// Walks the unit's top-level `let`/`var` declarations (and their exported
// companions), computes the canonical field list in declaration order and a
// layout hash over it.
ModuleStateLayout computeModuleStateLayout(const CompilationUnit& unit,
                                           const SemaResult& sema);

}  // namespace cp3::cli::hotpatch
