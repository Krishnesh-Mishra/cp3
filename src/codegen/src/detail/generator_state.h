// =============================================================================
// src/codegen/src/detail/generator_state.h - PRIVATE `Generator` state storage.
//
// The per-unit state of `Generator` (detail/generator.h): the int64 fast-path
// state, the diagnostic sink and sema out-params, the emitted-include flags,
// the registered JSON/enum/worker shapes and the type environment. It lives in
// its own header so `generator.h` (and the `Generator` type-size) stay within
// the source budgets (STYLE.md sec.2); `Generator` inherits the block privately,
// so every emit unit reads the members under their historic names. This header
// is implementation detail: it lives under `src/codegen/src/detail/` and is
// never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "detail/captures.h"
#include "detail/enums.h"
#include "detail/mangle.h"
#include "detail/program_link.h"
#include "detail/type_map.h"

namespace cp3 {

// The int64 fast-path state (D64): non-null exactly while a function's fast
// body is being attempted (`Generator::intMode_`). `failed` means the attempt
// cannot be proven exact and must be discarded, in which case the ordinary
// double body is the only lowering.
struct IntMode {
    bool failed = false;
    // Names read somewhere outside a `return` argument (int_reads.cpp);
    // `nullopt` when that walk could not see the whole body. A name absent here
    // is read only by `return`, so its bound check can be deferred from the
    // operation to that single observation point.
    std::optional<std::unordered_set<std::string>> readOutsideReturns;
    // Locals whose bound check is currently deferred: their value may sit
    // outside [-2^53, 2^53] until `arith::pass` re-checks it at a read.
    std::unordered_set<std::string> deferred;
    // Set while emitting a statement's own expression: the one position where
    // an assignment's value is discarded, so a raw store is allowed.
    bool stmtExpr = false;
    // Divisibility facts in force on the current straight-line branch path.
    // While `valid` is true, `name % mod == 0` was tested true and `name` has
    // not been written since, so `idiv(name, mod)` cannot hit its remainder
    // check. A write invalidates its facts in place (it never removes them) so
    // a branch exit can truncate by depth without shifting a live fact.
    struct DivisibleFact {
        std::string name;
        std::int64_t mod = 0;
        bool valid = true;
    };
    std::vector<DivisibleFact> divisible;
};

// One entry-body `number` binding proven to stay in a small integral range for
// its whole life (support/top_int64.cpp). Keyed by source name; `alias` is the
// fresh std::int64_t spelling the emitter uses and every read converts back
// with static_cast<double> (exact under the proof).
struct TopInt64Binding {
    std::string alias;
    std::uint32_t declNode = 0;  // the declarator this record was proven for
    std::int64_t start = 0;
    // R1: `while (c < N) { ...; c = c + K; }`, K > 0, N + K <= 2^53.
    bool counter = false;
    bool inclusive = false;      // the condition is `<=` (otherwise `<`)
    std::int64_t bound = 0;      // N
    std::int64_t step = 1;       // K
    std::uint32_t loopNode = 0;       // the while statement to rewrite
    std::uint32_t incrementNode = 0;  // its last statement (consumed by the for)
    // R2: `x = (x * mul) % mod`, x0 in [0, mod), mul > 0, mul * mod <= 2^53.
    std::int64_t mul = 0;
    std::int64_t mod = 0;
    std::uint32_t writeNode = 0;  // the one assignment
};

// One active breakable statement (loop or switch). `break` and `continue`
// lower to these labels; a JS label names one frame until its statement ends.
struct FlowFrame {
    bool isLoop = false;
    std::string breakLabel;
    std::string continueLabel;
};

// One active JS label attached to a statement. A labeled `continue` requires a
// loop frame (the label on a switch may only be broken out of).
struct LabelFrame {
    std::string name;
    bool isLoop = false;
    std::string breakLabel;
    std::string continueLabel;
};

// The state block of `Generator`; see the class comment in generator.h. The
// constructor captures the generator's fixed wiring (the sink, the sema side
// tables, the unit name and options); the remaining members initialize
// themselves (their historic default member initializers). All members keep
// their historic names (the class inherits the block privately and the emit
// units access it unchanged).
struct GeneratorState {
    GeneratorState(DiagnosticSink& sink, const SemaResult& sema,
                   const std::string& moduleName, bool lineMapping,
                   const CodegenOptions& options);

protected:
    // The int64 fast-path state (D64); see `struct IntMode` above.
    std::unique_ptr<IntMode> intMode_;

    DiagnosticSink& sink_;
    const SemaResult& sema_;
    std::string moduleName_;
    std::string namesp_;
    CodegenOptions options_;  // worker import / worker-module mode

    bool consoleImported_ = false;
    // A declared function, class, export or worker reads module bindings from
    // namespace scope: keep them globals. When false, the entry function is the
    // only reader and they become `main` locals (with an int64 rewrite proof).
    bool moduleNeedsGlobals_ = false;
    bool lineMapping_ = false;     // --debug: emit `#line` into generated code
    bool consoleStderr_ = false;   // console.warn/.error used (stderr output)
    bool needsConsoleAbi_ = false; // console async/misc members -> family header
    bool needsEventLoop_ = false;  // async/timers: emit runEventLoop() in main
    bool needsArith_ = false;      // `%`/int64 lowering -> cp3/core/arith.hpp include
    bool suppressDiagnostics_ = false;  // an int64 attempt must not report
    bool needsMath_ = false;       // Math.* -> cp3/std/math.hpp include
    bool needsString_ = false;     // string methods -> cp3/std/string.hpp include
    bool needsPath_ = false;       // path.* -> cp3/std/path.hpp include
    bool needsOs_ = false;         // os.* -> cp3/std/os.hpp include
    bool needsJson_ = false;       // JSON.stringify/parse -> cp3/std/json.hpp include
    // The registered JSON record shapes (usage-based reification): per
    // interface name the C++ struct text, the generated builder body and the
    // declared field names, plus the sema shape label -> interface name. The
    // structs materialize into the decls section on first use only.
    std::unordered_map<std::string, std::string> jsonShapeStructs_;
    std::unordered_map<std::string, std::string> jsonShapeBuilders_;
    std::unordered_map<std::string, std::vector<std::string>> jsonShapeFields_;
    std::unordered_map<std::string, std::string> jsonShapeByLabel_;
    std::unordered_set<std::string> jsonShapeUsed_;
    std::string jsonShapeDecls_;
    // The unit's enums (collectEnums): the resolved model per declaration name,
    // the accumulated `enum class` decls text, and the sema object label ->
    // field names of every enum-carrying object literal that lowered to a
    // `std::tuple` (the field read is `std::get<i>`).
    std::unordered_map<std::string, detail::EnumCppInfo> enums_;
    std::string enumDecls_;
    std::unordered_map<std::string, std::vector<std::string>> enumObjectFields_;
    bool needsProcess_ = false;    // process.* -> cp3/std/process.hpp include
    bool needsEnv_ = false;        // { env } -> cp3/std/env.hpp include
    bool needsCrypto_ = false;     // crypto.* -> cp3/std/crypto.hpp include
    bool needsDate_ = false;       // std/date -> cp3/std/date.hpp include
    bool needsRegex_ = false;      // regex literals -> cp3/core/regex.hpp include
    bool needsNetwork_ = false;    // std/network -> cp3/std/network.hpp include
    // D79: this unit opted into `createServer({ threads })`. Namespace-scope
    // module bindings become `thread_local`, and their runtime initializers are
    // replayed on every loop thread through `cp3_init_module_state()`.
    bool scaleMode_ = false;
    std::vector<std::string> moduleInitLines_;
    bool needsFs_ = false;         // std/fs -> cp3/std/fs.hpp include
    bool needsTypedArray_ = false; // the family -> cp3/typed_array.hpp include
    bool needsCollections_ = false;  // std/collections -> cp3/std/collections.hpp
    bool needsBuffer_ = false;       // std/buffer -> cp3/std/buffer.hpp include
    bool needsTuple_ = false;        // tuple value (enum object / generic tuple) -> <tuple>
    bool needsEventEmitter_ = false;  // EventEmitter/Listener -> cp3/async/emitter.hpp
    bool needsBitwise_ = false;       // bitwise/shift ops -> cp3/core/bitwise.hpp
    bool needsUnion_ = false;         // primitive unions -> cp3/core/union.hpp
    bool needsObject_ = false;        // object value shapes -> the object helper block
    bool needsRef_ = false;           // classes/objects -> cp3/core/value.hpp include
    bool needsParallel_ = false;      // parallel.* -> cp3/parallel/parallel.hpp
    bool needsWorker_ = false;        // new Worker(...) -> cp3/parallel/worker.hpp
    bool workerModule_ = false;       // this unit IS the worker module
    // The entry unit's resolved worker sites, keyed by the `NewExpr` node id:
    // the file sites from `options.workers` plus the inline sites the pre-scan
    // synthesizes (src/emit/worker.cpp). `inlineWorkerOrder_` keeps the inline
    // entry functions' emission order deterministic.
    std::unordered_map<std::uint32_t, WorkerSite> workerSites_;
    std::vector<std::uint32_t> inlineWorkerOrder_;
    // The inline form's entry arrow per site (needed to emit its function).
    std::unordered_map<std::uint32_t, const ArrowFunction*> inlineWorkerArrows_;
    bool topLevelAwait_ = false;   // an `await` appeared at top level
    bool inAsyncBody_ = false;     // current function body lowers to a coroutine
    int nestingDepth_ = 0;         // function + arrow nesting (0 == top level)
    int uniqueCounter_ = 0;        // fresh names for generated scaffolding
    std::string currentReturn_ = "int";
    // The cp3 type of the value the enclosing function/lambda returns, and the
    // type the context expects of the next expression (a binding annotation, a
    // call argument, an object field, a return type). Contextual typing fixes
    // an otherwise untyped arrow's parameters and return value.
    std::optional<Type> currentReturnType_;
    std::optional<Type> expected_;
    // The closure-capture analysis (support/captures.cpp): the shared cells of
    // every frame, and the arrow each `let f = (...) => ...` binding names.
    detail::CaptureInfo captures_;
    std::uint32_t currentFrameId_ = 0;  // Program / function / arrow being emitted
    bool currentReturnTuple_ = false;  // current function returns a std::tuple
    // Structs reifying object-literal arguments to generic functions
    // (support/generic.cpp), prepended to the decls section.
    std::string genericDecls_;

    std::vector<std::unordered_map<std::string, detail::Binding>> env_;
    // The spec 15 object surface: forward declarations (classFwdDecls_), structs
    // and `cp3_ts` overloads (classDecls_, objectDecls_), and out-of-line
    // method definitions (classDefs_).
    std::string classFwdDecls_;
    std::string classDecls_;
    std::string classDefs_;
    std::string objectDecls_;
    std::unordered_set<std::uint32_t> modelEmitted_;
    // Module-scope value names: globals emit at namespace scope and are not in
    // `env_`, so a lowering that must tell a value binding from a type name
    // (a class used as a static receiver) consults this set too.
    std::unordered_set<std::string> moduleValues_;
    // The class whose method/constructor body is currently emitting; `super`
    // lowers against its base (spec 15, D22).
    std::vector<const detail::ClassModel*> classStack_;
    // Codegen type hints for the Map-shaped std/network fields; the open
    // marker hides their type in sema, so networkMapFieldType checks these.
    std::unordered_map<std::uint32_t, Type> exprTypeHints_;
    // Whole-program imports (emit/program.cpp): local spelling -> linked name.
    std::unordered_map<std::string, detail::LinkedName> linkedNames_;
    // Whole-program build: module-link export forms carry no C++ of their own.
    bool programMode_ = false;
    std::unordered_set<std::string> functions_;
    std::unordered_map<std::string, const FunctionDeclaration*> functionDecls_;
    // Entry-body int64 locals: the proven records (by source name), the
    // statements the rewrite consumes, and node-id lookups for the loop and the
    // recurrence assignment.
    std::unordered_map<std::string, TopInt64Binding> topInt64Bindings_;
    std::unordered_set<std::uint32_t> int64SkipStmts_;
    std::unordered_map<std::uint32_t, const TopInt64Binding*> int64Loops_;
    std::unordered_map<std::uint32_t, const TopInt64Binding*> int64Writes_;
    std::unordered_set<std::string> asyncFunctions_;   // `async function` names
    std::vector<std::string> thrownBindings_;          // active `catch (e)` names
    std::vector<FlowFrame> flowStack_;   // innermost breakable statement last
    std::vector<LabelFrame> labelStack_;  // active JS labels (loops + blocks)
    std::unordered_set<std::uint32_t> reported_;
};

}  // namespace cp3
