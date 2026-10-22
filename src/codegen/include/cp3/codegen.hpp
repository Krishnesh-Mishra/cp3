// =============================================================================
// cp3/codegen.hpp - checked AST -> generated C++20 (+ source map).
//
// OWNERSHIP: codegen module. The current implementation is a STUB: generate()
// returns empty strings. Replace the body; the public shape is the frozen
// boundary (extended and frozen by the contracts pass - DECISIONS D14).
//
// One .cp3 file -> exactly one .hpp/.cpp pair (docs/cli-tooling/01-...).
//
// Codegen and sema communicate ONLY through the frozen AST plus the `SemaResult`
// side tables (DECISIONS D1): codegen looks up `sema.typeOf(node.nodeId)` to
// obtain an expression's inferred type and uses `sema.symbols`/`sema.functions`
// for resolved bindings and signatures.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/native_bindings.hpp"
#include "cp3/options.hpp"

namespace cp3 {

// The generated C++20 for one compilation unit.
struct GeneratedModule {
    std::string header;  // contents of the generated .hpp
    std::string source;  // contents of the generated .cpp
};

// One `new Worker(...)` site the entry unit lowers (spec 13, D80). The CLI
// resolves each static `.cp3` path to a worker unit, generates that unit's
// TU, and hands codegen the site: the entry names the generated header and
// spells the channel's direction types. A site's `nodeId` is the `NewExpr` in
// the entry unit; codegen matches it during its own walk, so the entry's
// expression lowering never re-resolves a path. Inline sites (`new Worker(
// (thread: Thread<In, Out>) => ...)`) are synthesized by codegen itself into
// the entry TU and never carry a header path.
struct WorkerSite {
    std::uint32_t nodeId = 0;  // the NewExpr's node id in the entry unit
    std::string moduleName;    // the worker unit's logical name
    std::string headerPath;    // its generated .hpp, as written in the include
    Type in;                   // main -> worker (the Thread<In, ...> type)
    Type out;                  // worker -> main (the Thread<..., Out> type)
    bool inlineSite = false;   // the inline arrow form (defined in this unit)
};

// Per-unit codegen options outside the AST/sema side tables. All-default for
// every ordinary unit; the CLI fills `workers` for an entry unit that starts
// workers and sets `workerModule` while generating a worker unit itself.
struct CodegenOptions {
    bool workerModule = false;
    std::vector<WorkerSite> workers;
    // Top-level enums declared in the unit's local imports. Imports are
    // compile-time, and an enum can travel as a worker payload; each unit
    // re-emits the names it imported as its own `enum class`, and same-named
    // enums are structurally compatible at the channel boundary (D80).
    std::vector<const EnumDeclaration*> importedEnums;
    // The project's plain type-name vocabulary (spec 03). Inline r_/c_/ts_
    // prefixes work in every project; this only affects bare `i64`-style
    // annotations in a rust/cpp project.
    TypeSystem typeSystem = TypeSystem::TypeScript;
    // The native bindings this unit imports (`import native "id" as alias`).
    // The module resolver loaded them; codegen lowers `alias.f(...)` against
    // the recorded C++ names and includes the binding's headers.
    std::vector<NativeBinding> nativeBindings;
};

// The typed channel of a worker unit's entry (D80): the `thread: Thread<In, Out>`
// annotation on its default-export arrow is the single source of truth.
struct WorkerChannel {
    Type in;   // main -> worker
    Type out;  // worker -> main
};

// Resolves `program`'s default-export arrow's `Thread<In, Out>` annotation via
// the sema type-node side table. Returns nullopt when the unit has no
// default-export arrow or its entry lacks a usable annotation (codegen reports
// CP3905 when it lowers such a unit). Used by the CLI, which needs the same
// channel types the worker TU's `cp3_default` signature spells.
std::optional<WorkerChannel> resolveWorkerChannel(const Program& program,
                                                  const SemaResult& sema);

class CodeGenerator {
public:
    // `lineDirectives` emits `#line <n> "<source>"` ahead of generated
    // declarations and statements, so DWARF maps executable code back to the
    // .cp3 source (Debug profile / F5 debugging). Off by default: the
    // incremental and release workflows stay byte-identical.
    explicit CodeGenerator(DiagnosticSink& sink, bool lineDirectives = false) noexcept
        : sink_(sink), lineDirectives_(lineDirectives) {}

    // Emits C++20 for `program`, consulting `sema` for inferred types and
    // resolved bindings. `moduleName` is the logical/relative module name used
    // to derive the output file pair and any include guards.
    // Never returns a partially-written module: on internal failure the
    // generator reports a diagnostic and returns empty strings.
    GeneratedModule generate(const Program& program,
                             const SemaResult& sema,
                             const std::string& moduleName);

    // `options` extends the boundary with the concurrency lowering's per-unit
    // inputs (spec 13); the 3-argument overload delegates with the defaults.
    GeneratedModule generate(const Program& program,
                             const SemaResult& sema,
                             const std::string& moduleName,
                             const CodegenOptions& options);

    // Convenience: generate against an empty SemaResult (types stay unknown).
    // Intended for probes/tests, not for the real pipeline.
    GeneratedModule generate(const Program& program, const std::string& moduleName);

private:
    DiagnosticSink& sink_;
    bool lineDirectives_ = false;  // emit `#line` source mapping (Debug)
};

}  // namespace cp3
