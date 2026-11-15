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
// forms report a codegen diagnostic exactly like the single-unit path (never
// throw, never abort).
GeneratedModule generateProgram(const GenerateProgramInput& input);

// One checked unit emitted as its own translation unit (plural pipeline).
// `name` is the deterministic file stem used for the .cpp and the object
// file; `sourceKey` is the logical identity of the unit for the build
// manifest (the unit's module name). The TU is `#include`d of the shared
// header and holds the unit's definitions and its module initializer.
struct GeneratedProgramUnit {
    std::string name;       // e.g. "cp3_unit_3" - unique per emission index
    std::string sourceKey;  // the unit's module name (manifest identity)
    std::string source;     // the unit's .cpp text
    // The unit's C++ namespace ("cp3_mod_counter"). Additive; older
    // callers that never read this member behave exactly as before.
    std::string namesp;
};

// The plural result. `sharedHeader` is the .hpp every unit TU and the entry
// TU include (includes, helpers, shared globals, all units' declaration
// sections and the module-initializer forward declarations). `units` holds
// one TU per checked unit in emission order; the entry unit's top-level
// statements are not a TU of their own - they run in `entry`'s main().
// `entry` is the small entry TU (using namespace <entry-ns>; main body).
struct GeneratedProgramModules {
    GeneratedModule sharedHeader;   // .source = the shared .hpp text
    std::vector<GeneratedProgramUnit> units;
    GeneratedModule entry;          // .source = the entry TU text
};

// Lowers a local-module program to one shared header plus ONE .cpp per
// checked unit plus a small entry TU (v0.2.5 dev-profile pipeline). The
// per-unit namespaces and the link table are identical to `generateProgram`;
// cross-unit references resolve through the shared header's declaration
// sections exactly as through the single TU's phase-1 sections.
GeneratedProgramModules generateProgramModules(const GenerateProgramInput& input);

}  // namespace cp3
