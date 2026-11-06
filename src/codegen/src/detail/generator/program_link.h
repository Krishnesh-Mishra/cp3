// =============================================================================
// src/codegen/src/detail/program_link.h - PRIVATE local-module link table.
//
// The whole-program counterpart of the loader's export surfaces
// (src/modules/src/support/export_surface.cpp): for every unit it resolves
// exported names to the qualified C++ spelling the unit's namespace carries
// (`cp3_mod_<unit>::<name>`), and every unit's local imports to those
// spellings. `emit/program.cpp` consumes the table to assemble one translation
// unit; `Generator::emitProgramUnit` copies the per-unit import map into that
// unit's generator, where an imported name is looked up before anything else.
//
// Only `ModuleResolution::Local` imports are linked: std/package/native/cpp
// specifiers keep their current lowering (a lowered intrinsic, or a use-site
// error when no lowering exists). A local form this milestone cannot lower to
// a C++ name (a namespace import, a namespace re-export, an anonymous default
// export, a type-only export) gets an empty `cpp` plus a `miss` reason: the
// name itself stays quiet until it is actually used, then reports an
// unresolved-import error at the use site.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/codegen_program.hpp"
#include "cp3/meta/module.hpp"

namespace cp3::detail {

// One name of a unit's importable surface: the C++ spelling an importer emits,
// plus the facts a call site needs (the resolved function declaration so
// optional/default arguments lower, the async flag, the value type when sema
// knows one). An empty `cpp` marks a name that has no C++ lowering (`miss`
// explains why and is reported if the name is used).
struct LinkedName {
    std::string cpp;                          // "cp3_mod_mathx::triple"
    const FunctionDeclaration* fn = nullptr;  // non-null when it is a function
    bool isAsync = false;
    // Whether the target declares type parameters (`export function f<T>`),
    // as seen by the target unit's sema. The call site needs this to accept
    // an explicit type-argument list; the AST declaration alone is not
    // always the object sema resolved the import through.
    bool isGeneric = false;
    Type type;
    std::string miss;
};

// One unit's export surface plus its namespace.
struct UnitSurface {
    std::string namesp;
    std::unordered_map<std::string, LinkedName> named;
    bool hasDefault = false;
    LinkedName defaultExport;
};

// The whole program's linkage, keyed by unit id.
struct ProgramLink {
    std::unordered_map<std::uint32_t, UnitSurface> surfaces;
    // unit id -> (imported local name -> resolution, misses included)
    std::unordered_map<std::uint32_t,
                       std::unordered_map<std::string, LinkedName>> imports;
    // Each linked unit's sema (the generation input `emit/program.cpp` hands
    // to that unit's own generator; nodeIds are per unit, D6).
    std::unordered_map<std::uint32_t, const SemaResult*> semas;
};

// Builds the link table for `graph`: every unit listed in `units` gets a
// surface (recursing through `export * from` / `export { x } from` chains,
// cycles yielding partial surfaces) and every local import of every unit is
// resolved against the surface of the unit it points to.
ProgramLink buildProgramLink(const ModuleGraph& graph,
                             const std::vector<CheckedModuleUnit>& units);

}  // namespace cp3::detail
