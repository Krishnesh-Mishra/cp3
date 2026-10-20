// =============================================================================
// src/codegen/src/code_generator.cpp - codegen entry + module assembly
// (STYLE.md §8).
//
// Owns the frozen `CodeGenerator::generate` boundary (cp3/codegen.hpp), the
// `Generator` constructor/diagnostic helpers, and the top-level emit walk
// (`Generator::run`). The generated module assembly (the .hpp/.cpp writer and
// the synthesized helper block + `main()`) lives in support/module_writer.cpp.
// Out-of-scope AST nodes report CP3002 (deduped per nodeId) and emit a
// compilable stub; the generator never throws, and the dedup drives the CLI's
// exit-3 "skipped" behavior.
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "cp3/codegen.hpp"
#include "cp3/diagnostic_codes.hpp"
#include "detail/cpp_writer.h"
#include "detail/helper_blocks.h"
#include "detail/mangle.h"

namespace cp3 {

using namespace detail;

// --- Generator - construction, diagnostics and top-level orchestration -------

GeneratorState::GeneratorState(DiagnosticSink& sink, const SemaResult& sema,
                               const std::string& moduleName, bool lineMapping,
                               const CodegenOptions& options)
    : sink_(sink), sema_(sema), moduleName_(moduleName),
      namesp_(suggestNamespace(moduleName)), options_(options),
      lineMapping_(lineMapping) {}

Generator::Generator(DiagnosticSink& sink, const SemaResult& sema,
                     const std::string& moduleName, bool lineDirectives,
                     const CodegenOptions& options)
    : GeneratorState(sink, sema, moduleName, lineDirectives, options) {}

void Generator::notImplemented(const Node& node, const std::string& what) {
    // Inside an int64 fast-path attempt nothing is reported and nothing is
    // deduped: any CP3002 makes the attempt unusable, and the ordinary pass
    // (which always runs) reports it once, in place.
    if (intMode_) intMode_->failed = true;
    if (suppressDiagnostics_) return;
    if (node.nodeId != 0) {
        if (!reported_.insert(node.nodeId).second) {
            return;
        }
    }
    sink_.error(diagcode::kNotImplemented, node.span,
                "not implemented yet: " + what);
}

GeneratedModule Generator::run(const Program& program) {
    // The worker unit lowers only its default-export arrow (spec 13); the
    // entry unit's ordinary walk follows.
    if (options_.workerModule) {
        return runWorkerModule(program);
    }
    std::string decls;   // function forward declarations
    std::string defs;    // function definitions
    std::string mainBody;
    std::string globals;
    runUnit(program, decls, defs, mainBody, globals);
    GeneratedModule module;
    module.header = buildHeader(decls, globals);
    module.source = buildSource(decls, defs, globals, mainBody);
    return module;
}

// One unit's emit walk, shared by the single-unit assembly above and the
// whole-program assembler (emit/program.cpp): collect the unit's declarations
// and shapes, lower its top-level statements into the three sections, then
// prepend the resolved enum/object/JSON declarations and select the family
// includes from the emitted spellings.
void Generator::runUnit(const Program& program, std::string& decls,
                        std::string& defs, std::string& mainBody,
                        std::string& globals) {
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        collectFunctionNames(*stmtPtr);
        if (stmtPtr->kind == NodeKind::ImportDeclaration) {
            const auto& imp = static_cast<const ImportDeclaration&>(*stmtPtr);
            if (imp.source == "std/console") {
                consoleImported_ = true;
            }
        }
    }

    // Usage-based reification (STDLIB docs §04): register the unit's interface
    // record shapes before the emit walk, so a later `JSON.parse<T>` can name T.
    collectJsonShapes(program);

    // The unit's enums (spec 04): resolved before the emit walk (emit/enum.cpp).
    collectEnums(program);

    // The worker sites (spec 13, D80): file sites arrive resolved from the CLI;
    // the inline form is synthesized into the same site model (worker.cpp).
    collectWorkerSites(program);

    // The closure-capture analysis (spec 10): classify every function frame's
    // locals before the emit walk, so a shared mutable binding lowers to a
    // `Cell<T>` from its first declaration on.
    captures_ = analyzeCaptures(program);
    currentFrameId_ = program.nodeId;

    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) {
            emitTopLevel(*stmtPtr, decls, defs, mainBody);
        }
    }

    // The inline worker entries (D80) are ordinary functions of this unit: one
    // `cp3_inline_worker_<n>` per inline site, emitted after the unit's own
    // declarations so their bodies can call this unit's top-level functions.
    emitInlineWorkerEntries(decls, defs);

    // Enums first (signatures and record shapes can name one), then the JSON
    // record structs, which are namespace-scope and complete before any
    // builder reads them, and the `$o{...}` value structs at global scope
    // (guarded, so a worker TU spells the same nominal type).
    decls = enumDecls_ + jsonShapeDecls_ + decls;
    globals = objectGlobalDeclsFor(decls + defs + mainBody, needsWorker_);

    // The family include (D69) follows the emitted spellings
    // (support/usage_includes.cpp).
    applyUsageIncludes(decls + defs + globals + mainBody);
}


// --- CodeGenerator (frozen boundary) ------------------------------------------

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const SemaResult& sema,
                                        const std::string& moduleName) {
    return generate(program, sema, moduleName, CodegenOptions{});
}

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const SemaResult& sema,
                                        const std::string& moduleName,
                                        const CodegenOptions& options) {
    Generator generator(sink_, sema, moduleName, lineDirectives_, options);
    return generator.run(program);
}

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const std::string& moduleName) {
    return generate(program, SemaResult{}, moduleName);
}

}  // namespace cp3
