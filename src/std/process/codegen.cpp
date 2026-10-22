// =============================================================================
// src/std/process/codegen.cpp - `process.*` lowering.
//
// OWNERSHIP: the std/process family (src/std/process/**). Lowers the
// documented surface (STDLIB docs §04, process section) onto
// cp3/std/process.hpp:
//
//   process.exit(code)      -> ::cp3::runtime::process::exit(code)
//   process.argv            -> ::cp3::runtime::process::argv()
//   process.argv.length     -> argv()->size()
//   process.env             -> ::cp3::runtime::process::envMap()
//                             (the `Map<string, string>`; its members lower
//                             through the std/collections tables,
//                             src/std/collections/codegen.cpp)
//   process.exec(cmd, args) -> Promise<ExecResult> (buffered)
//   process.spawn(cmd, args)-> SpawnHandle (streaming)
//
// The ExecResult fields map stdout -> out and stderr -> err (the C++ struct
// keeps away from the stdio macros); the SpawnHandle methods
// (`on`/`write`/`kill`) lower to the runtime member calls, the listener's
// parameter types chosen by the literal event name ("data" and "stderr"
// chunks are std::string, the "exit" code is double).
//
// `args` lowers to the runtime's std::vector<std::string> (a string[] literal
// or an Array<string> binding). Anything else reports CP3002 and emits a
// compilable stub, the usual graceful-skip path.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/builtin_tables.h"
#include "detail/cpp_writer.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kExecResult = "$process:ExecResult";
constexpr const char* kSpawnHandle = "$process:SpawnHandle";
constexpr const char* kRuntimeNs = "::cp3::runtime::process::";

/// The literal string value of `e` ("" when it is not a string literal).
std::string stringLiteralOf(const Expr* e) {
    if (!e || e->kind != NodeKind::StringLiteral) return std::string();
    return static_cast<const StringLiteral&>(*e).value;
}

/// The documented listener parameter types for one spawn event name. An
/// unidentified event gets no context types: the emitted lambda stays generic
/// and the runtime adapts it (a listener may also take no parameters).
std::vector<std::string> spawnEventParams(const std::string& event) {
    if (event == "exit") return {"double"};
    if (event == "data" || event == "stderr") return {"std::string"};
    return {};
}

}  // namespace

bool Generator::isProcessObject(const Expr* e) const {
    if (e == nullptr) return false;
    const std::string& name = effectiveType(*e).name;
    return name == kExecResult || name == kSpawnHandle;
}

/// `process.exit` plus the two child-process entry points. The calls keep the
/// event loop alive: their results / events are delivered as loop tasks.
std::string Generator::emitProcessCall(const CallExpr& c, const MemberExpr& m) {
    const std::string& prop = m.property;
    if (prop == "exit" && c.arguments.size() == 1) {
        const Expr* arg = c.arguments.front().get();
        if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
        needsProcess_ = true;
        return std::string(kRuntimeNs) + "exit(" + expr(*arg) + ")";
    }
    if ((prop == "exec" || prop == "spawn") && c.arguments.size() == 2) {
        const Expr* cmd = c.arguments[0].get();
        const Expr* args = c.arguments[1].get();
        if (!cmd || !args || cmd->kind == NodeKind::SpreadElement ||
            args->kind == NodeKind::SpreadElement) {
            return std::string();
        }
        needsProcess_ = true;
        needsEventLoop_ = true;  // the result / handle events need the loop
        return std::string(kRuntimeNs) + prop + "(" + expr(*cmd) + ", " +
               processArgs(*args) + ")";
    }
    return std::string();
}

/// Lowers a process argument vector to the runtime's std::vector<std::string>:
/// a string[] literal builds the vector, an Array<string> binding
/// dereferences to the vector behind the handle.
std::string Generator::processArgs(const Expr& e) {
    if (e.kind == NodeKind::ArrayLiteral) {
        const auto& literal = static_cast<const ArrayLiteral&>(e);
        bool spread = false;
        for (const auto& element : literal.elements) {
            if (element && element->kind == NodeKind::SpreadElement) {
                spread = true;
                break;
            }
        }
        if (spread) {
            // `[...a, "b"]`: the elements are spliced at run time into the
            // vector the runtime takes (emit/spread.cpp).
            const std::string text =
                spreadListText(Type::string(), literal.elements);
            if (!text.empty()) return text;
            reportTypeError(e, "the process arguments must be a string[]");
            return "std::vector<std::string>{}";
        }
        std::vector<std::string> elems;
        for (const auto& element : literal.elements) {
            if (!element) continue;
            elems.push_back(expr(*element));
        }
        return "std::vector<std::string>{" + joinStrings(elems, ", ") + "}";
    }
    if (info(e).shape == Shape::Array) {
        return "(*" + expr(e) + ")";
    }
    // The documented surface is `string[]` (docs 04, process section): an
    // argument list the compiler cannot prove to be one is a type error, not
    // a missing feature.
    reportTypeError(e, "the process arguments must be a string[] (an array "
                       "literal or an array binding)");
    return "std::vector<std::string>{}";
}

/// The SpawnHandle member calls: `on(event, listener)`, `write(input)`,
/// `kill()`. A listener arrow gets the runtime parameter types for the
/// literal event name, with its parameter names typed for the body emission
/// (sema keeps callback parameters permissive, the network convention).
std::string Generator::emitProcessObjectCall(const CallExpr& c,
                                             const MemberExpr& m) {
    if (!m.object || effectiveType(*m.object).name != kSpawnHandle) {
        return std::string();
    }
    if (m.property == "kill" && c.arguments.empty()) {
        needsProcess_ = true;
        needsEventLoop_ = true;
        return "(" + expr(*m.object) + ").kill()";
    }
    if (m.property == "write" && c.arguments.size() == 1) {
        const Expr* arg = c.arguments.front().get();
        if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
        needsProcess_ = true;
        needsEventLoop_ = true;
        return "(" + expr(*m.object) + ").write(" + expr(*arg) + ")";
    }
    if (m.property != "on" || c.arguments.size() != 2) return std::string();
    const Expr* event = c.arguments[0].get();
    const Expr* listener = c.arguments[1].get();
    if (!event || !listener || event->kind == NodeKind::SpreadElement ||
        listener->kind == NodeKind::SpreadElement) {
        return std::string();
    }
    needsProcess_ = true;
    needsEventLoop_ = true;
    const std::string eventName = stringLiteralOf(event);
    const std::vector<std::string> context = spawnEventParams(eventName);
    const std::string call = "(" + expr(*m.object) + ").on(" + expr(*event) + ", ";
    if (listener->kind != NodeKind::ArrowFunction) {
        return call + expr(*listener) + ")";
    }
    const auto& arrow = static_cast<const ArrowFunction&>(*listener);
    if (eventName.empty()) {
        // A computed event name: the payload type depends on the name
        // ("data"/"stderr" -> a string chunk, "exit" -> the exit code), so
        // only a listener that takes no payload can be lowered. The runtime
        // dispatches that one on the name it sees at run time.
        if (!arrow.params.empty()) {
            reportTypeError(*event, "the spawn event name must be a string "
                                    "literal (\"data\", \"stderr\" or "
                                    "\"exit\"): the listener's payload type "
                                    "depends on it");
            return "0.0";
        }
    }
    pushScope();
    for (std::size_t i = 0; i < arrow.params.size() && i < context.size(); ++i) {
        const Parameter* p = arrow.params[i].get();
        if (!p || !p->name || p->name->kind != NodeKind::Identifier) continue;
        if (context[i] == "std::string") {
            declare(static_cast<const Identifier&>(*p->name).name, Type::string());
        } else if (context[i] == "double") {
            declare(static_cast<const Identifier&>(*p->name).name, Type::number());
        }
    }
    std::string text = call + emitArrow(arrow, context) + ")";
    popScope();
    return text;
}

// The `process.argv` / `process.argv.length` / `process.env` property reads
// and the ExecResult field reads (`result.stdout` / `.stderr` / `.exitCode`).
// Returns "" when `m` is none of them, so the caller keeps its CP3002
// fallthrough.
std::string Generator::emitProcessMember(const MemberExpr& m) {
    if (isNsMember(&m, "process", "argv")) {
        needsProcess_ = true;
        return std::string(kRuntimeNs) + "argv()";
    }
    if (m.property == "length" && isNsMember(m.object.get(), "process", "argv")) {
        needsProcess_ = true;
        return "static_cast<double>(" + std::string(kRuntimeNs) + "argv()->size())";
    }
    if (isNsMember(&m, "process", "env")) {
        // docs §04: `Map<string, string>`; its members lower through
        // src/std/collections/codegen.cpp.
        needsProcess_ = true;
        needsCollections_ = true;
        return std::string(kRuntimeNs) + "envMap()";
    }
    if (!m.object || !isProcessObject(m.object.get())) return std::string();
    if (effectiveType(*m.object).name != kExecResult) return std::string();
    std::string field;
    if (m.property == "stdout") {
        field = "out";
    } else if (m.property == "stderr") {
        field = "err";
    } else if (m.property == "exitCode") {
        field = "exitCode";
    } else {
        return std::string();
    }
    needsProcess_ = true;
    return "(" + expr(*m.object) + ")." + field;
}

}  // namespace cp3
