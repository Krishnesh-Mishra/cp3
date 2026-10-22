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
//   p.then(f)/p.catch(f)/p.finally(f)
//                         -> `p.then(f)` / `p.catchError(f)` / `p.finally(f)`
//
// The arrow-function lowering lives in the sibling unit (emit/arrow.cpp) and
// the spread lowering in emit/spread.cpp.
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

// -----------------------------------------------------------------------------
// await (spec §14)
// -----------------------------------------------------------------------------

std::string Generator::emitAwait(const AwaitExpr& a) {
    // `await` needs a coroutine to suspend. Codegen builds one at top level
    // (spec §14's top-level await) and for `async function` bodies; anywhere
    // else the enclosing C++ is a plain function or lambda.
    if (nestingDepth_ > 0 && !inAsyncBody_) {
        notImplemented(a, "await outside an async function or the top level");
        return "0.0";
    }
    // Awaiting something means a continuation will be scheduled on the loop.
    needsEventLoop_ = true;
    if (nestingDepth_ == 0) {
        // A top-level `await`: `main()`'s body becomes a coroutine and the
        // loop is driven at exit (cp3/async/promise.hpp's codegen contract).
        topLevelAwait_ = true;
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
        notImplemented(c, "Promise." + prop);
        return "0.0";
    }
    if (c.arguments.size() != 1 || !c.arguments.front() ||
        c.arguments.front()->kind != NodeKind::ArrayLiteral) {
        notImplemented(c, "Promise." + prop + " over a non-array-literal input");
        return "0.0";
    }
    const auto& array = static_cast<const ArrayLiteral&>(*c.arguments.front());
    const Type arrayType = effectiveType(array);
    Type elem = Type::unknown();
    if (arrayType.kind == TypeKind::Array && !arrayType.parts.empty()) {
        elem = arrayType.parts.front();
    }
    const CppInfo elemInfo = classify(elem);
    if (!detail::isPromiseType(elem) || !elemInfo.representable) {
        notImplemented(c, "Promise." + prop + " over non-primitive promises");
        return "0.0";
    }
    // A spread element (`Promise.all([...a, p])`) joins the list, so the
    // vector is built at runtime instead of in a braced initializer.
    bool hasSpread = false;
    for (const auto& el : array.elements) {
        if (el && el->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    if (hasSpread) {
        const std::string list = spreadListText(elem, array.elements);
        if (list.empty()) return "0.0";
        return "::cp3::runtime::" + prop + "(" + list + ")";
    }
    std::vector<std::string> elems;
    for (const auto& el : array.elements) {
        if (!el) continue;
        elems.push_back(expr(*el));
    }
    return "::cp3::runtime::" + prop + "(std::vector<" + elemInfo.cpp + ">{" +
           joinStrings(elems, ", ") + "})";
}

// `Promise.resolve(v)` (DECISIONS D40, codegen contract in cp3/async/promise.hpp).
// An existing promise passes through unchanged (JS identity); otherwise the
// promise is born settled with a copy of the value. The C++ `Promise<T>` comes
// from sema's type for the call, so it always agrees with the type the
// surrounding declaration uses.
std::string Generator::emitPromiseResolve(const CallExpr& c) {
    const Expr* arg = c.arguments.empty() ? nullptr : c.arguments.front().get();
    // `Promise<T>` has a spelling only for the M1 value types (void
    // included); `Promise<undefined>` (a no-argument resolve) stays
    // not-implemented.
    const CppInfo info = classify(effectiveType(c));
    if (!info.representable) {
        notImplemented(c, "Promise.resolve over a value with no C++ spelling");
        return "0.0";
    }
    std::string value;
    if (arg && arg->kind == NodeKind::SpreadElement) {
        // `Promise.resolve(...values)`: the first element is the value (the
        // extra elements are ignored, exactly as JS ignores the extra
        // arguments of a one-parameter call).
        const CppInfo valueInfo =
            classify(detail::promiseValue(effectiveType(c)));
        const std::string first =
            spreadFirstText(static_cast<const SpreadElement&>(*arg),
                            valueInfo.representable ? valueInfo.cpp
                                                    : std::string());
        if (first.empty()) {
            reportTypeError(*arg, "type mismatch: a spread argument of "
                                  "Promise.resolve must be an array of '" +
                                      describe(detail::promiseValue(
                                          effectiveType(c))) + "'");
            return "0.0";
        }
        value = first;
    } else if (arg && detail::isPromiseType(effectiveType(*arg))) {
        return expr(*arg);  // `Promise.resolve(p)` is `p` itself in JS
    } else {
        value = arg ? expr(*arg) : "::cp3::runtime::undefined";
    }
    return info.cpp + "::resolve(" + value + ")";
}

// `Promise.reject(reason)` (DECISIONS D40): a rejection carries no observable
// value, so the C++ promise is `Promise<void>` and the reason is boxed as a
// `Thrown` (the `.catch` handler unboxes it). Sema types the call the same way.
std::string Generator::emitPromiseReject(const CallExpr& c) {
    const Expr* arg = c.arguments.empty() ? nullptr : c.arguments.front().get();
    const CppInfo info = classify(detail::promiseValue(effectiveType(c)));
    if (!info.representable || info.shape != Shape::Void) {
        notImplemented(c, "Promise.reject whose value type is not void");
        return "0.0";
    }
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
        notImplemented(c, "Promise member '" + m.property + "'");
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
