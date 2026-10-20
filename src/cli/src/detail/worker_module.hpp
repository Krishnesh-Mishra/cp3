// =============================================================================
// src/cli/src/detail/worker_module.hpp - the worker-file linkage (D80, spec 13).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header. Definitions live in
// src/toolchain/worker_module.cpp (STYLE.md §3/§8: headers declare, `.cpp`
// defines).
//
// `new Worker("./x.cp3")` names a compile-time unit: the CLI resolves the
// literal against the entry file's folder, loads that unit in its own module
// graph, runs sema on it, reads its `Thread<In, Out>` annotation (the single
// source of truth for the channel), and generates its TU. The entry's codegen
// then only needs `CodegenOptions::workers` - the per-site channel types and
// the generated header to include. The inline form needs none of this: it is
// synthesized by codegen from the entry unit itself.
//
// The resolver never guesses: a path that is not a static literal is reported
// by codegen (CP3910), a missing file here (CP3101), and a worker unit that
// does not analyze or lower cleanly lands in `diagnostics` with `failed` set.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/module.hpp"
#include "cp3/source.hpp"

namespace cp3::cli {

// One `new Worker("<literal>")` site of the entry unit.
struct WorkerPathSite {
    std::uint32_t nodeId = 0;
    std::string literal;
    SourceSpan span{};
};

// Every file-form site in `program`, including sites nested in functions and
// arrows. A user declaration named `Worker` disables the builtin (the form is
// then an ordinary constructor call and codegen reports it).
std::vector<WorkerPathSite> collectWorkerPathSites(const Program& program);

// The resolved worker units of one entry compile: `sites` feed
// `CodegenOptions::workers`; `modules[i]` is the generated TU for
// `moduleNames[i]`, keyed incrementally by `sourceKeys[i]`.
struct ResolvedWorkers {
    std::vector<WorkerSite> sites;
    std::vector<std::string> moduleNames;
    std::vector<std::string> sourceKeys;
    std::vector<GeneratedModule> modules;
    std::vector<Diagnostic> diagnostics;
    bool failed = false;
};

// Resolves each site's literal against `entryFolder`, dedupes by canonical
// source path (two sites may drive the same file), and generates each worker
// TU. Worker-unit diagnostics are collected here; the caller renders them and
// stops on `failed`.
ResolvedWorkers resolveWorkerUnits(const std::vector<WorkerPathSite>& sites,
                                   const std::filesystem::path& entryFolder,
                                   const std::string& entryModuleName,
                                   SourceManager& sm, bool lineDirectives);

// Every top-level enum declared in a unit of `graph` other than `unit` (a
// local import, including one reached through another import). The CLI hands
// these to codegen as `CodegenOptions::importedEnums`: imports are compile-time,
// so each unit re-emits the enum names it can see, and a worker channel can
// carry a shared enum as a payload (D80).
std::vector<const EnumDeclaration*> importedEnumsFor(const ModuleGraph& graph,
                                                     const CompilationUnit& unit);

}  // namespace cp3::cli
