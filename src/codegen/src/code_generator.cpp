// =============================================================================
// src/codegen/src/code_generator.cpp - codegen entry + module assembly
// (STYLE.md §8).
//
// Owns the frozen `CodeGenerator::generate` boundary (cp3/codegen.hpp), the
// `Generator` constructor/diagnostic helpers, and the top-level emit walk
// (`Generator::run`). The generated module assembly (the .hpp/.cpp writer and
// the synthesized helper block + `main()`) lives in support/module_writer.cpp.
// A construct with no lowering reports a stage diagnostic and emits a
// compilable stub; the generator never throws. Repeated diagnostics for the
// same nodeId are deduplicated.
// =============================================================================
#include "detail/generator/generator.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "cp3/codegen.hpp"
#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/class/virtual_set.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/helper_blocks.h"
#include "detail/generator/mangle.h"

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

// A hard type error at a use site. The int64 fast-path attempt is discarded
// on any diagnostic and the ordinary pass reports in place.
void Generator::reportTypeError(const Node& node, const std::string& message) {
    if (intMode_) intMode_->failed = true;
    if (suppressDiagnostics_) return;
    sink_.error(diagcode::kTypeMismatch, node.span, message);
}

// A codegen-stage diagnostic with its own code: the construct the program used
// is a program error (a member outside a documented surface, a value no C++
// spelling can carry), so it gets its own code and the ordinary compile-error
// exit.
void Generator::reportCodegenError(const Node& node, const std::string& code,
                                    const std::string& message) {
    if (intMode_) intMode_->failed = true;
    if (suppressDiagnostics_) return;
    if (node.nodeId != 0) {
        if (!reported_.insert(node.nodeId).second) {
            return;
        }
    }
    sink_.error(code, node.span, message);
}

// A real compile error: the construct has no lowering, so the program must not
// compile (the CLI stops before the C++ step). Inside an int64 fast-path
// attempt nothing is reported.
void Generator::reportUnsupported(const Node& node, const char* code,
                                   const std::string& message) {
    if (intMode_) intMode_->failed = true;
    if (suppressDiagnostics_) return;
    if (node.nodeId != 0) {
        if (!reported_.insert(node.nodeId).second) {
            return;
        }
    }
    sink_.error(code, node.span, message);
}

GeneratedModule Generator::run(const Program& program) {
    // The program-wide override analysis (spec: virtual dispatch): a
    // single-unit program IS its whole program, so the set is built from the
    // one unit before any class is emitted. The whole-program path installs
    // its own set (emit/program.cpp).
    const std::unordered_set<std::string> virtualSet =
        detail::buildVirtualMethodSet({&program});
    virtualMethodSet_ = &virtualSet;
    // The worker unit lowers only its default-export arrow (spec 13); the
    // entry unit's ordinary walk follows.
    if (options_.workerModule) {
        return runWorkerModule(program);
    }
    // D79: a scaled `createServer` opts the whole unit into per-loop module
    // state. Units without the option keep the exact pre-D79 lowering.
    scaleMode_ = sema_.networkScaleMode;
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
// Whether the module bindings must stay at namespace scope, and the entry-body
// int64 proof when they need not.
void Generator::decideModuleScope(const Program& program) {
    moduleNeedsGlobals_ = programMode_ || scaleMode_ ||
                          !functions_.empty() || !workerSites_.empty() ||
                          !options_.workers.empty();
    if (!moduleNeedsGlobals_) {
        for (const auto& stmtPtr : program.body) {
            if (stmtPtr && (stmtPtr->kind == NodeKind::ExportDeclaration ||
                            stmtPtr->kind == NodeKind::ClassDeclaration)) {
                moduleNeedsGlobals_ = true;
                break;
            }
        }
    }
    if (!moduleNeedsGlobals_) {
        moduleNeedsGlobals_ = moduleUsesBeforeDecl(program);
    }
    if (!moduleNeedsGlobals_) analyzeTopInt64(program);
}

void Generator::runUnit(const Program& program, std::string& decls,
                        std::string& defs, std::string& mainBody,
                        std::string& globals) {
    collectNumericConstants(program);
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        collectDeclaredNames(*stmtPtr);
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

    // The native bindings this unit imports (`import native "id" as alias`):
    // the alias map and the headers the generated .cpp must include.
    collectNativeImports(program);

    // Array element reads that can bind a const reference (array_refs.cpp).
    analyzeArrayRefs(program);

    // The worker sites (spec 13, D80): file sites arrive resolved from the CLI;
    // the inline form is synthesized into the same site model (worker.cpp).
    collectWorkerSites(program);

    // Module bindings become `main` locals only when this entry function is
    // their sole possible reader: any declared function, class, export or
    // worker can name them from outside `main`, as can a reference that
    // precedes the declaration (namespace scope hoists, a local does not).
    // The entry-body int64 proof (support/top_int64.cpp) requires this
    // lowering, so it runs only after the condition is settled.
    decideModuleScope(program);

    // The closure-capture analysis (spec 10): classify every function frame's
    // locals before the emit walk, so a shared mutable binding lowers to a
    // `Cell<T>` from its first declaration on.
    captures_ = analyzeCaptures(program);
    currentFrameId_ = program.nodeId;

    // The spec 15 object surface: rebuild the class/interface/object models
    // for this unit before anything is emitted, so a class used above its
    // declaration (and a function taking one) already has a C++ spelling.
    detail::ModelRegistry::get().reset();
    detail::ModelRegistry::get().setNamespace(namesp_);
    collectModels(program);
    analyzeEscapeAllocations(program);
    analyzeDataViewLoops(program);

    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) {
            emitTopLevel(*stmtPtr, decls, defs, mainBody);
        }
    }

    if (scaleMode_) {
        // D79: replay the module-scope runtime initializers on every child loop
        // thread (the main thread already ran them in `mainBody` in source
        // order). The runtime calls this after the loop is created and before
        // the loop's uWS App exists.
        std::string hook = "\nvoid cp3_init_module_state() {\n";
        for (const std::string& initLine : moduleInitLines_) {
            hook += "    " + initLine + "\n";
        }
        hook += "}\n";
        defs += hook;
    }

    // The inline worker entries (D80) are ordinary functions of this unit: one
    // `cp3_inline_worker_<n>` per inline site, emitted after the unit's own
    // declarations so their bodies can call this unit's top-level functions.
    emitInlineWorkerEntries(decls, defs);

    // Class structs materialize after the walk (every class and signature is
    // registered by then), before the structural object/interface structs (a
    // method signature can still register a new object shape while it emits).
    emitAllClasses();
    objectDecls_ = objectStructsText();
    if (!objectDecls_.empty()) needsRef_ = true;

    // Enums first (signatures and record shapes can name one), then the JSON
    // record structs, the structs reifying generic-call object literals
    // (support/generic.cpp), class forward declarations and structs, the
    // structural object value shapes, and then the unit's own declarations.
    // All are namespace-scope and complete before any builder reads them.
    decls = enumDecls_ + classFwdDecls_ + jsonShapeDecls_ + objectDecls_ +
            genericDecls_ + classDecls_ + decls;
    defs = classDefs_ + defs;
    globals = objectGlobalDeclsFor(decls + defs + mainBody, needsWorker_);

    // A tuple can surface only in a signature (a tuple return type or a tuple
    // parameter) with no literal or index read to raise the flag; scan the
    // emitted spellings before the header chooses its includes.
    const std::string unitText = decls + defs + mainBody;
    if (unitText.find("std::tuple") != std::string::npos ||
        unitText.find("std::make_tuple") != std::string::npos ||
        unitText.find("std::get<") != std::string::npos) {
        needsTuple_ = true;
    }

    // The family include (D69) follows the emitted spellings
    // (support/usage_includes.cpp).
    applyUsageIncludes(decls + defs + globals + mainBody);
}

namespace {

std::optional<double> evalNumericConstant(
    const Expr& expression,
    const std::unordered_map<std::string, double>& values) {
    switch (expression.kind) {
        case NodeKind::NumberLiteral:
            return static_cast<const NumberLiteral&>(expression).value;
        case NodeKind::Identifier: {
            const std::string& name =
                static_cast<const Identifier&>(expression).name;
            const auto found = values.find(name);
            return found == values.end() ? std::nullopt
                                         : std::optional<double>(found->second);
        }
        case NodeKind::UnaryExpr: {
            const auto& unary = static_cast<const UnaryExpr&>(expression);
            if (!unary.operand || (unary.op != UnaryOp::Negate &&
                                   unary.op != UnaryOp::Plus)) {
                return std::nullopt;
            }
            const auto operand = evalNumericConstant(*unary.operand, values);
            if (!operand) return std::nullopt;
            const double result = unary.op == UnaryOp::Negate ? -*operand
                                                              : *operand;
            return std::isfinite(result) ? std::optional<double>(result)
                                         : std::nullopt;
        }
        case NodeKind::BinaryExpr: {
            const auto& binary = static_cast<const BinaryExpr&>(expression);
            if (!binary.left || !binary.right) return std::nullopt;
            const auto left = evalNumericConstant(*binary.left, values);
            const auto right = evalNumericConstant(*binary.right, values);
            if (!left || !right) return std::nullopt;
            double result = 0.0;
            switch (binary.op) {
                case BinaryOp::Add: result = *left + *right; break;
                case BinaryOp::Subtract: result = *left - *right; break;
                case BinaryOp::Multiply: result = *left * *right; break;
                case BinaryOp::Divide:
                    if (*right == 0.0) return std::nullopt;
                    result = *left / *right;
                    break;
                default: return std::nullopt;
            }
            return std::isfinite(result) ? std::optional<double>(result)
                                         : std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

}  // namespace

void Generator::collectNumericConstants(const Program& program) {
    numericConstBindings_.clear();
    std::unordered_map<std::string, double> values;
    for (const StmtPtr& statement : program.body) {
        if (!statement || statement->kind != NodeKind::VariableDeclaration) {
            continue;
        }
        const auto& declaration =
            static_cast<const VariableDeclaration&>(*statement);
        if (declaration.kind != VarKind::Const) continue;
        for (const VariableDeclaratorPtr& item : declaration.declarations) {
            if (!item || !item->id || item->id->kind != NodeKind::Identifier ||
                !item->init) {
                continue;
            }
            const auto value = evalNumericConstant(*item->init, values);
            if (!value) continue;
            const std::string& name =
                static_cast<const Identifier&>(*item->id).name;
            values[name] = *value;
            numericConstBindings_[item->nodeId] = *value;
        }
    }
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
