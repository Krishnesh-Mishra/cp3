// =============================================================================
// src/codegen/src/emit/program.cpp - whole-program assembly (STYLE.md §8).
//
// OWNERSHIP: codegen module. Implements cp3/codegen_program.hpp and the
// `Generator` members the header declares for it.
//
// A local-module program lowers to ONE C++20 translation unit: every unit gets
// its own `cp3_mod_<name>` namespace, emitted in two phases (all declaration
// sections first, then all definition sections) so units that reference each
// other - a cycle included - compile in any emission order. Each non-entry
// unit's top-level statements become its `cp3_module_init()`, run from `main`
// in ModuleGraph::compilationOrder() (dependencies first, ES-module
// semantics), and the entry unit's statements run in `main` under
// `using namespace <entry-ns>;`. An import resolves through the link table
// (detail/program_link.h) to the target unit's qualified C++ name, so
// module-scope bindings keep live-binding semantics.
//
// `assembleProgram` mirrors code_generator.cpp's include/helper selection for
// the union of the units' needs; the R"( strings of the helper blocks are the
// same objects (detail/helper_blocks.h), so both paths splice the same bytes.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cp3/codegen.hpp"
#include "cp3/codegen_program.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/helper_blocks.h"
#include "detail/generator/mangle.h"
#include "detail/generator/program_assembly.h"
#include "detail/generator/program_link.h"

namespace cp3 {

using namespace detail;

namespace {

// The `main()` call of a unit's module initializer.
std::string initCall(const ProgramUnitText& unit) {
    return unit.namesp + "::cp3_module_init()";
}

// The entry unit's text, or nullptr for an empty assembly.
const ProgramUnitText* entryOf(const ProgramAssembly& assembly) {
    for (const ProgramUnitText& unit : assembly.units) {
        if (unit.isEntry) return &unit;
    }
    return nullptr;
}

// Does any unit carry top-level `await`? Then `main` itself must be a
// coroutine so the awaitable initializers can be awaited in order.
bool programHasAwait(const ProgramAssembly& assembly) {
    for (const ProgramUnitText& unit : assembly.units) {
        if (unit.topLevelAwait) return true;
    }
    return false;
}

// One unit's module initializer: the unit's top-level statements, wrapped in
// `cp3_module_init()` (`Promise<void>` when the body awaits).
std::string unitInitDef(const ProgramUnitText& unit) {
    std::string out;
    out += unit.topLevelAwait
               ? "::cp3::runtime::Promise<void> cp3_module_init() {\n"
               : "void cp3_module_init() {\n";
    out += unit.mainBody;
    if (unit.topLevelAwait) out += "    co_return;\n";
    out += "}\n";
    return out;
}

// One unit's contribution: its namespace, the declaration section, the
// definition section and (non-entry units) the module initializer. A package
// unit contributes only its declarations: its implementation is compiled from
// the archive's `pak/` sources, not regenerated here.
void appendUnit(std::string& out, const ProgramUnitText& unit, bool definitions) {
    if (definitions && unit.isPackage) return;
    out += "namespace " + unit.namesp + " {\n\n";
    out += definitions ? unit.defs : unit.decls;
    out += "\n";
    if (definitions && !unit.isEntry) out += unitInitDef(unit);
    out += "}  // namespace " + unit.namesp + "\n\n";
}

// The generated `main()`: the process argument vector init, the module
// initializers in compilation order, the entry unit's statements, and the
// event-loop drain.
void appendProgramMain(std::string& out, const ProgramAssembly& assembly) {
    if (assembly.needs.process) {
        out += "int main(int cp3_argc, char** cp3_argv) {\n";
        out += "    ::cp3::runtime::process::initArgv(cp3_argc, cp3_argv);\n";
    } else {
        out += "int main() {\n";
    }
    if (programHasAwait(assembly)) {
        out += "    auto cp3_top_level = [&]() -> ::cp3::runtime::Promise<void> {\n";
        for (const ProgramUnitText& unit : assembly.units) {
            if (unit.isEntry || unit.isPackage) continue;
            out += unit.topLevelAwait ? "        co_await " + initCall(unit) + ";\n"
                                      : "        " + initCall(unit) + ";\n";
        }
        const ProgramUnitText* entry = entryOf(assembly);
        if (entry != nullptr) out += indentText(entry->mainBody, "    ");
        out += "        co_return;\n";
        out += "    };\n";
        out += "    cp3_top_level();\n";
    } else {
        for (const ProgramUnitText& unit : assembly.units) {
            if (!unit.isEntry && !unit.isPackage)
                out += "    " + initCall(unit) + ";\n";
        }
        const ProgramUnitText* entry = entryOf(assembly);
        if (entry != nullptr) out += entry->mainBody;
    }
    if (assembly.needs.eventLoop) {
        out += "    ::cp3::runtime::runEventLoop();\n";
    }
    out += "    return 0;\n";
    out += "}\n";
}

// The generated .hpp: the program's public surface is the entry unit's
// declarations (the single-unit `buildHeader` shape, entry namespace).
std::string programHeader(const ProgramAssembly& assembly,
                          const std::string& moduleName) {
    const ProgramUnitText* entry = entryOf(assembly);
    const std::string namesp =
        entry != nullptr ? entry->namesp : suggestNamespace(moduleName);
    const std::string guard =
        "CP3_GENERATED_" + upperSnake(sanitizeIdent(moduleName)) + "_HPP";
    const ProgramNeeds& n = assembly.needs;
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName + "` - do not edit.\n";
    out += "#ifndef " + guard + "\n";
    out += "#define " + guard + "\n\n";
    out += "#include <optional>\n";
    out += "#include <string>\n";
    if (n.unionValue) {
        // A signature can name a primitive union (`std::variant<...>`).
        out += "#include <variant>\n";
    }
    out += "\n";
    out += "#include \"cp3/core/runtime.hpp\"\n";
    if (n.tuple) out += "#include <tuple>\n";
    if (n.eventLoop) out += "#include \"cp3/async/promise.hpp\"\n";
    if (n.typedArray) out += "#include \"cp3/typed_array.hpp\"\n";
    if (n.collections) out += "#include \"cp3/std/collections.hpp\"\n";
    if (n.buffer) out += "#include \"cp3/std/buffer.hpp\"\n";
    if (n.eventEmitter) out += "#include \"cp3/async/emitter.hpp\"\n";
    if (n.regex) out += "#include \"cp3/core/regex.hpp\"\n";
    out += "\n";
    if (entry != nullptr && !entry->globals.empty()) {
        out += entry->globals;
        out += "\n";
    }
    out += "namespace " + namesp + " {\n\n";
    if (entry != nullptr) out += entry->decls;
    out += "\n}  // namespace " + namesp + "\n\n";
    out += "#endif  // " + guard + "\n";
    return out;
}

// The generated .cpp: the union include set, the shared helper blocks, the
// units in two phases, then `main`.
std::string programSource(const ProgramAssembly& assembly,
                          const std::string& moduleName) {
    const ProgramNeeds& n = assembly.needs;
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName + "` - do not edit.\n";
    out += "#include <cmath>\n#include <cstddef>\n#include <cstdint>\n";
    out += "#include <limits>\n#include <optional>\n#include <string>\n";
    if (n.tuple) out += "#include <tuple>\n";          // enum-carrying literals
    if (n.unionValue) out += "#include <variant>\n";   // primitive unions
    if (n.eventLoop) out += "#include <vector>\n";     // Promise input lists
    out += "\n";
    out += "#include \"cp3/core/runtime.hpp\"\n";
    out += "#include \"cp3/core/error.hpp\"\n";
    if (n.bitwise) out += "#include \"cp3/core/bitwise.hpp\"\n";
    if (n.unionValue) out += "#include \"cp3/core/union.hpp\"\n";
    if (n.arith) out += "#include \"cp3/core/arith.hpp\"\n";
    if (n.regex) out += "#include \"cp3/core/regex.hpp\"\n";
    if (n.eventLoop) out += "#include \"cp3/async/promise.hpp\"\n";
    if (n.typedArray) out += "#include \"cp3/typed_array.hpp\"\n";
    if (n.eventEmitter) out += "#include \"cp3/async/emitter.hpp\"\n";
    if (n.consoleStderr || n.consoleAbi) out += "#include \"cp3/std/console.hpp\"\n";
    if (n.math) out += "#include \"cp3/std/math.hpp\"\n";
    if (n.str) out += "#include \"cp3/std/string.hpp\"\n";
    if (n.path) out += "#include \"cp3/std/path.hpp\"\n";
    if (n.os) out += "#include \"cp3/std/os.hpp\"\n";
    if (n.json) out += "#include \"cp3/std/json.hpp\"\n";
    if (n.process) out += "#include \"cp3/std/process.hpp\"\n";
    if (n.env) out += "#include \"cp3/std/env.hpp\"\n";
    if (n.crypto) out += "#include \"cp3/std/crypto.hpp\"\n";
    if (n.date) out += "#include \"cp3/std/date.hpp\"\n";
    if (n.network) out += "#include \"cp3/std/network.hpp\"\n";
    if (n.collections) out += "#include \"cp3/std/collections.hpp\"\n";
    if (n.buffer) out += "#include \"cp3/std/buffer.hpp\"\n";
    if (n.fs) out += "#include \"cp3/std/fs.hpp\"\n";
    if (n.io) out += "#include \"cp3/std/io.hpp\"\n";
    if (n.parallel) out += "#include \"cp3/parallel/parallel.hpp\"\n";
    if (n.worker) {
        out += "#include \"cp3/parallel/worker.hpp\"\n";
        for (const std::string& header : n.workerHeaders) {
            out += "#include \"" + header + "\"\n";
        }
    }
    // The native bindings' own headers (`import native "id" as alias`).
    for (const std::string& header : n.nativeHeaders) {
        out += "#include \"" + header + "\"\n";
    }
    out += "\n";
    out += kHelperBlock;
    if (n.typedArray) out += kTypedArrayHelperBlock;
    if (n.buffer) out += kBufferHelperBlock;
    if (n.eventEmitter) out += kEventEmitterHelperBlock;
    if (n.unionValue) out += unionHelperBlock();
    if (n.objectValue) out += objectHelperBlock();
    out += "\n";
    // The generated `$o{...}` value structs are global (guarded), so every
    // unit spells the same nominal type and a worker TU can share it.
    for (const ProgramUnitText& unit : assembly.units) {
        if (!unit.globals.empty()) {
            out += unit.globals;
            out += "\n";
        }
    }
    // Phase 1: every unit's declaration section (forward declarations and
    // module-scope bindings), so phase 2 can reference any unit.
    for (const ProgramUnitText& unit : assembly.units) {
        appendUnit(out, unit, false);
    }
    // Phase 2: every unit's definitions plus its module initializer.
    for (const ProgramUnitText& unit : assembly.units) {
        appendUnit(out, unit, true);
    }
    const ProgramUnitText* entry = entryOf(assembly);
    if (entry != nullptr) out += "using namespace " + entry->namesp + ";\n\n";
    appendProgramMain(out, assembly);
    return out;
}

}  // namespace

namespace detail {

GeneratedModule assembleProgram(const ProgramAssembly& assembly,
                                const std::string& moduleName) {
    GeneratedModule module;
    module.header = programHeader(assembly, moduleName);
    module.source = programSource(assembly, moduleName);
    return module;
}

}  // namespace detail

// -----------------------------------------------------------------------------
// Generator - whole-program orchestration
// -----------------------------------------------------------------------------

GeneratedModule Generator::runProgram(
    const ModuleGraph& graph, const std::vector<CheckedModuleUnit>& units) {
    const ProgramLink link = buildProgramLink(graph, units);
    ProgramAssembly assembly;
    for (CompilationUnit* unit : graph.compilationOrder()) {
        if (unit == nullptr || unit->ast == nullptr) continue;
        if (unit->isPrelude && !unit->isEntry) continue;
        if (link.semas.find(unit->id) == link.semas.end()) continue;
        emitProgramUnit(link, *unit, assembly);
    }
    return assembleProgram(assembly, moduleName_);
}

void Generator::emitProgramUnit(const ProgramLink& link,
                                const CompilationUnit& unit,
                                ProgramAssembly& assembly) {
    const auto semaIt = link.semas.find(unit.id);
    if (semaIt == link.semas.end() || semaIt->second == nullptr || !unit.ast) {
        return;
    }
    // Only the entry unit carries options (its worker import, spec 13); an
    // imported unit always generates with the defaults. The native bindings
    // are the program's, so every unit gets them.
    CodegenOptions options = unit.isEntry ? options_ : CodegenOptions{};
    options.nativeBindings = options_.nativeBindings;
    Generator inner(sink_, *semaIt->second, unit.moduleName, lineMapping_,
                    options);
    inner.programMode_ = true;
    inner.packageSurfaceOnly_ = unit.isPackage;
    const auto importsIt = link.imports.find(unit.id);
    if (importsIt != link.imports.end()) inner.linkedNames_ = importsIt->second;
    ProgramUnitText text;
    const auto surfaceIt = link.surfaces.find(unit.id);
    text.namesp = surfaceIt != link.surfaces.end()
                      ? surfaceIt->second.namesp
                      : suggestNamespace(unit.moduleName);
    text.isEntry = unit.isEntry;
    text.isPackage = unit.isPackage;
    inner.runUnit(*unit.ast, text.decls, text.defs, text.mainBody,
                  text.globals);
    text.topLevelAwait = inner.topLevelAwait_;
    ProgramNeeds& n = assembly.needs;
    n.consoleStderr |= inner.consoleStderr_;
    n.consoleAbi |= inner.needsConsoleAbi_;
    n.eventLoop |= inner.needsEventLoop_ || inner.topLevelAwait_;
    n.arith |= inner.needsArith_;
    n.math |= inner.needsMath_;
    n.str |= inner.needsString_;
    n.path |= inner.needsPath_;
    n.os |= inner.needsOs_;
    n.json |= inner.needsJson_;
    n.process |= inner.needsProcess_;
    n.env |= inner.needsEnv_;
    n.crypto |= inner.needsCrypto_;
    n.date |= inner.needsDate_;
    n.network |= inner.needsNetwork_;
    n.typedArray |= inner.needsTypedArray_;
    n.collections |= inner.needsCollections_;
    n.buffer |= inner.needsBuffer_;
    n.fs |= inner.needsFs_;
    n.io |= inner.needsIo_;
    n.tuple |= inner.needsTuple_;
    n.eventEmitter |= inner.needsEventEmitter_;
    n.bitwise |= inner.needsBitwise_;
    n.unionValue |= inner.needsUnion_;
    n.objectValue |= inner.needsObject_;
    n.regex |= inner.needsRegex_;
    n.parallel |= inner.needsParallel_;
    n.worker |= inner.needsWorker_;
    for (const std::string& header : inner.nativeHeaders_) {
        bool seen = false;
        for (const std::string& existing : n.nativeHeaders) {
            if (existing == header) seen = true;
        }
        if (!seen) n.nativeHeaders.push_back(header);
    }
    if (unit.isEntry) {
        for (const WorkerSite& site : options.workers) {
            if (site.inlineSite || site.headerPath.empty()) continue;
            bool seen = false;
            for (const std::string& existing : n.workerHeaders) {
                if (existing == site.headerPath) seen = true;
            }
            if (!seen) n.workerHeaders.push_back(site.headerPath);
        }
    }
    assembly.units.push_back(std::move(text));
}

// -----------------------------------------------------------------------------
// The whole-program boundary (cp3/codegen_program.hpp)
// -----------------------------------------------------------------------------

GeneratedModule generateProgram(const GenerateProgramInput& input) {
    const SemaResult* entrySema = nullptr;
    for (const CheckedModuleUnit& checked : input.units) {
        if (checked.unit != nullptr && checked.unit->isEntry &&
            checked.sema != nullptr) {
            entrySema = checked.sema;
        }
    }
    SemaResult noSema;  // the outer generator only orchestrates the units
    Generator generator(input.sink, entrySema != nullptr ? *entrySema : noSema,
                        input.moduleName, input.lineDirectives, input.options);
    return generator.runProgram(input.graph, input.units);
}

}  // namespace cp3
