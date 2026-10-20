// =============================================================================
// src/codegen/src/code_generator.cpp - codegen entry + module assembly
// (STYLE.md §8).
//
// Owns the frozen `CodeGenerator::generate` boundary (cp3/codegen.hpp), the
// `Generator` constructor/diagnostic helpers, the module namespace, and the
// synthesized assembly (the generated .hpp, the .cpp's per-TU helper block and
// `main()`), driven by `Generator::run`. Out-of-scope AST nodes report CP3002
// (deduped per nodeId) and emit a compilable stub; the generator never throws,
// and the dedup drives the CLI's exit-3 "skipped" behavior.
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
    runUnit(program, decls, defs, mainBody);
    GeneratedModule module;
    module.header = buildHeader(decls);
    module.source = buildSource(decls, defs, mainBody);
    return module;
}

// One unit's emit walk, shared by the single-unit assembly above and the
// whole-program assembler (emit/program.cpp): collect the unit's declarations
// and shapes, lower its top-level statements into the three sections, then
// prepend the resolved enum/object/JSON declarations and select the family
// includes from the emitted spellings.
void Generator::runUnit(const Program& program, std::string& decls,
                        std::string& defs, std::string& mainBody) {
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

    // The worker-object pre-scan (spec 13): only when this unit imports a
    // worker, mark the object literals that lower to `std::tuple` and the
    // kernel shapes the `worker(...)` calls accept (src/emit/worker.cpp).
    collectWorkerObjects(program);

    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) {
            emitTopLevel(*stmtPtr, decls, defs, mainBody);
        }
    }

    // Enums first (signatures and record shapes can name one), then the object
    // value and JSON record structs, complete before any builder reads them.
    decls = enumDecls_ +
            objectDeclsFor(jsonShapeDecls_ + decls + defs + mainBody) +
            jsonShapeDecls_ + decls;

    // The family include (D69) follows the emitted spellings
    // (support/usage_includes.cpp).
    applyUsageIncludes(decls + defs + mainBody);
}

// --- Generator - assembly (generated .hpp / .cpp) -----------------------------

std::string Generator::buildHeader(const std::string& decls) const {
    const std::string guard =
        "CP3_GENERATED_" + upperSnake(sanitizeIdent(moduleName_)) + "_HPP";
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName_ + "` - do not edit.\n";
    out += "#ifndef " + guard + "\n";
    out += "#define " + guard + "\n\n";
    out += "#include <optional>\n";
    out += "#include <string>\n";
    if (needsUnion_) {
        // A signature can name a primitive union (`std::variant<...>`).
        out += "#include <variant>\n";
    }
    out += "\n";
    out += "#include \"cp3/core/runtime.hpp\"\n";
    if (needsTuple_) {
        // An enum-carrying object literal can surface in a forward declaration
        // (a parameter default).
        out += "#include <tuple>\n";
    }
    if (needsEventLoop_) {
        // A declared `async function` returns `Promise<T>` (spec §14), so the
        // alias must be complete for callers of this header.
        out += "#include \"cp3/async/promise.hpp\"\n";
    }
    if (needsTypedArray_) {
        // The JS-compat typed-array family (D69): a signature or module-scope
        // binding can name `Array<T>`/the typed views.
        out += "#include \"cp3/typed_array.hpp\"\n";
    }
    if (needsCollections_) {
        // std/collections (D44): a signature or module-scope binding can name
        // `Map<K, V>`/`Set<T>`.
        out += "#include \"cp3/std/collections.hpp\"\n";
    }
    if (needsBuffer_) {
        // std/buffer: a signature or module-scope binding can name `Buffer`.
        out += "#include \"cp3/std/buffer.hpp\"\n";
    }
    if (needsEventEmitter_) {
        // The builtin EventEmitter (D74): a signature or module-scope binding
        // can name `Ref<EventEmitter>`/`Listener`.
        out += "#include \"cp3/async/emitter.hpp\"\n";
    }
    if (needsRegex_) {
        // Core regex literals (D48): a signature or module-scope binding can
        // name `::cp3::runtime::regex::Regex`.
        out += "#include \"cp3/core/regex.hpp\"\n";
    }
    out += "\n";
    out += "namespace " + namesp_ + " {\n\n";
    out += decls;
    out += "\n}  // namespace " + namesp_ + "\n\n";
    out += "#endif  // " + guard + "\n";
    return out;
}

std::string Generator::buildSource(const std::string& decls, const std::string& defs,
                                   const std::string& mainBody) const {
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName_ + "` - do not edit.\n";
    out += "#include <cmath>\n";
    out += "#include <cstddef>\n";
    out += "#include <cstdint>\n";
    out += "#include <limits>\n";
    out += "#include <optional>\n";
    out += "#include <string>\n";
    if (needsTuple_) out += "#include <tuple>\n";  // enum-carrying object literals
    if (needsUnion_) out += "#include <variant>\n";  // primitive unions
    if (needsEventLoop_) out += "#include <vector>\n";  // Promise input lists
    out += "\n";
    out += "#include \"cp3/core/runtime.hpp\"\n";
    out += "#include \"cp3/core/error.hpp\"\n";
    if (needsBitwise_) out += "#include \"cp3/core/bitwise.hpp\"\n";  // & | ^ ~ << >> >>>
    if (needsUnion_) out += "#include \"cp3/core/union.hpp\"\n";  // primitive unions
    if (needsArith_) {
        out += "#include \"cp3/core/arith.hpp\"\n";  // `%` lowering / int64 fast path
    }
    if (needsRegex_) out += "#include \"cp3/core/regex.hpp\"\n";  // regex literals
    if (needsEventLoop_) {
        out += "#include \"cp3/async/promise.hpp\"\n";  // Promise/await/timers
    }
    if (needsTypedArray_) {
        out += "#include \"cp3/typed_array.hpp\"\n";  // the family (D69)
    }
    if (needsEventEmitter_) {
        out += "#include \"cp3/async/emitter.hpp\"\n";  // the builtin (D74)
    }
    if (consoleStderr_ || needsConsoleAbi_) {
        out += "#include \"cp3/std/console.hpp\"\n";  // the console family
    }
    if (needsMath_) out += "#include \"cp3/std/math.hpp\"\n";
    if (needsString_) out += "#include \"cp3/std/string.hpp\"\n";
    if (needsPath_) out += "#include \"cp3/std/path.hpp\"\n";
    if (needsOs_) out += "#include \"cp3/std/os.hpp\"\n";
    if (needsJson_) out += "#include \"cp3/std/json.hpp\"\n";
    if (needsProcess_) out += "#include \"cp3/std/process.hpp\"\n";
    if (needsEnv_) out += "#include \"cp3/std/env.hpp\"\n";
    if (needsCrypto_) out += "#include \"cp3/std/crypto.hpp\"\n";
    if (needsDate_) out += "#include \"cp3/std/date.hpp\"\n";
    if (needsNetwork_) out += "#include \"cp3/std/network.hpp\"\n";
    if (needsCollections_) {
        out += "#include \"cp3/std/collections.hpp\"\n";  // Map/Set (D44)
    }
    if (needsBuffer_) out += "#include \"cp3/std/buffer.hpp\"\n";
    if (needsFs_) out += "#include \"cp3/std/fs.hpp\"\n";
    if (needsParallel_) {
        out += "#include \"cp3/parallel/parallel.hpp\"\n";  // parallel.* (spec 13)
    }
    if (needsWorker_) {
        out += "#include \"cp3/parallel/worker.hpp\"\n";  // worker(...) (spec 13)
        if (options_.worker && !workerModule_) {
            // The worker module's default export (`cp3_default`).
            out += "#include \"" + options_.worker->headerPath + "\"\n";
        }
    }
    out += "\n";
    out += kHelperBlock;
    if (needsTypedArray_) out += kTypedArrayHelperBlock;
    if (needsBuffer_) out += kBufferHelperBlock;
    if (needsEventEmitter_) out += kEventEmitterHelperBlock;
    if (needsUnion_) out += unionHelperBlock();
    if (needsObject_) out += objectHelperBlock();
    out += "\nnamespace " + namesp_ + " {\n\n";
    out += decls;
    out += "\n";
    out += defs;
    out += "\n}  // namespace " + namesp_ + "\n\n";
    if (!workerModule_) {
        out += "using namespace " + namesp_ + ";\n\n";
        emitMain(out, mainBody);
    }
    return out;
}

void Generator::emitMain(std::string& out, const std::string& mainBody) const {
    if (needsProcess_) {
        // std/process argv: hand the C argument vector to the runtime before
        // any cp3 statement runs (process.argv, pinned by
        // process_argv_is_array).
        out += "int main(int cp3_argc, char** cp3_argv) {\n";
        out += "    ::cp3::runtime::process::initArgv(cp3_argc, cp3_argv);\n";
    } else {
        out += "int main() {\n";
    }
    if (topLevelAwait_) {
        // Top-level `await` (spec §14): the entry unit's statements run inside
        // an eagerly-started coroutine, driven to completion by the loop below.
        out += "    auto cp3_top_level = [&]() -> ::cp3::runtime::Promise<void> {\n";
        out += indentText(mainBody, "    ");
        out += "        co_return;\n";
        out += "    };\n";
        out += "    cp3_top_level();\n";
    } else {
        out += mainBody;
    }
    if (needsEventLoop_) {
        out += "    ::cp3::runtime::runEventLoop();\n";
    }
    out += "    return 0;\n";
    out += "}\n";
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
