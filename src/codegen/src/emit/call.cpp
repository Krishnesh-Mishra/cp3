// =============================================================================
// src/codegen/src/emit/call.cpp - codegen call lowering + console.* (STYLE.md §8).
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
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The core timer builtins (spec §14 / STDLIB docs §03): bare globals lowered
// onto cp3/core/runtime.hpp + cp3/async/promise.hpp, no import required.
bool isTimerBuiltinName(const std::string& name) {
    return name == "setTimeout" || name == "setInterval" ||
           name == "clearTimeout" || name == "clearInterval" || name == "sleep";
}

// A parameter that may absorb an explicit `undefined` argument: it carries a
// default expression, or is declared optional (`a?: T` lowers to
// `std::optional<T> = std::nullopt`).
bool optionalParamAt(const FunctionDeclaration& decl, std::size_t i) {
    if (i >= decl.params.size()) return false;
    const Parameter* p = decl.params[i].get();
    return p && (p->optional || p->defaultValue != nullptr);
}

const Parameter* paramAt(const FunctionDeclaration& decl, std::size_t i) {
    if (i >= decl.params.size()) return nullptr;
    return decl.params[i].get();
}

}  // namespace

// Lowers a user-function call's argument list against the callee's declared
// parameters. An explicit `undefined` for an optional/default parameter is the
// JS "argument absent" value: it lowers to the parameter's default expression
// (or `std::nullopt`) when a later argument follows, and is dropped when
// trailing, so the C++ default applies.
std::vector<std::string> Generator::emitCallArgs(const CallExpr& c,
                                                 const FunctionDeclaration* decl) {
    const std::size_t count = c.arguments.size();
    std::vector<std::string> raw(count);
    std::vector<bool> drop(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        const Expr* arg = c.arguments[i].get();
        if (!arg) { raw[i] = "0.0"; continue; }
        if (arg->kind == NodeKind::SpreadElement) {
            notImplemented(*arg, "spread argument");
            continue;
        }
        if (arg->kind == NodeKind::UndefinedLiteral && decl &&
            optionalParamAt(*decl, i)) {
            drop[i] = true;
            continue;
        }
        raw[i] = expr(*arg);
    }
    std::size_t passed = count;  // one past the last argument that must be passed
    while (passed > 0 && drop[passed - 1]) --passed;
    std::vector<std::string> args;
    args.reserve(passed);
    for (std::size_t i = 0; i < passed; ++i) {
        if (!drop[i]) {
            args.push_back(std::move(raw[i]));
            continue;
        }
        const Parameter* p = paramAt(*decl, i);
        if (p && p->defaultValue) {
            args.push_back(expr(*p->defaultValue));
        } else {
            args.push_back("std::nullopt");
        }
    }
    return args;
}

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
        // `String(value)` (bug 9 surface): the scalar conversion global. A
        // user-declared `String` function wins (checked above).
        if (name == "String" && functions_.find(name) == functions_.end()) {
            if (c.arguments.size() == 1 && c.arguments.front() &&
                c.arguments.front()->kind != NodeKind::SpreadElement) {
                return "::cp3::runtime::toString(" + expr(*c.arguments.front()) +
                       ")";
            }
            notImplemented(c, "String(...)");
            return "0.0";
        }
        if (name == "worker") {
            notImplemented(c, "worker(...)");
            return "0.0";
        }
        if (!c.typeArgs.empty()) {
            notImplemented(c, "generic call");
            return "0.0";
        }
        // A name that is not a function declared in this unit.
        if (functions_.find(name) == functions_.end()) {
            return emitUnresolvedCall(c, name);
        }
        if (asyncFunctions_.count(name) != 0) {
            // Calling an async function starts async work whose continuations
            // only run when the loop does.
            needsEventLoop_ = true;
        }
        const auto declIt = functionDecls_.find(name);
        const FunctionDeclaration* decl =
            declIt != functionDecls_.end() ? declIt->second : nullptr;
        return mapIdent(name) + "(" + joinStrings(emitCallArgs(c, decl), ", ") + ")";
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
        // `Math.*`, `path.*`, `os.*`, `JSON.stringify`, `process.*`, and the
        // primitive `string` method surface (emit_builtin.cpp).
        if (std::string builtin = emitBuiltinCall(c, m); !builtin.empty()) {
            return builtin;
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
