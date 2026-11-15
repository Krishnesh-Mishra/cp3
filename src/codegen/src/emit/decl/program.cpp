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
#include "detail/class/virtual_set.h"
#include "detail/generator/generator.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/codegen.hpp"
#include "cp3/codegen_program.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/fragment_cache.hpp"
#include "detail/generator/helper_blocks.h"
#include "detail/generator/mangle.h"
#include "detail/generator/program_assembly.h"
#include "detail/generator/program_link.h"

namespace cp3 {

using namespace detail;

namespace {

// fnv1a64 render (reuse format); matches the CLI's hex rendering.
std::string hex64u(std::uint64_t value) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string(buf, 16);
}

// ProgramNeeds <-> the fragment needsMask bit layout (see
// detail/generator/fragment_cache.hpp). The bits are filled in one place so
// the two lists cannot drift.
void applyNeedsMask(ProgramNeeds& needs, std::uint64_t mask) {
    needs.consoleStderr |= (mask & (std::uint64_t{1} << 0)) != 0;
    needs.consoleAbi |= (mask & (std::uint64_t{1} << 1)) != 0;
    needs.eventLoop |= (mask & (std::uint64_t{1} << 2)) != 0;
    needs.arith |= (mask & (std::uint64_t{1} << 3)) != 0;
    needs.math |= (mask & (std::uint64_t{1} << 4)) != 0;
    needs.number |= (mask & (std::uint64_t{1} << 5)) != 0;
    needs.str |= (mask & (std::uint64_t{1} << 6)) != 0;
    needs.path |= (mask & (std::uint64_t{1} << 7)) != 0;
    needs.os |= (mask & (std::uint64_t{1} << 8)) != 0;
    needs.json |= (mask & (std::uint64_t{1} << 9)) != 0;
    needs.jsonFile |= (mask & (std::uint64_t{1} << 10)) != 0;
    needs.process |= (mask & (std::uint64_t{1} << 11)) != 0;
    needs.env |= (mask & (std::uint64_t{1} << 12)) != 0;
    needs.crypto |= (mask & (std::uint64_t{1} << 13)) != 0;
    needs.date |= (mask & (std::uint64_t{1} << 14)) != 0;
    needs.url |= (mask & (std::uint64_t{1} << 15)) != 0;
    needs.time |= (mask & (std::uint64_t{1} << 16)) != 0;
    needs.hash |= (mask & (std::uint64_t{1} << 17)) != 0;
    needs.text |= (mask & (std::uint64_t{1} << 18)) != 0;
    needs.readline |= (mask & (std::uint64_t{1} << 19)) != 0;
    needs.network |= (mask & (std::uint64_t{1} << 20)) != 0;
    needs.webserverSettings |= (mask & (std::uint64_t{1} << 21)) != 0;
    needs.typedArray |= (mask & (std::uint64_t{1} << 22)) != 0;
    needs.collections |= (mask & (std::uint64_t{1} << 23)) != 0;
    needs.buffer |= (mask & (std::uint64_t{1} << 24)) != 0;
    needs.fs |= (mask & (std::uint64_t{1} << 25)) != 0;
    needs.io |= (mask & (std::uint64_t{1} << 26)) != 0;
    needs.tuple |= (mask & (std::uint64_t{1} << 27)) != 0;
    needs.eventEmitter |= (mask & (std::uint64_t{1} << 28)) != 0;
    needs.bitwise |= (mask & (std::uint64_t{1} << 29)) != 0;
    needs.unionValue |= (mask & (std::uint64_t{1} << 30)) != 0;
    needs.objectValue |= (mask & (std::uint64_t{1} << 31)) != 0;
    needs.regex |= (mask & (std::uint64_t{1} << 32)) != 0;
    needs.parallel |= (mask & (std::uint64_t{1} << 33)) != 0;
    needs.worker |= (mask & (std::uint64_t{1} << 34)) != 0;
}

// The native-header unique-append the merge uses for reused fragments.
void appendUnique(std::vector<std::string>& out, const std::string& item) {
    for (const std::string& existing : out) {
        if (existing == item) return;
    }
    out.push_back(item);
}

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
    // Reclaim the remaining suspect islands
    // (docs/design/cycle-collector.md).
    out += "    ::cp3::runtime::CycleCollector::current().drain();\n";
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
// The prologue include block for every generated TU of a program: the union of
// the units' needs. Factored so the single-TU and the per-module plural
// pipeline splice identical bytes.
std::string needsIncludes(const ProgramNeeds& n) {
    std::string out;
    out += "#include <cmath>\n#include <cstddef>\n#include <cstdint>\n";
    out += "#include <limits>\n#include <optional>\n#include <string>\n";
    out += "#include <type_traits>\n#include <functional>\n";
    if (n.tuple) out += "#include <tuple>\n";          // enum-carrying literals
    if (n.unionValue) out += "#include <variant>\n";   // primitive unions
    if (n.eventLoop) out += "#include <vector>\n";     // Promise input lists
    out += "\n";
    out += "#include \"cp3/core/runtime.hpp\"\n";
    out += "#include \"cp3/core/error.hpp\"\n";
    out += "#include \"cp3/core/checked.hpp\"\n";
    out += "#include \"cp3/core/value.hpp\"\n";
    out += "#include \"cp3/core/cycle.hpp\"\n";
    if (n.bitwise) out += "#include \"cp3/core/bitwise.hpp\"\n";
    if (n.unionValue) out += "#include \"cp3/core/union.hpp\"\n";
    if (n.arith) out += "#include \"cp3/core/arith.hpp\"\n";
    if (n.regex) out += "#include \"cp3/core/regex.hpp\"\n";
    if (n.eventLoop) out += "#include \"cp3/async/promise.hpp\"\n";
    if (n.typedArray) out += "#include \"cp3/typed_array.hpp\"\n";
    if (n.eventEmitter) out += "#include \"cp3/async/emitter.hpp\"\n";
    if (n.consoleStderr || n.consoleAbi) out += "#include \"cp3/std/console.hpp\"\n";
    if (n.number) out += "#include \"cp3/core/number.hpp\"\n";
    if (n.math) out += "#include \"cp3/std/math.hpp\"\n";
    if (n.str) out += "#include \"cp3/std/string.hpp\"\n";
    if (n.path) out += "#include \"cp3/std/path.hpp\"\n";
    if (n.os) out += "#include \"cp3/std/os.hpp\"\n";
    if (n.json) out += "#include \"cp3/std/json.hpp\"\n";
    if (n.jsonFile) out += "#include \"cp3/std/json_file.hpp\"\n";
    if (n.process) out += "#include \"cp3/std/process.hpp\"\n";
    if (n.env) out += "#include \"cp3/std/env.hpp\"\n";
    if (n.crypto) out += "#include \"cp3/std/crypto.hpp\"\n";
    if (n.date) out += "#include \"cp3/std/date.hpp\"\n";
    if (n.url) out += "#include \"cp3/std/url.hpp\"\n";
    if (n.time) out += "#include \"cp3/std/time.hpp\"\n";
    if (n.hash) out += "#include \"cp3/std/hash.hpp\"\n";

    if (n.text) out += "#include \"cp3/std/text.hpp\"\n";
    if (n.readline) out += "#include \"cp3/std/readline.hpp\"\n";
    if (n.network) {
        out += "#include \"cp3/std/network.hpp\"\n";
        out += "#include \"cp3/std/network/tcp.hpp\"\n";
        out += "#include \"cp3/std/network/udp.hpp\"\n";
        out += "#include \"cp3/std/network/tls.hpp\"\n";
    }
    if (n.webserverSettings) out += "#include \"cp3/std/network/webserver/settings.hpp\"\n";
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
    return out;
}

// The helper blocks for every generated TU of a program, in the fixed order
// both pipelines emit them (the helper block first, then the family blocks).
std::string helperBlocks(const ProgramNeeds& n) {
    std::string out;
    out += kHelperBlock;
    if (n.typedArray) out += kTypedArrayHelperBlock;
    if (n.buffer) out += kBufferHelperBlock;
    if (n.eventEmitter) out += kEventEmitterHelperBlock;
    if (n.unionValue) out += unionHelperBlock();
    if (n.objectValue) out += objectHelperBlock();
    out += "\n";
    return out;
}

std::string programSource(const ProgramAssembly& assembly,
                          const std::string& moduleName) {
    const ProgramNeeds& n = assembly.needs;
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName + "` - do not edit.\n";
    out += needsIncludes(n);
    out += helperBlocks(n);
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

// The shared header of the plural pipeline: every unit TU and the entry TU
// include it. Contains the same prologue the single-TU pipeline emits, the
// helper blocks, every unit's guarded globals, every unit's declaration
// section and the module-initializer forward declarations, so cross-unit
// references resolve exactly as they do through the single TU's phase 1.
// The include guard is one literal: a build directory holds one program.
std::string sharedProgramHeader(const ProgramAssembly& assembly,
                                const std::string& moduleName) {
    const ProgramNeeds& n = assembly.needs;
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName + "` - do not edit.\n";
    out += "#ifndef CP3_PROGRAM_HPP\n";
    out += "#define CP3_PROGRAM_HPP\n\n";
    out += needsIncludes(n);
    out += helperBlocks(n);
    // The generated `$o{...}` value structs are global (guarded), so every
    // TU names the same nominal type.
    for (const ProgramUnitText& unit : assembly.units) {
        if (!unit.globals.empty()) {
            out += unit.globals;
            out += "\n";
        }
    }
    // Phase 1: every unit's declaration section (forward declarations and
    // module-scope bindings), so per-unit TUs can reference one another in
    // any order. Non-entry units additionally forward-declare their
    // module initializer for the entry TU's main.
    for (const ProgramUnitText& unit : assembly.units) {
        appendUnit(out, unit, /*definitions=*/false);
        if (unit.isEntry || unit.isPackage) continue;
        out += "namespace " + unit.namesp + " {\n";
        out += unit.topLevelAwait
                   ? "::cp3::runtime::Promise<void> cp3_module_init();\n"
                   : "void cp3_module_init();\n";
        out += "}  // namespace " + unit.namesp + "\n\n";
    }
    out += "#endif  // CP3_PROGRAM_HPP\n";
    return out;
}

// One checked unit as its own translation unit: the shared header plus the
// unit's definitions and its module initializer.
std::string unitModuleSource(const ProgramUnitText& unit) {
    std::string out;
    out += "// Generated by cp3c codegen from `" + unit.sourceKey +
           "` - do not edit.\n";
    out += "#include \"cp3_program.hpp\"\n\n";
    appendUnit(out, unit, /*definitions=*/true);
    return out;
}

// The small entry TU: the shared header, the entry namespace block (its
// definitions), the using declaration and the program's main (module
// initializers in compilation order, then the entry body and the event-loop
// drain).
std::string entryModuleSource(const ProgramAssembly& assembly,
                              const std::string& moduleName) {
    std::string out;
    out += "// Generated by cp3c codegen from `" + moduleName + "` - do not edit.\n";
    out += "#include \"cp3_program.hpp\"\n\n";
    const ProgramUnitText* entry = entryOf(assembly);
    if (entry != nullptr) {
        appendUnit(out, *entry, /*definitions=*/true);
        out += "using namespace " + entry->namesp + ";\n\n";
    }
    appendProgramMain(out, assembly);
    return out;
}

// The unit TU name: the sourceKey (the unit's logical module name, e.g.
// "lib/base/b0") mirrors the project layout under the build directory.
// Segments are path-sanitized; anything unfit becomes '_' so the output is
// always a relative path a command line can carry.
std::string unitNameForPath(const std::string& sourceKey) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= sourceKey.size()) {
        const std::size_t end = sourceKey.find('/', start);
        std::string seg = sourceKey.substr(start,
                                           end == std::string::npos ? std::string::npos
                                                                     : end - start);
        std::string clean;
        clean.reserve(seg.size());
        for (const unsigned char c : seg) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_';
            clean.push_back(ok ? static_cast<char>(c) : '_');
        }
        if (clean == "..") clean = "__";
        if (!clean.empty() && clean != ".") segments.push_back(clean);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (segments.empty()) return "unit";
    std::string out;
    for (const std::string& seg : segments) {
        out += seg;
        out += '/';
    }
    out.pop_back();
    return out;
}

GeneratedProgramModules assembleProgramModules(const ProgramAssembly& assembly,
                                               const std::string& moduleName) {
    GeneratedProgramModules out;
    out.sharedHeader.source = sharedProgramHeader(assembly, moduleName);
    out.sharedHeader.header = programHeader(assembly, moduleName);
    for (const ProgramUnitText& unit : assembly.units) {
        // The entry unit's definitions belong in the entry TU itself (it
        // carries the namespace block, the using directive and main());
        // emitting a second TU for it would overwrite it.
        if (unit.isEntry || unit.isPackage) continue;
        GeneratedProgramUnit u;
        u.name = unitNameForPath(unit.sourceKey);
        u.sourceKey = unit.sourceKey;
        u.source = unitModuleSource(unit);
        out.units.push_back(std::move(u));
    }
    out.entry.source = entryModuleSource(assembly, moduleName);
    out.entry.header = programHeader(assembly, moduleName);
    return out;
}

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

// The shared assembly of the single-TU and the per-module plural pipelines:
// the link, the program-wide virtual set and every unit's texts in emission
// order.
ProgramAssembly Generator::buildAssembly(
    const ModuleGraph& graph, const std::vector<CheckedModuleUnit>& units) {
    const ProgramLink link = buildProgramLink(graph, units);
    // One pass over every unit's AST, BEFORE any class is emitted: the
    // program-wide set of members that must lower as `virtual`. An override
    // can be declared in a unit other than the one emitting the base's
    // struct, so this runs across all units at once; an empty set means no
    // unit's class overridden anywhere in the program.
    std::vector<const Program*> asts;
    asts.reserve(units.size());
    std::unordered_set<const Program*> seen;
    for (const CheckedModuleUnit& checked : units) {
        if (!checked.unit || !checked.unit->ast) continue;
        const Program* ast = checked.unit->ast.get();
        if (seen.insert(ast).second) asts.push_back(ast);
    }
    const std::unordered_set<std::string> virtualSet =
        detail::buildVirtualMethodSet(asts);
    // v0.2.5 Phase 2: the CLI-validated fragment cache (Incremental only,
    // pinned by env var). Byte-identical fragments reuse the unit's text;
    // the shared header stays byte-identical and the build manifest keeps
    // reusing objects.
    std::unique_ptr<detail::FragmentCache> fragmentCache(
        detail::loadFragmentCache());
    if (fragmentCache) {
        fragmentCache_ = fragmentCache.get();
        fragmentCachePath_ =
            std::getenv("CP3_INCREMENTAL_FRAGMENTS") != nullptr
                ? std::getenv("CP3_INCREMENTAL_FRAGMENTS")
                : std::string{};
        fragmentOut_.clear();
    }
    ProgramAssembly assembly;
    for (CompilationUnit* unit : graph.compilationOrder()) {
        if (unit == nullptr || unit->ast == nullptr) continue;
        if (unit->isPrelude && !unit->isEntry) continue;
        // A cached fragment is used even when the unit's sema is missing,
        // but only through the splice inside emitProgramUnit; the default
        // path still requires sema.
        if (link.semas.find(unit->id) == link.semas.end() &&
            !fragmentCache_) {
            continue;
        }
        emitProgramUnit(link, *unit, assembly, &virtualSet);
    }
    if (fragmentCache_ != nullptr) {
        // Rewrite the cache with the union of reused and freshly emitted
        // records so stale generations disappear and fresh records carry
        // this build's fingerprints on the next run. Best effort.
        std::unordered_set<std::string> freshKeys;
        freshKeys.reserve(fragmentOut_.size());
        for (const detail::FragmentRecord& rec : fragmentOut_) {
            freshKeys.insert(rec.unitKey);
        }
        for (const CompilationUnit* unit : graph.compilationOrder()) {
            if (unit == nullptr || !unit->ast) continue;
            if (unit->isPrelude && !unit->isEntry) continue;
            if (freshKeys.count(unit->moduleName) != 0) continue;
            const auto it = fragmentCache_->records.find(unit->moduleName);
            if (it != fragmentCache_->records.end()) {
                fragmentOut_.push_back(it->second);
            }
        }
        if (!fragmentCachePath_.empty()) {
            detail::writeFragmentCache(fragmentCachePath_, fragmentOut_);
        }
    }
    fragmentCache_ = nullptr;
    fragmentCachePath_.clear();
    fragmentOut_.clear();
    return assembly;
}

GeneratedModule Generator::runProgram(
    const ModuleGraph& graph, const std::vector<CheckedModuleUnit>& units) {
    ProgramAssembly assembly = buildAssembly(graph, units);
    return assembleProgram(assembly, moduleName_);
}

GeneratedProgramModules Generator::runProgramModules(
    const ModuleGraph& graph, const std::vector<CheckedModuleUnit>& units) {
    ProgramAssembly assembly = buildAssembly(graph, units);
    return assembleProgramModules(assembly, moduleName_);
}

void Generator::emitProgramUnit(const ProgramLink& link,
                                const CompilationUnit& unit,
                                ProgramAssembly& assembly,
                                const std::unordered_set<std::string>* virtualSet) {
    // v0.2.5 Phase 2: splice a verified fragment when one exists. The CLI
    // already validated the full key (source bytes, interface fingerprint,
    // program shape, toolchain + profile); this re-verifies the cheap,
    // locally-computable parts and the isEntry/isPackage flags. A miss falls
    // through to the fresh emit path below.
    if (fragmentCache_ != nullptr) {
        const auto recIt = fragmentCache_->records.find(unit.moduleName);
        if (recIt != fragmentCache_->records.end()) {
            const detail::FragmentRecord& rec = recIt->second;
            if (rec.sourceHash == hex64u(detail::fragmentSourceHashOf(unit)) &&
                rec.isEntry == unit.isEntry &&
                rec.isPackage == unit.isPackage) {
                if (const char* dbg = std::getenv("CP3_INCREMENTAL_DEBUG");
                    dbg != nullptr && *dbg != '\0') {
                    std::fprintf(stderr,
                                 "cp3c: incremental reuse: %s\n",
                                 unit.moduleName.c_str());
                }
                const auto surfaceIt = link.surfaces.find(unit.id);
                ProgramUnitText text;
                text.namesp = surfaceIt != link.surfaces.end()
                                  ? surfaceIt->second.namesp
                                  : suggestNamespace(unit.moduleName);
                text.sourceKey = unit.moduleName;
                text.isEntry = unit.isEntry;
                text.isPackage = unit.isPackage;
                text.globals = rec.globals;
                text.decls = rec.decls;
                text.defs = rec.defs;
                text.mainBody = rec.mainBody;
                text.topLevelAwait = rec.topLevelAwait;
                ProgramNeeds& n = assembly.needs;
                applyNeedsMask(n, rec.needsMask);
                for (const std::string& header : rec.nativeHeaders) {
                    appendUnique(n.nativeHeaders, header);
                }
                if (unit.isEntry) {
                    for (const WorkerSite& site : options_.workers) {
                        if (site.inlineSite || site.headerPath.empty()) {
                            continue;
                        }
                        appendUnique(n.workerHeaders, site.headerPath);
                    }
                }
                assembly.units.push_back(std::move(text));
                return;
            }
        }
    }
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
    inner.virtualMethodSet_ = virtualSet;
    const auto importsIt = link.imports.find(unit.id);
    if (importsIt != link.imports.end()) inner.linkedNames_ = importsIt->second;
    ProgramUnitText text;
    const auto surfaceIt = link.surfaces.find(unit.id);
    text.namesp = surfaceIt != link.surfaces.end()
                      ? surfaceIt->second.namesp
                      : suggestNamespace(unit.moduleName);
    text.sourceKey = unit.moduleName;
    text.isEntry = unit.isEntry;
    text.isPackage = unit.isPackage;
    inner.runUnit(*unit.ast, text.decls, text.defs, text.mainBody,
                  text.globals);
    text.topLevelAwait = inner.topLevelAwait_;
    // v0.2.5 Phase 2: capture the needs mask this unit produced, so the
    // recorded fragment can replay these bits when this unit is reused.
    std::uint64_t needsMask = 0;
    needsMask |= inner.consoleStderr_ ? (std::uint64_t{1} << 0) : 0;
    needsMask |= inner.needsConsoleAbi_ ? (std::uint64_t{1} << 1) : 0;
    needsMask |= (inner.needsEventLoop_ || inner.topLevelAwait_)
                     ? (std::uint64_t{1} << 2)
                     : 0;
    needsMask |= inner.needsArith_ ? (std::uint64_t{1} << 3) : 0;
    needsMask |= inner.needsMath_ ? (std::uint64_t{1} << 4) : 0;
    needsMask |= inner.needsNumber_ ? (std::uint64_t{1} << 5) : 0;
    needsMask |= inner.needsString_ ? (std::uint64_t{1} << 6) : 0;
    needsMask |= inner.needsPath_ ? (std::uint64_t{1} << 7) : 0;
    needsMask |= inner.needsOs_ ? (std::uint64_t{1} << 8) : 0;
    needsMask |= inner.needsJson_ ? (std::uint64_t{1} << 9) : 0;
    needsMask |= inner.needsJsonFile_ ? (std::uint64_t{1} << 10) : 0;
    needsMask |= inner.needsProcess_ ? (std::uint64_t{1} << 11) : 0;
    needsMask |= inner.needsEnv_ ? (std::uint64_t{1} << 12) : 0;
    needsMask |= inner.needsCrypto_ ? (std::uint64_t{1} << 13) : 0;
    needsMask |= inner.needsDate_ ? (std::uint64_t{1} << 14) : 0;
    needsMask |= inner.needsUrl_ ? (std::uint64_t{1} << 15) : 0;
    needsMask |= inner.needsTime_ ? (std::uint64_t{1} << 16) : 0;
    needsMask |= inner.needsHash_ ? (std::uint64_t{1} << 17) : 0;
    needsMask |= inner.needsText_ ? (std::uint64_t{1} << 18) : 0;
    needsMask |= inner.needsReadline_ ? (std::uint64_t{1} << 19) : 0;
    needsMask |= inner.needsNetwork_ ? (std::uint64_t{1} << 20) : 0;
    needsMask |= inner.needswebserverSettings_ ? (std::uint64_t{1} << 21) : 0;
    needsMask |= inner.needsTypedArray_ ? (std::uint64_t{1} << 22) : 0;
    needsMask |= inner.needsCollections_ ? (std::uint64_t{1} << 23) : 0;
    needsMask |= inner.needsBuffer_ ? (std::uint64_t{1} << 24) : 0;
    needsMask |= inner.needsFs_ ? (std::uint64_t{1} << 25) : 0;
    needsMask |= inner.needsIo_ ? (std::uint64_t{1} << 26) : 0;
    needsMask |= inner.needsTuple_ ? (std::uint64_t{1} << 27) : 0;
    needsMask |= inner.needsEventEmitter_ ? (std::uint64_t{1} << 28) : 0;
    needsMask |= inner.needsBitwise_ ? (std::uint64_t{1} << 29) : 0;
    needsMask |= inner.needsUnion_ ? (std::uint64_t{1} << 30) : 0;
    needsMask |= inner.needsObject_ ? (std::uint64_t{1} << 31) : 0;
    needsMask |= inner.needsRegex_ ? (std::uint64_t{1} << 32) : 0;
    needsMask |= inner.needsParallel_ ? (std::uint64_t{1} << 33) : 0;
    needsMask |= inner.needsWorker_ ? (std::uint64_t{1} << 34) : 0;

    ProgramNeeds& n = assembly.needs;
    n.consoleStderr |= inner.consoleStderr_;
    n.consoleAbi |= inner.needsConsoleAbi_;
    n.eventLoop |= inner.needsEventLoop_ || inner.topLevelAwait_;
    n.arith |= inner.needsArith_;
    n.math |= inner.needsMath_;
    n.number |= inner.needsNumber_;
    n.str |= inner.needsString_;
    n.path |= inner.needsPath_;
    n.os |= inner.needsOs_;
    n.json |= inner.needsJson_;
    n.jsonFile |= inner.needsJsonFile_;
    n.process |= inner.needsProcess_;
    n.env |= inner.needsEnv_;
    n.crypto |= inner.needsCrypto_;
    n.date |= inner.needsDate_;
    n.url |= inner.needsUrl_;
    n.time |= inner.needsTime_;
    n.hash |= inner.needsHash_;

    n.text |= inner.needsText_;
    n.readline |= inner.needsReadline_;
    n.network |= inner.needsNetwork_;
    n.webserverSettings |= inner.needswebserverSettings_;
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
    if (fragmentCache_ != nullptr) {
        detail::FragmentRecord rec;
        rec.unitKey = unit.moduleName;
        rec.sourceHash = hex64u(detail::fragmentSourceHashOf(unit));
        rec.fragmentSourceKey = unit.moduleName;
        rec.namesp = text.namesp;
        rec.globals = text.globals;
        rec.decls = text.decls;
        rec.defs = text.defs;
        rec.mainBody = text.mainBody;
        rec.needsMask = needsMask;
        rec.nativeHeaders = inner.nativeHeaders_;
        rec.isEntry = unit.isEntry;
        rec.isPackage = unit.isPackage;
        rec.topLevelAwait = text.topLevelAwait;
        fragmentOut_.push_back(std::move(rec));
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

GeneratedProgramModules generateProgramModules(const GenerateProgramInput& input) {
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
    return generator.runProgramModules(input.graph, input.units);
}

}  // namespace cp3
