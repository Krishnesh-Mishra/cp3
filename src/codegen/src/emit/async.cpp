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

#include "cp3/diagnostic_codes.hpp"
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
        // `await` needs a coroutine, and only an async function (or the top
        // level) is one: sema reports it as CP3506, and the codegen repeats
        // it so a codegen-only pass stays honest.
        reportUnsupported(a, diagcode::kAwaitOutsideAsync,
                          "'await' is only allowed in an async function or at "
                          "the top level");
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
// then the type the surrounding context expects (the expected function type
// on `expected_`), then the call context (a timer callback or a promise
// handler). An untyped parameter becomes a C++20 generic-lambda parameter,
// which keeps the emitted lambda valid without knowing the cp3 type.
//
// A destructuring parameter (`({ a, b }) => ...`) keeps the source value as
// the C++ parameter (`cp3_param<i>`); the body prologue unpacks it
// (emit/closures.cpp).
std::vector<std::string> Generator::arrowParamText(
    const ArrowFunction& a, const std::vector<std::string>& contextParams,
    const std::optional<Type>& expectedFn) {
    std::vector<std::string> params;
    params.reserve(a.params.size());
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        const Parameter* p = a.params[i].get();
        if (!p) continue;
        if (p->isRest) notImplemented(*p, "rest parameter");
        std::string name = "cp3_param" + std::to_string(i);
        const bool named = p->name && p->name->kind == NodeKind::Identifier;
        if (named) {
            name = mapIdent(static_cast<const Identifier&>(*p->name).name);
        }
        const std::optional<Type> type = arrowParamType(a, i, expectedFn);
        // An untyped parameter whose sema type is still `unknown` (or sema's
        // open `$ambient` object placeholder) takes the spelling the context
        // hands it. Leave any binding the context already declared
        // (parallel.reduce declares its accumulator as the init type) in place
        // instead of shadowing it with a placeholder.
        const bool placeholder =
            type && (type->isUnknown() || type->isError() ||
                     (type->kind == TypeKind::Object &&
                      type->name == "$ambient"));
        if (named && type && !placeholder) {
            declare(static_cast<const Identifier&>(*p->name).name, type);
        }
        std::string cpp;
        if (type) {
            const CppInfo ci = classify(*type);
            if (ci.representable && ci.shape != Shape::Void) cpp = ci.cpp;
        }
        if (cpp.empty() && i < contextParams.size()) cpp = contextParams[i];
        params.push_back(cpp.empty() ? ("auto " + name) : (cpp + " " + name));
    }
    return params;
}

// The cp3 type of the lambda's return value: the annotation, then sema, then
// the expected function type's return slot. `nullopt` for a void lambda (its
// C++ return type is deduced).
std::optional<Type> Generator::arrowReturnCpType(
    const ArrowFunction& a, const std::optional<Type>& expectedFn) {
    if (a.returnType) {
        if (auto annotated = typeOfNode(*a.returnType)) {
            return annotated->isVoid() ? std::nullopt : annotated;
        }
    }
    const auto it = sema_.functions.find(a.nodeId);
    if (it != sema_.functions.end()) {
        const Type& t = it->second.returnType;
        if (!t.isUnknown() && !t.isError() && !t.isVoid()) return t;
    }
    if (expectedFn && expectedFn->kind == TypeKind::Function &&
        !expectedFn->parts.empty()) {
        const Type& t = expectedFn->parts.back();
        if (!t.isUnknown() && !t.isError() && !t.isVoid()) return t;
    }
    return std::nullopt;
}

// The lambda's explicit return type, or "" to let C++ deduce it (a `void`
// arrow stays deduced, which also covers a block body with bare `return;`).
std::string Generator::arrowReturnType(const ArrowFunction& a,
                                       const std::optional<Type>& expectedFn) {
    if (auto t = arrowReturnCpType(a, expectedFn)) {
        const CppInfo ci = classify(*t);
        if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
    }
    return std::string();
}

std::string Generator::emitArrow(const ArrowFunction& a,
                                 const std::vector<std::string>& contextParams,
                                 bool byValue) {
    if (!a.typeParams.empty()) {
        notImplemented(a, "generic arrow");
    }
    if (intOn()) intFail();

    // An async arrow (D40) is a C++20 coroutine: its signature is the
    // runtime's `Promise<T>`, its body yields `T` with `co_return`, and its
    // frame outlives the call - so it captures by value.
    const bool isAsync = a.isAsync;
    if (isAsync) needsEventLoop_ = true;

    // The context's expected function type (a binding annotation, a call
    // argument, a field, a return type) fixes the lambda's parameters and
    // return value where the arrow's own resolved types are not concrete.
    std::optional<Type> expectedFn;
    if (expected_ && expected_->kind == TypeKind::Function) expectedFn = *expected_;

    ++nestingDepth_;
    pushScope();
    const std::vector<std::string> params =
        arrowParamText(a, contextParams, expectedFn);
    std::string ret;
    const std::string body = arrowBodyText(a, isAsync, expectedFn, ret);
    popScope();
    --nestingDepth_;

    // `[=] mutable` for a value the enclosing scope may not outlive (a
    // returned closure, a stored field, a call argument, a listener box):
    // copies of a Cell handle still share the binding, and the copy semantics
    // are exactly what spec 10 requires for a non-reassigned local. `mutable`
    // keeps a `count.set(...)` through a by-value cell handle well-formed.
    // An async arrow always captures by value: its coroutine frame outlives
    // the call that created it.
    const bool captureByValue = byValue || isAsync;
    std::string out = captureByValue ? "[=](" : "[&](";
    out += joinStrings(params, ", ") + ")";
    if (captureByValue) out += " mutable";
    if (!ret.empty()) out += " -> " + ret;
    out += " {\n" + body + "}";
    return out;
}

}  // namespace cp3
