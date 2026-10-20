// =============================================================================
// src/codegen/src/emit/call.cpp - codegen call lowering (STYLE.md §8).
//
// The `CallExpr` dispatcher:
//
//   f(...)                     -> `f(...)` (functions declared in this unit)
//   setTimeout/sleep/...        -> emit_async.cpp's timer/promise lowering
//   Promise.all/... / p.then()  -> emit_async.cpp's promise lowering
//   new EventEmitter() / e.on/once/off/emit(...)  -> the builtin's lowering
//                                 (cp3/async/emitter.hpp, D74)
//   console.<member>(...)      -> the std/console family's lowering unit
//                                 (src/std/console/codegen.cpp, D72); this
//                                 file only recognises the `console.` receiver.
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

// A call to a local module's imported binding (emit/program.cpp's link table):
// the target unit's qualified C++ name, with the callee's declared parameters
// applied to optional/default arguments. A name that is not linked (or is a
// function of this unit) returns "" so the caller keeps its other lowerings.
// A link miss reports CP3002 here, at the use site.
std::string Generator::emitLinkedCall(const CallExpr& c,
                                      const std::string& name) {
    if (functions_.count(name) != 0) return std::string();
    const auto it = linkedNames_.find(name);
    if (it == linkedNames_.end()) return std::string();
    const LinkedName& ln = it->second;
    if (ln.cpp.empty()) {
        // The canonical worker import's default export is an anonymous arrow:
        // the ambient `worker(...)` lowering owns the call (spec 13).
        if (ln.fn == nullptr && name == "worker") return std::string();
        notImplemented(c, ln.miss);
        return "0.0";
    }
    if (ln.fn == nullptr) {
        notImplemented(c, "this call");
        return "0.0";
    }
    if (!c.typeArgs.empty()) {
        notImplemented(c, "calls with type arguments");
        return "0.0";
    }
    if (ln.isAsync) {
        // Calling an async function starts async work whose continuations only
        // run when the loop does.
        needsEventLoop_ = true;
    }
    return ln.cpp + "(" + joinStrings(emitCallArgs(c, ln.fn), ", ") + ")";
}

// The link miss for a member access whose object is an imported binding with
// no C++ lowering (a namespace import/re-export), reported at the use site.
std::string Generator::linkedMemberMiss(const MemberExpr& m) const {
    if (!m.object || m.object->kind != NodeKind::Identifier) return std::string();
    const auto linked = linkedNames_.find(
        static_cast<const Identifier&>(*m.object).name);
    if (linked == linkedNames_.end() || !linked->second.cpp.empty()) {
        return std::string();
    }
    return linked->second.miss;
}

std::string Generator::emitIdentifierCall(const CallExpr& c,
                                          const std::string& name) {
    if (std::string linked = emitLinkedCall(c, name); !linked.empty()) {
        return linked;
    }
    if (functions_.find(name) == functions_.end() && isTimerBuiltinName(name)) {
        return emitTimerBuiltin(c, name);
    }
    // `String(value)` (bug 9 surface): the scalar conversion global. A
    // user-declared `String` function wins (checked above).
    if (name == "String" && functions_.find(name) == functions_.end()) {
        if (c.arguments.size() == 1 && c.arguments.front() &&
            c.arguments.front()->kind != NodeKind::SpreadElement) {
            return "::cp3::runtime::toString(" + expr(*c.arguments.front()) + ")";
        }
        notImplemented(c, "String(...)");
        return "0.0";
    }
    if ((name == "parseInt" || name == "parseFloat") &&
        functions_.find(name) == functions_.end()) {
        return emitParseBuiltin(c, name);
    }
    if (name == "worker" && functions_.find(name) == functions_.end()) {
        // The ambient `worker(...)` lowering (spec 13); a user-declared
        // `worker` function wins (checked here, before the disambiguation).
        return emitWorkerCall(c);
    }
    if (!c.typeArgs.empty()) {
        notImplemented(c, "calls with type arguments");
        return "0.0";
    }
    // A name that is not a function declared in this unit.
    if (functions_.find(name) == functions_.end()) {
        return emitUnresolvedCall(c, name);
    }
    if (asyncFunctions_.count(name) != 0) {
        // Calling an async function starts async work whose continuations only
        // run when the loop does.
        needsEventLoop_ = true;
    }
    const auto declIt = functionDecls_.find(name);
    const FunctionDeclaration* decl =
        declIt != functionDecls_.end() ? declIt->second : nullptr;
    return mapIdent(name) + "(" + joinStrings(emitCallArgs(c, decl), ", ") + ")";
}

std::string Generator::emitCall(const CallExpr& c) {
    if (isConsoleMember(c)) {
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        return emitConsoleCall(c, m);
    }
    if (c.callee->kind == NodeKind::Identifier) {
        const std::string name = static_cast<const Identifier&>(*c.callee).name;
        return emitIdentifierCall(c, name);
    }
    if (c.callee->kind == NodeKind::MemberExpr) {
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        // A call through an import form with no C++ lowering (a namespace
        // import/re-export): report its link miss at the use site instead of a
        // generic method-call skip.
        if (std::string miss = linkedMemberMiss(m); !miss.empty()) {
            notImplemented(*m.object, miss);
            return "0.0";
        }
        if (m.object && m.object->kind == NodeKind::Identifier &&
            static_cast<const Identifier&>(*m.object).name == "parallel") {
            // `parallel.for` / `parallel.reduce` / `parallel.run` (spec 13).
            return emitParallelCall(c, m);
        }
        // `Promise.all/race/allSettled(...)`.
        if (m.object && m.object->kind == NodeKind::Identifier &&
            static_cast<const Identifier&>(*m.object).name == "Promise") {
            return emitPromiseStatic(c, m);
        }
        // A Promise method (`then`/`catch`/`finally`); `on/once/off/emit` (D74).
        if (m.object && detail::isPromiseType(effectiveType(*m.object))) {
            return emitPromiseMemberCall(c, m);
        }
        if (m.object && detail::isEventEmitterType(effectiveType(*m.object))) {
            return emitEventEmitterCall(c, m);
        }
        // `Math.*`, `path.*`, `os.*`, `JSON.stringify`, `process.*`, and the
        // primitive `string` method surface (emit_builtin.cpp).
        if (std::string builtin = emitBuiltinCall(c, m); !builtin.empty()) {
            return builtin;
        }
        notImplemented(c, "this method call");
        return "0.0";
    }
    notImplemented(c, "this call");
    return "0.0";
}

std::string Generator::emitParseBuiltin(const CallExpr& c,
                                        const std::string& name) {
    // The documented global numeric parsers (STDLIB docs section 3).
    // `toString` gives JS's argument coercion for non-string inputs.
    if (c.arguments.size() == 1 && c.arguments.front() &&
        c.arguments.front()->kind != NodeKind::SpreadElement) {
        return "::cp3::runtime::" + name + "(::cp3::runtime::toString(" +
               expr(*c.arguments.front()) + "))";
    }
    notImplemented(c, name + "(...)");
    return "0.0";
}

bool Generator::isConsoleMember(const CallExpr& c) const {
    if (c.callee->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    return m.object && m.object->kind == NodeKind::Identifier &&
           static_cast<const Identifier&>(*m.object).name == "console";
}

// -----------------------------------------------------------------------------
// The builtin EventEmitter (D74): construction, listener values, method calls
// -----------------------------------------------------------------------------

std::string Generator::emitEventEmitterNew(const NewExpr& n) {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::string();
    if (static_cast<const Identifier&>(*n.callee).name != "EventEmitter") {
        return std::string();
    }
    needsEventEmitter_ = true;
    if (!n.arguments.empty()) {
        notImplemented(n, "new EventEmitter(...) with arguments");
    }
    return "::cp3::runtime::makeRef<::cp3::runtime::EventEmitter>()";
}

// A function-typed value is boxed once into a `Listener`
// (cp3/async/emitter.hpp), so every copy of the value shares one identity -
// what makes `off("go", h)` remove the earlier `on("go", h)` for the same
// local. An inline arrow is a fresh box per evaluation, so two inline arrows
// never match (JS function identity).
std::string Generator::emitListenerValue(const Expr& e) {
    if (e.kind == NodeKind::ArrowFunction) {
        const CppInfo ci = info(e);
        if (!ci.representable || ci.shape != Shape::Listener) {
            // A value-returning arrow has no boxed spelling (type_map.cpp).
            notImplemented(e, "this function value");
            return "::cp3::runtime::Listener()";
        }
        needsEventEmitter_ = true;
        return "::cp3::runtime::Listener::box(" +
               emitArrow(static_cast<const ArrowFunction&>(e), {}) + ")";
    }
    const CppInfo ci = info(e);
    if (ci.representable && ci.shape == Shape::Listener) {
        // A function-typed local (or a parameter, or a call returning one):
        // the value already is a box, so hand over the same identity. A named
        // function declaration is not a boxed value and stays out of M1 (its
        // C++ name is a function, not a `Listener`).
        if (e.kind == NodeKind::Identifier &&
            functions_.count(static_cast<const Identifier&>(e).name) != 0) {
            notImplemented(e, "a named function used as a listener value");
            return "::cp3::runtime::Listener()";
        }
        needsEventEmitter_ = true;
        return expr(e);
    }
    notImplemented(e, "this listener argument");
    return "::cp3::runtime::Listener()";
}

std::string Generator::emitEventEmitterCall(const CallExpr& c,
                                            const MemberExpr& m) {
    needsEventEmitter_ = true;
    const std::string recv = "(" + expr(*m.object) + ")";
    if (m.property == "on" || m.property == "once" || m.property == "off") {
        if (c.arguments.size() != 2 || !c.arguments[0] || !c.arguments[1] ||
            c.arguments[0]->kind == NodeKind::SpreadElement ||
            c.arguments[1]->kind == NodeKind::SpreadElement) {
            notImplemented(c, "this method call");
            return "0.0";
        }
        return recv + "->" + m.property + "(" + expr(*c.arguments[0]) + ", " +
               emitListenerValue(*c.arguments[1]) + ")";
    }
    if (m.property == "emit") {
        if (c.arguments.empty() || !c.arguments.front() ||
            c.arguments.front()->kind == NodeKind::SpreadElement) {
            notImplemented(c, "this method call");
            return "0.0";
        }
        std::string text = recv + "->emit(" + expr(*c.arguments.front());
        // The listener surface is arity-fixed in C++: at most one emitted
        // value reaches a listener (cp3/async/emitter.hpp), so a wider emit
        // reports instead of truncating silently.
        if (c.arguments.size() == 2 && c.arguments[1] &&
            c.arguments[1]->kind != NodeKind::SpreadElement) {
            text += ", ::cp3::runtime::EventValue(" + expr(*c.arguments[1]) + ")";
        } else if (c.arguments.size() > 1) {
            notImplemented(c, "emit() with more than one argument");
        }
        return text + ")";
    }
    notImplemented(c, "this method call");
    return "0.0";
}

}  // namespace cp3
