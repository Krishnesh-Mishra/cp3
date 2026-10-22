// =============================================================================
// src/codegen/src/emit/async.cpp - codegen async / promise / timer lowering
// (STYLE.md §8).
//
// The spec §14 surface, all of it routed through the frozen runtime ABI
// (cp3/async/promise.hpp, cp3/async/eventloop.hpp):
//
//   await expr            -> `(co_await ::cp3::runtime::awaitValue(expr))`
//   setTimeout/setInterval/clearTimeout/clearInterval/sleep
//                         -> `::cp3::runtime::<name>(...)`
//   Promise.all/race/allSettled([...])
//                         -> `::cp3::runtime::all|race|allSettled(vec)`
//                         (the combinator operand lowering lives in
//                         emit/async_promise.cpp)
//   p.then(f)/p.catch(f)/p.finally(f)
//                         -> `p.then(f)` / `p.catchError(f)` / `p.finally(f)`
//
// The arrow-function lowering lives in the sibling unit (emit/arrow.cpp) and
// the spread lowering in emit/spread.cpp.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"
#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

// True when a promise appears in the type at any depth. The fast `valueAwait`
// path only accepts a value whose representation cannot contain a promise: a
// promise nested in an array/tuple/union would need the ordinary settlement
// path to be observed, and copying it into the awaiter would silently keep the
// wrong contract.
bool containsPromiseType(const Type& t) {
    if (detail::isPromiseType(t)) return true;
    for (const Type& part : t.parts) {
        if (containsPromiseType(part)) return true;
    }
    return false;
}

// The delay expression of a direct `sleep(ms)` call, or nullptr when the await
// target is not the builtin (wrong shape, or a user declaration, import, or
// local binding shadows the name). Zero-argument calls are left to the
// ordinary call path so their arity is checked the same way as any other call.
const Expr* directSleepDelay(const Expr* argument,
                             const std::unordered_set<std::string>& functions,
                             const std::unordered_set<std::string>& imports,
                             bool localBinding) {
    if (argument == nullptr || argument->kind != NodeKind::CallExpr) return nullptr;
    if (localBinding || functions.count("sleep") != 0 ||
        imports.count("sleep") != 0) {
        return nullptr;
    }
    const auto& call = static_cast<const CallExpr&>(*argument);
    if (!call.callee || call.callee->kind != NodeKind::Identifier) return nullptr;
    if (static_cast<const Identifier&>(*call.callee).name != "sleep") return nullptr;
    if (call.arguments.size() != 1) return nullptr;
    const Expr* delay = call.arguments.front().get();
    if (delay == nullptr || delay->kind == NodeKind::SpreadElement) return nullptr;
    return delay;
}

}  // namespace

// The value of a temporary `Promise.resolve(v)` (with non-promise `v`), or
// nullptr when the await target is not that shape. The value is safe to pass
// straight to `valueAwait`: the temporary promise has no other observer.
const Expr* Generator::temporaryResolveValue(const Expr* argument) const {
    if (argument == nullptr || argument->kind != NodeKind::CallExpr) return nullptr;
    if (functions_.count("Promise") != 0 || importBindings_.count("Promise") != 0 ||
        hasValueBinding("Promise")) {
        return nullptr;
    }
    const auto& call = static_cast<const CallExpr&>(*argument);
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr) return nullptr;
    const auto& member = static_cast<const MemberExpr&>(*call.callee);
    if (member.property != "resolve" || !member.object ||
        member.object->kind != NodeKind::Identifier ||
        static_cast<const Identifier&>(*member.object).name != "Promise") {
        return nullptr;
    }
    if (call.arguments.size() != 1 || !call.arguments.front()) return nullptr;
    const Expr* value = call.arguments.front().get();
    if (value->kind == NodeKind::SpreadElement) return nullptr;
    if (containsPromiseType(effectiveType(*value))) return nullptr;
    return value;
}

// -----------------------------------------------------------------------------
// await (spec §14)
// -----------------------------------------------------------------------------

// The coroutine requirement of an `await`: awaiting something means the
// continuation is scheduled on the loop, so the enclosing code must be a
// coroutine (an `async function` body, or the top level, whose `main()` the
// compiler turns into one). Reports and returns false when the context cannot
// suspend.
bool Generator::awaitContextOk(const Node& site) {
    if (nestingDepth_ > 0 && !inAsyncBody_) {
        // `await` needs a coroutine, and only an async function (or the top
        // level) is one: sema reports it as CP3506, and the codegen repeats
        // it so a codegen-only pass stays honest.
        reportUnsupported(site, diagcode::kAwaitOutsideAsync,
                          "'await' is only allowed in an async function or at "
                          "the top level");
        return false;
    }
    // Awaiting something means a continuation will be scheduled on the loop.
    needsEventLoop_ = true;
    if (nestingDepth_ == 0) {
        // A top-level `await`: `main()`'s body becomes a coroutine and the
        // loop is driven at exit (cp3/async/promise.hpp's codegen contract).
        topLevelAwait_ = true;
    }
    return true;
}

std::string Generator::emitAwait(const AwaitExpr& a) {
    // `await` needs a coroutine to suspend. Codegen builds one at top level
    // (spec §14's top-level await) and for `async function` bodies; anywhere
    // else the enclosing C++ is a plain function or lambda.
    if (!awaitContextOk(a)) return "0.0";
    // `await sleep(ms)`: the temporary promise is never observed, so lower
    // straight to the timer awaiter (no Promise<void> state or settlement
    // bookkeeping; the resume is still posted as a microtask).
    if (const Expr* delay =
            directSleepDelay(a.argument.get(), functions_, importBindings_,
                             hasValueBinding("sleep"))) {
        return "(co_await ::cp3::runtime::sleepAwait(" + expr(*delay) + "))";
    }
    // `await Promise.resolve(v)` for a non-promise `v`: the temporary state has
    // no other observer, so lower straight to the value awaiter. It still
    // suspends, preserving caller and FIFO microtask order.
    if (const Expr* value = temporaryResolveValue(a.argument.get())) {
        const CppInfo info = classify(effectiveType(*value));
        if (info.representable && info.shape != Shape::Void) {
            return "(co_await ::cp3::runtime::valueAwait(" + expr(*value) + "))";
        }
    }
    const std::string arg =
        a.argument ? expr(*a.argument) : "cp3::runtime::undefined";
    return "(co_await ::cp3::runtime::awaitValue(" + arg + "))";
}

// -----------------------------------------------------------------------------
// Core timer builtins and `sleep` (spec §14, STDLIB docs §03)
// -----------------------------------------------------------------------------

// The delay a timer call's spread argument supplies: the source's first
// element (an empty source is the JS default of 0).
std::string Generator::timerSpreadDelay(const SpreadElement& sp) {
    const std::string src = spreadArrayText(sp, "double");
    if (src.empty()) {
        reportTypeError(sp, "type mismatch: a spread argument of a timer call "
                            "must be a number array (the delay)");
        return "0.0";
    }
    needsTypedArray_ = true;
    return "([&]{ const auto& cp3_src = " + src +
           "; return cp3_src->empty() ? 0.0 : "
           "::cp3::runtime::arrayAt<double>(cp3_src, 0.0); })()";
}

std::string Generator::emitTimerBuiltin(const CallExpr& c, const std::string& name) {
    needsEventLoop_ = true;
    std::vector<std::string> args;
    const std::size_t count = c.arguments.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& arg = c.arguments[i];
        if (!arg) {
            args.push_back("0.0");
            continue;
        }
        if (arg->kind == NodeKind::SpreadElement) {
            // A timer call takes a callback and a delay. A spread supplies the
            // delay; the extra arguments a JS timer would hand to the
            // callback are dropped, because a cp3 timer callback takes none.
            const auto& sp = static_cast<const SpreadElement&>(*arg);
            if (i == 0) {
                reportTypeError(sp, "a timer call takes a callback and a delay;"
                                   " a spread argument cannot supply the "
                                   "callback");
                args.push_back("0.0");
                continue;
            }
            if (i == 1) {
                args.push_back(timerSpreadDelay(sp));
                continue;
            }
            continue;  // the extra arguments the callback cannot take
        }
        if (arg->kind == NodeKind::ArrowFunction) {
            // Timer callbacks take no cp3-level arguments.
            args.push_back(
                emitArrow(static_cast<const ArrowFunction&>(*arg), {}));
            continue;
        }
        args.push_back(expr(*arg));
    }
    return "::cp3::runtime::" + name + "(" + joinStrings(args, ", ") + ")";
}

// -----------------------------------------------------------------------------
// Promise statics and members (spec §14, DECISIONS D42)
// -----------------------------------------------------------------------------

std::string Generator::emitPromiseStatic(const CallExpr& c, const MemberExpr& m) {
    needsEventLoop_ = true;
    const std::string prop = m.property;
    if (prop == "resolve") return emitPromiseResolve(c);
    if (prop == "reject") return emitPromiseReject(c);
    if (prop != "all" && prop != "race" && prop != "allSettled") {
        // A Promise static outside the documented surface (spec 14: resolve,
        // reject, all, race, allSettled) is a program error, not a feature
        // the compiler is still missing: report the member miss.
        reportCodegenError(c, diagcode::kMemberNotFound,
                           "'Promise' has no member '" + prop + "'");
        return "0.0";
    }
    return emitPromiseCombinator(c, prop);
}

// `Promise.resolve(v)` (DECISIONS D40, codegen contract in cp3/async/promise.hpp).
// An existing promise passes through unchanged (JS identity); otherwise the
// promise is born settled with a copy of the value. The C++ `Promise<T>` comes
// from sema's type for the call, so it always agrees with the type the
// surrounding declaration uses.
std::string Generator::emitPromiseResolve(const CallExpr& c) {
    const Expr* arg = c.arguments.empty() ? nullptr : c.arguments.front().get();
    // The promise carries the value's type, so the spelling is the runtime
    // promise over that value type. Every value type cp3 can represent (a
    // primitive, an array, an object shape, a class handle) is a template
    // argument of `cp3::runtime::Promise`; `void` carries no value at all.
    const CppInfo value = classify(detail::promiseValue(effectiveType(c)));
    std::string valueText;
    if (arg && arg->kind == NodeKind::SpreadElement) {
        // `Promise.resolve(...values)`: the first element is the value (the
        // extra elements are ignored, exactly as JS ignores the extra
        // arguments of a one-parameter call).
        const std::string first =
            spreadFirstText(static_cast<const SpreadElement&>(*arg),
                            value.representable ? value.cpp : std::string());
        if (first.empty()) {
            reportTypeError(*arg, "type mismatch: a spread argument of "
                                  "Promise.resolve must be an array of '" +
                                      describe(detail::promiseValue(
                                          effectiveType(c))) + "'");
            return "0.0";
        }
        valueText = first;
    } else if (arg && detail::isPromiseType(effectiveType(*arg))) {
        return expr(*arg);  // `Promise.resolve(p)` is `p` itself in JS
    } else {
        if (!value.representable) {
            // A value no C++ spelling can carry (an `unknown` value, a tuple).
            reportCodegenError(c, diagcode::kTypeMismatch,
                               "type mismatch: cannot resolve a promise over a "
                               "value with no C++ spelling");
            return "0.0";
        }
        if (value.shape == Shape::Void) {
            return "::cp3::runtime::Promise<void>::resolve()";
        }
        valueText = arg ? expr(*arg) : "::cp3::runtime::undefined";
    }
    return "::cp3::runtime::Promise<" + value.cpp + ">::resolve(" + valueText +
           ")";
}

// `Promise.reject(reason)` (DECISIONS D40): a rejection carries no observable
// value, so the C++ promise is `Promise<void>` and the reason is boxed as a
// `Thrown` (the `.catch` handler unboxes it). Sema types the call the same way
// (a `Promise<void>`), so the promise never carries another value type.
std::string Generator::emitPromiseReject(const CallExpr& c) {
    const Expr* arg = c.arguments.empty() ? nullptr : c.arguments.front().get();
    // Sema types the call as `Promise<void>` (D40), so the value type is void
    // by construction: the "not void" form is unreachable and needs no
    // marker of its own.
    std::string reason;
    if (arg && arg->kind == NodeKind::SpreadElement) {
        // `Promise.reject(...reasons)`: the first element is the reason (the
        // extra elements are ignored, exactly as JS ignores the extra
        // arguments of a one-parameter call).
        const std::string first =
            spreadFirstText(static_cast<const SpreadElement&>(*arg), "");
        if (first.empty()) {
            reportTypeError(*arg, "type mismatch: a spread argument of "
                                  "Promise.reject must be an array");
            return "0.0";
        }
        reason = first;
    } else {
        reason = arg ? expr(*arg) : "::cp3::runtime::undefined";
    }
    return "::cp3::runtime::Promise<void>::reject(::cp3::runtime::Thrown::make(" +
           reason + "))";
}

std::string Generator::emitPromiseMemberCall(const CallExpr& c, const MemberExpr& m) {
    needsEventLoop_ = true;
    std::string method;
    if (m.property == "then") {
        method = "then";
    } else if (m.property == "catch" || m.property == "catchError") {
        method = "catchError";  // `catch` is a C++ keyword (cp3/async/promise.hpp)
    } else if (m.property == "finally") {
        method = "finally";
    } else {
        // A Promise member outside the documented surface (spec 14: `then`,
        // `catch`, `finally`) is a program error, not a feature the compiler
        // is still missing: report the member miss.
        reportCodegenError(c, diagcode::kMemberNotFound,
                           "'Promise' has no member '" + m.property + "'");
        return "0.0";
    }

    // Handler parameter types: the receiver's value for `then`, a `Thrown` for
    // `catchError`, none for `finally`. `Promise<void>` has no value to hand
    // over, and a `void` parameter is not a C++ type - a handler there is
    // called nullary (cp3/async/promise.hpp's handler arity adaptation).
    std::vector<std::string> context;
    if (method == "catchError") {
        context.push_back("::cp3::runtime::Thrown");
    } else if (method == "then") {
        const CppInfo valueInfo =
            classify(detail::promiseValue(effectiveType(*m.object)));
        if (valueInfo.representable && valueInfo.shape != Shape::Void) {
            context.push_back(valueInfo.cpp);
        }
    }

    std::vector<std::string> args;
    for (const auto& arg : c.arguments) {
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            // `p.then(...handlers)`: the first element is the handler (the
            // extra elements are ignored, exactly as JS ignores the extra
            // arguments of a one-parameter call).
            const std::string first =
                spreadFirstText(static_cast<const SpreadElement&>(*arg), "");
            if (first.empty()) {
                reportTypeError(*arg, "type mismatch: a spread argument of a "
                                      "Promise handler must be an array of "
                                      "functions");
                continue;
            }
            args.push_back(first);
            continue;
        }
        if (arg->kind == NodeKind::ArrowFunction) {
            args.push_back(emitArrow(static_cast<const ArrowFunction&>(*arg),
                                     context));
            continue;
        }
        args.push_back(expr(*arg));
    }
    return "(" + expr(*m.object) + ")." + method + "(" +
           joinStrings(args, ", ") + ")";
}

}  // namespace cp3
