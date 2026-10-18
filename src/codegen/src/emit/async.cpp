// =============================================================================
// src/codegen/src/emit/async.cpp - codegen async / promise / timer lowering
// (STYLE.md §8).
//
// The spec §14 surface, all of it routed through the frozen runtime ABI
// (cp3/async/promise.hpp, cp3/async/eventloop.hpp):
//
//   await expr            -> `(co_await ::cp3::runtime::awaitValue(expr))`
//   arrow functions       -> C++ lambdas (`[&](params) -> T { ... }`)
//   setTimeout/setInterval/clearTimeout/clearInterval/sleep
//                         -> `::cp3::runtime::<name>(...)`
//   Promise.all/race/allSettled([...])
//                         -> `::cp3::runtime::all|race|allSettled(vec)`
//   p.then(f)/p.catch(f)/p.finally(f)
//                         -> `p.then(f)` / `p.catchError(f)` / `p.finally(f)`
//
// Arrows are emitted only where a context requires one (a timer callback or a
// promise handler) - the generic expression dispatcher in emit_expr.cpp has no
// `ArrowFunction` case, which keeps unrelated closure cases out of scope.
// Anything else reports CP3002 and emits a compilable stub.
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

std::string Generator::emitTimerBuiltin(const CallExpr& c, const std::string& name) {
    needsEventLoop_ = true;
    std::vector<std::string> args;
    for (const auto& arg : c.arguments) {
        if (!arg) {
            args.push_back("0.0");
            continue;
        }
        if (arg->kind == NodeKind::SpreadElement) {
            notImplemented(*arg, "spread argument");
            continue;
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
    std::vector<std::string> elems;
    for (const auto& el : array.elements) {
        if (!el) continue;
        if (el->kind == NodeKind::SpreadElement) {
            notImplemented(*el, "spread argument");
            continue;
        }
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
    if (arg && arg->kind == NodeKind::SpreadElement) {
        notImplemented(*arg, "spread argument");
        return "0.0";
    }
    if (arg && detail::isPromiseType(effectiveType(*arg))) {
        return expr(*arg);  // `Promise.resolve(p)` is `p` itself in JS
    }
    // `Promise<T>` has a spelling only for the M1 value types (void included);
    // `Promise<undefined>` (a no-argument resolve) stays not-implemented.
    const CppInfo info = classify(effectiveType(c));
    if (!info.representable) {
        notImplemented(c, "Promise.resolve over a value with no C++ spelling");
        return "0.0";
    }
    const std::string value = arg ? expr(*arg) : "::cp3::runtime::undefined";
    return info.cpp + "::resolve(" + value + ")";
}

// `Promise.reject(reason)` (DECISIONS D40): a rejection carries no observable
// value, so the C++ promise is `Promise<void>` and the reason is boxed as a
// `Thrown` (the `.catch` handler unboxes it). Sema types the call the same way.
std::string Generator::emitPromiseReject(const CallExpr& c) {
    const Expr* arg = c.arguments.empty() ? nullptr : c.arguments.front().get();
    if (arg && arg->kind == NodeKind::SpreadElement) {
        notImplemented(*arg, "spread argument");
        return "0.0";
    }
    const CppInfo info = classify(detail::promiseValue(effectiveType(c)));
    if (!info.representable || info.shape != Shape::Void) {
        notImplemented(c, "Promise.reject whose value type is not void");
        return "0.0";
    }
    const std::string reason = arg ? expr(*arg) : "::cp3::runtime::undefined";
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
            notImplemented(*arg, "spread argument");
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

// -----------------------------------------------------------------------------
// Arrow functions -> C++ lambdas
// -----------------------------------------------------------------------------

// A lambda parameter list: the annotation wins, then sema's parameter type,
// then the surrounding context (a timer callback or a promise handler). An
// untyped parameter becomes a C++20 generic-lambda parameter, which keeps the
// emitted lambda valid without knowing the cp3 type.
std::vector<std::string> Generator::arrowParamText(
    const ArrowFunction& a, const std::vector<std::string>& contextParams) {
    const auto fnIt = sema_.functions.find(a.nodeId);
    std::vector<std::string> params;
    params.reserve(a.params.size());
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        const Parameter* p = a.params[i].get();
        if (!p) continue;
        if (p->isRest) notImplemented(*p, "rest parameter");
        std::string name = "cp3_param";
        if (p->name && p->name->kind == NodeKind::Identifier) {
            name = mapIdent(static_cast<const Identifier&>(*p->name).name);
        } else if (p->name) {
            notImplemented(*p->name, "destructuring parameter");
        }
        std::string type;
        if (p->type) {
            if (auto t = typeFromTypeNode(*p->type)) {
                const CppInfo ci = classify(*t);
                if (ci.representable) type = ci.cpp;
            }
        }
        if (type.empty() && fnIt != sema_.functions.end() &&
            i < fnIt->second.paramTypes.size()) {
            const CppInfo ci = classify(fnIt->second.paramTypes[i]);
            if (ci.representable && ci.shape != Shape::Void) type = ci.cpp;
        }
        if (type.empty() && i < contextParams.size()) {
            type = contextParams[i];
        }
        params.push_back(type.empty() ? ("auto " + name) : (type + " " + name));
    }
    return params;
}

// The lambda's explicit return type, or "" to let C++ deduce it (a `void`
// arrow stays deduced, which also covers a block body with bare `return;`).
std::string Generator::arrowReturnType(const ArrowFunction& a) {
    if (a.returnType) {
        if (auto t = typeFromTypeNode(*a.returnType)) {
            const CppInfo ci = classify(*t);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
        }
    }
    auto it = sema_.functions.find(a.nodeId);
    if (it != sema_.functions.end()) {
        const CppInfo ci = classify(it->second.returnType);
        if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
    }
    return std::string();
}

std::string Generator::emitArrow(const ArrowFunction& a,
                                 const std::vector<std::string>& contextParams) {
    if (a.isAsync) {
        // D40 admits `async () => {}`; lowering them is out of this wave.
        notImplemented(a, "async arrow function");
        return "0.0";
    }
    if (!a.typeParams.empty()) {
        notImplemented(a, "generic arrow");
    }

    ++nestingDepth_;
    pushScope();
    const std::vector<std::string> params = arrowParamText(a, contextParams);
    const std::string ret = arrowReturnType(a);

    // A `return` inside the lambda body leaves the LAMBDA, not the enclosing
    // function, and a lambda body is never a coroutine: clear both pieces of
    // the enclosing function's context while lowering it.
    const std::string savedReturn = currentReturn_;
    const bool savedInAsync = inAsyncBody_;
    currentReturn_ = ret.empty() ? "void" : ret;
    inAsyncBody_ = false;

    std::string body;
    if (a.body && a.expressionBody) {
        body = "return " + expr(*static_cast<const Expr*>(a.body.get())) + ";";
    } else if (a.body) {
        stmt(*static_cast<const Stmt*>(a.body.get()), body, 1);
    }

    inAsyncBody_ = savedInAsync;
    currentReturn_ = savedReturn;
    popScope();
    --nestingDepth_;

    std::string out = "[&](" + joinStrings(params, ", ") + ")";
    if (!ret.empty()) out += " -> " + ret;
    out += " {\n" + body + "}";
    return out;
}

}  // namespace cp3
