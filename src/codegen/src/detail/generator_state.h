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
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "detail/captures.h"
#include "detail/enums.h"
#include "detail/mangle.h"
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
    bool needsFs_ = false;         // std/fs -> cp3/std/fs.hpp include
    bool needsTypedArray_ = false; // the family -> cp3/typed_array.hpp include
    bool needsCollections_ = false;  // std/collections -> cp3/std/collections.hpp
    bool needsBuffer_ = false;       // std/buffer -> cp3/std/buffer.hpp include
    bool needsTuple_ = false;        // enum-carrying object literal -> <tuple>
    bool needsEventEmitter_ = false;  // EventEmitter/Listener -> cp3/async/emitter.hpp
    bool needsBitwise_ = false;       // bitwise/shift ops -> cp3/core/bitwise.hpp
    bool needsUnion_ = false;         // primitive unions -> cp3/core/union.hpp
    bool needsObject_ = false;        // object value shapes -> the object helper block
    bool needsParallel_ = false;      // parallel.* -> cp3/parallel/parallel.hpp
    bool needsWorker_ = false;        // worker(...) -> cp3/parallel/worker.hpp
    bool workerModule_ = false;       // this unit IS the worker module
    // Worker-object pre-scan state: the object-literal node ids handed to
    // `worker(...)` (or bound to a never-assigned binding), and the sema object
    // labels that lowered to a `std::tuple` (the read/write hooks key on these).
    std::unordered_set<std::uint32_t> workerLiterals_;
    std::unordered_set<std::string> workerObjectLabels_;
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

    std::vector<std::unordered_map<std::string, detail::Binding>> env_;
    // Codegen type hints for the Map-shaped std/network fields; the open
    // marker hides their type in sema, so networkMapFieldType checks these.
    std::unordered_map<std::uint32_t, Type> exprTypeHints_;
    std::unordered_set<std::string> functions_;
    std::unordered_map<std::string, const FunctionDeclaration*> functionDecls_;
    std::unordered_set<std::string> asyncFunctions_;   // `async function` names
    std::vector<std::string> thrownBindings_;          // active `catch (e)` names
    std::unordered_set<std::uint32_t> reported_;
};

}  // namespace cp3
