// =============================================================================
// src/cli/src/detail/worker_module.hpp - the `worker(...)` module linkage (spec 13).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header. Definitions live in
// src/toolchain/worker_module.cpp (STYLE.md §3/§8: headers declare, `.cpp`
// defines).
//
// A program drives a worker module through the canonical import
//
//     import worker from "<local>.cp3";
//
// plus `worker(...)` calls in the entry unit. The entry's codegen lowers those
// calls onto the worker module's `cp3_default`, so the worker unit needs its own
// generated TU compiled into the program. This header owns that resolution:
// find the canonical import, derive the worker's module name and the include
// path of its generated header, and lower the worker unit by itself.
//
// Anything outside the canonical shape resolves to nullopt, which leaves the
// entry's `worker(...)` a CP3002 skip (exit 3): never a wrong call.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/module.hpp"

namespace cp3::cli {

// A resolved canonical worker import.
struct WorkerUnit {
    const CompilationUnit* unit = nullptr;  // the worker module's unit
    std::string moduleName;                 // specifier stem, e.g. "worker_impl"
    std::string sourceKey;                  // the .cp3 path (incremental key)
    std::string headerPath;                 // generated .hpp path, as #included
};

// Resolves the canonical `import worker from "<local>.cp3"` of the entry unit
// through `graph`. Returns nullopt when the import is absent or repeated, when
// its edge is not a resolved local module, or when the unit carries no AST.
std::optional<WorkerUnit> findWorkerUnit(const Program& entry,
                                         const ModuleGraph& graph,
                                         const std::string& entryModuleName);

// Conservative scan for a `worker(...)` call anywhere in the unit. It may
// report true without a call (an unused import then pays for one small TU),
// never false when a call exists.
bool programCallsWorker(const Program& program);

// Lowers the worker unit (`export default (…) => …` -> `cp3_default`), running
// sema on the worker unit with its own `SemaResult`. An unsupported worker
// reports into `sink` and returns an all-empty module; the caller then leaves
// the entry's codegen without worker options (CP3002 skip). `compile` carries
// the project type-system settings.
GeneratedModule generateWorkerModule(const WorkerUnit& worker, bool lineDirectives,
                                     DiagnosticSink& sink,
                                     const CompileOptions& compile);

}  // namespace cp3::cli
