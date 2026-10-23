// =============================================================================
// cp3/codegen_program.hpp - whole-program codegen (local module linkage).
//
// OWNERSHIP: codegen module (src/codegen/**).
//
// The single-unit boundary stays in `cp3/codegen.hpp` (frozen). This header adds
// the multi-unit entry: one program whose units import each other with local
// specifiers (docs/language-spec/18-modules-imports.md). Every unit lowers into
// its own `cp3_mod_<name>` C++ namespace inside ONE generated translation unit;
// an import resolves to the target unit's qualified C++ name, so module-scope
// bindings keep live-binding semantics. Module initializers run in
// `ModuleGraph::compilationOrder()` (dependencies first, ES-module semantics),
// then the entry unit's top-level statements in `main`.
//
// A program without a resolved local import keeps the single-unit path
// (`CodeGenerator::generate`), so its output is byte-identical.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"

namespace cp3 {

// One checked unit of the program: the unit's parsed AST plus the SemaResult it
// was checked with. Each unit's parser restarts nodeIds at 1 (D6), so the
// side tables are per unit and cannot be merged.
struct CheckedModuleUnit {
    const CompilationUnit* unit = nullptr;  // must carry a parsed `ast`
    const SemaResult* sema = nullptr;
};

// The inputs of one `generateProgram` call. `graph->compilationOrder()` supplies
// the emission order and `units` the checked AST of every emittable unit; a unit
// without an entry is skipped (a prelude, or a unit the caller did not check).
// `options` carries the entry unit's worker import (spec 13); imported units
// always generate with the defaults.
struct GenerateProgramInput {
    const ModuleGraph& graph;
    const std::vector<CheckedModuleUnit>& units;
    const std::string& moduleName;
    DiagnosticSink& sink;
    bool lineDirectives = false;
    CodegenOptions options;
};

// Lowers a local-module program to one C++20 translation unit. Out-of-scope
// forms report CP3002 exactly like the single-unit path (never throw, never
// abort).
GeneratedModule generateProgram(const GenerateProgramInput& input);

}  // namespace cp3
