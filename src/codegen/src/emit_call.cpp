// =============================================================================
// src/codegen/src/emit_call.cpp — codegen call lowering + console.* (STYLE.md §8).
//
// The `CallExpr` dispatcher and the `console.*` surface:
//
//   f(...)                     -> `f(...)` (functions declared in this unit)
//   setTimeout/sleep/...        -> emit_async.cpp's timer/promise lowering
//   Promise.all/... / p.then()  -> emit_async.cpp's promise lowering
//   console.log/info(...)      -> `cp3::runtime::println(...)`   (stdout)
//   console.warn/error(...)    -> `::cp3::runtime::console::warn/error(...)`
//                                 (stderr, STDLIB docs §07)
//
// A callee that is neither a user function nor a lowered intrinsic reports
// CP3002 rather than emitting a call to an undeclared C++ name.
// =============================================================================
#include "generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cpp_writer.h"
#include "mangle.h"
#include "type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The core timer builtins (spec §14 / STDLIB docs §03): bare globals lowered
// onto cp3/runtime.hpp + cp3/promise.hpp, no import required.
bool isTimerBuiltinName(const std::string& name) {
    return name == "setTimeout" || name == "setInterval" ||
           name == "clearTimeout" || name == "clearInterval" || name == "sleep";
}

}  // namespace

std::string Generator::emitCall(const CallExpr& c) {
    if (isConsoleCall(c, "log") || isConsoleCall(c, "info")) {
        return emitConsoleLog(c);
    }
    if (isConsoleCall(c, "warn") || isConsoleCall(c, "error")) {
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        return emitConsoleMethod(c, m.property.c_str());
    }
    if (isConsoleMember(c)) {
        notImplemented(c, "console method (only log/info/warn/error are supported)");
        return "0.0";
    }
    if (c.callee->kind == NodeKind::Identifier) {
        const std::string name = static_cast<const Identifier&>(*c.callee).name;
        if (functions_.find(name) == functions_.end() && isTimerBuiltinName(name)) {
            return emitTimerBuiltin(c, name);
        }
        if (name == "worker") {
            notImplemented(c, "worker(...)");
            return "0.0";
        }
        if (!c.typeArgs.empty()) {
            notImplemented(c, "generic call");
            return "0.0";
        }
        if (functions_.find(name) == functions_.end()) {
            // Not a function declared in this unit and not an intrinsic we
            // lower (console.* is handled above): treat it as out of scope
            // rather than emitting a call to an undeclared C++ name.
            notImplemented(c, "call to function '" + name + "' not declared in this unit");
            return "0.0";
        }
        if (asyncFunctions_.count(name) != 0) {
            // Calling an async function starts async work whose continuations
            // only run when the loop does.
            needsEventLoop_ = true;
        }
        std::vector<std::string> args;
        for (const auto& arg : c.arguments) {
            if (!arg) { args.push_back("0.0"); continue; }
            if (arg->kind == NodeKind::SpreadElement) {
                notImplemented(*arg, "spread argument");
                continue;
            }
            args.push_back(expr(*arg));
        }
        return mapIdent(name) + "(" + joinStrings(args, ", ") + ")";
    }
    if (c.callee->kind == NodeKind::MemberExpr) {
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        if (m.object && m.object->kind == NodeKind::Identifier &&
            static_cast<const Identifier&>(*m.object).name == "parallel") {
            notImplemented(c, "parallel.*");
            return "0.0";
        }
        // `Promise.all/race/allSettled(...)`.
        if (m.object && m.object->kind == NodeKind::Identifier &&
            static_cast<const Identifier&>(*m.object).name == "Promise") {
            return emitPromiseStatic(c, m);
        }
        // `p.then(...)` / `p.catch(...)` / `p.finally(...)` on a Promise.
        if (m.object && detail::isPromiseType(effectiveType(*m.object))) {
            return emitPromiseMemberCall(c, m);
        }
        notImplemented(c, "method call");
        return "0.0";
    }
    notImplemented(c, "call expression");
    return "0.0";
}

bool Generator::isConsoleMember(const CallExpr& c) const {
    if (c.callee->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    return m.object && m.object->kind == NodeKind::Identifier &&
           static_cast<const Identifier&>(*m.object).name == "console";
}

bool Generator::isConsoleCall(const CallExpr& c, const char* method) const {
    if (c.callee->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    return isConsoleMember(c) && m.property == method;
}

std::string Generator::emitConsoleLog(const CallExpr& c) {
    if (!consoleImported_) {
        notImplemented(c, "console used without `import console from \"std/console\"`");
        return "0.0";
    }
    if (c.arguments.empty()) {
        return "cp3::runtime::println(std::string())";
    }
    std::string joined = tsCall(*c.arguments.front());
    for (std::size_t i = 1; i < c.arguments.size(); ++i) {
        joined += " + std::string(\" \") + " + tsCall(*c.arguments[i]);
    }
    return "cp3::runtime::println(" + joined + ")";
}

std::string Generator::emitConsoleMethod(const CallExpr& c, const char* method) {
    if (!consoleImported_) {
        notImplemented(c, "console used without `import console from \"std/console\"`");
        return "0.0";
    }
    // `.warn` / `.error` write to stderr (STDLIB docs §07); the runtime renders
    // the arguments itself (cp3::runtime::console::renderAll).
    consoleStderr_ = true;
    std::vector<std::string> args;
    for (const auto& arg : c.arguments) {
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            notImplemented(*arg, "spread argument");
            continue;
        }
        args.push_back(expr(*arg));
    }
    return "::cp3::runtime::console::" + std::string(method) + "(" +
           joinStrings(args, ", ") + ")";
}

}  // namespace cp3
