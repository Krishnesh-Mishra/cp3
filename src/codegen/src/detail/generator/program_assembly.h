// =============================================================================
// src/codegen/src/detail/program_assembly.h - PRIVATE whole-program assembly.
//
// The text model `emit/program.cpp` assembles from: one `ProgramUnitText` per
// unit (its namespace, its declaration/definition/statement sections and its
// flags) plus the `ProgramNeeds` union that selects the includes, the helper
// blocks and the `main()` shape of the generated .cpp. `assembleProgram` turns
// the model into the generated `.hpp`/`.cpp` pair.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/codegen.hpp"

namespace cp3::detail {

// The include/main needs of a whole program: the union over its units (the
// per-unit flags on `Generator` are merged by `emit/program.cpp`).
struct ProgramNeeds {
    bool consoleStderr = false;  // console.warn/.error used (stderr output)
    bool consoleAbi = false;     // console async/misc members -> family header
    bool eventLoop = false;      // async/timers: run the event loop in main
    bool arith = false;          // `%`/int64 lowering -> cp3/core/arith.hpp
    bool math = false;
    bool str = false;            // string methods -> cp3/std/string.hpp
    bool path = false;
    bool os = false;
    bool json = false;
    bool process = false;
    bool env = false;
    bool crypto = false;
    bool date = false;
    bool network = false;
    bool typedArray = false;
    bool collections = false;
    bool buffer = false;
    bool fs = false;
    bool tuple = false;
    bool eventEmitter = false;
    bool bitwise = false;        // bitwise/shift ops -> cp3/core/bitwise.hpp
    bool unionValue = false;     // primitive unions -> cp3/core/union.hpp
    bool objectValue = false;    // object value shapes -> the object helpers
    bool regex = false;          // regex literals -> cp3/core/regex.hpp
    bool parallel = false;       // parallel.* -> cp3/parallel/parallel.hpp
    bool worker = false;         // new Worker(...) -> cp3/parallel/worker.hpp
    std::vector<std::string> workerHeaders;  // generated worker unit headers
    // The native bindings' headers (`import native "id" as alias`).
    std::vector<std::string> nativeHeaders;
};

// One unit's contribution to the assembled translation unit.
struct ProgramUnitText {
    std::string namesp;    // e.g. "cp3_mod_counter"
    std::string globals;   // guarded global `$o{...}` value structs
    std::string decls;     // forward declarations + module-scope globals
    std::string defs;      // function definitions
    std::string mainBody;  // top-level statements (the module initializer)
    bool isEntry = false;  // the entry's body runs in main, not an initializer
    bool isPackage = false;  // declaration-only unit linked from `pak/`
    bool topLevelAwait = false;  // the unit's body needs a coroutine
};

// The assembled program: the units in emission order plus the needs union.
struct ProgramAssembly {
    std::vector<ProgramUnitText> units;
    ProgramNeeds needs;
};

// Builds the generated `.hpp`/`.cpp` pair for `assembly` (emit/program.cpp).
GeneratedModule assembleProgram(const ProgramAssembly& assembly,
                                const std::string& moduleName);

}  // namespace cp3::detail
