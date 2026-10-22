// =============================================================================
// src/codegen/src/emit/arrow.cpp - the arrow function lowering (STYLE.md §8).
//
// `(a, b) => ...` lowers to a C++ lambda (`[&](A a, B b) -> R { ... }`): the
// parameter list comes from the annotation, then sema, then the type the
// surrounding context expects (a binding annotation, a call argument, a
// field), then the call context (a timer callback or a promise handler). An
// untyped parameter becomes a C++20 generic-lambda parameter, which keeps the
// emitted lambda valid without knowing the cp3 type.
//
// A rest parameter (`...rest: T[]`) lowers to ONE array parameter: the call
// site packs the trailing arguments into it, exactly as a declared function's
// rest parameter does (emit/patterns.cpp's parameterText).
//
// The lambda's return type comes from the annotation, then sema, then the
// expected function type; a `void` arrow stays deduced (which also covers a
// block body with a bare `return;`).
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

// -----------------------------------------------------------------------------
// Arrow functions -> C++ lambdas
// -----------------------------------------------------------------------------

// A lambda parameter list: the annotation wins, then sema's parameter type,
// then the type the surrounding context expects (the expected function type
// on `expected_`), then the call context (a timer callback or a promise
// handler). An untyped parameter becomes a C++20 generic-lambda parameter,
// which keeps the emitted lambda valid without knowing the cp3 type.
std::vector<std::string> Generator::arrowParamText(
    const ArrowFunction& a, const std::vector<std::string>& contextParams,
    const std::optional<Type>& expectedFn) {
    const auto fnIt = sema_.functions.find(a.nodeId);
    std::vector<std::string> params;
    params.reserve(a.params.size());
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        const Parameter* p = a.params[i].get();
        if (!p) continue;
        std::string name = "cp3_param" + std::to_string(i);
        // A rest parameter binds through its `...rest` wrapper
        // (detail::parameterIdentifier), so the name is unwrapped here.
        const Identifier* id = detail::parameterIdentifier(*p);
        const bool named = id != nullptr;
        if (named) {
            name = mapIdent(id->name);
        } else if (p->name) {
            notImplemented(*p->name, "destructuring parameter");
        }
        std::optional<Type> type;
        if (p->type) {
            if (auto t = typeOfNode(*p->type)) type = t;
        }
        if (!type && fnIt != sema_.functions.end() &&
            i < fnIt->second.paramTypes.size()) {
            type = fnIt->second.paramTypes[i];
        }
        if (!type && expectedFn && expectedFn->kind == TypeKind::Function &&
            i + 1 < expectedFn->parts.size()) {
            type = expectedFn->parts[i];
        }
        if (p->isRest) checkRestParam(*p, i + 1 == a.params.size(), type);
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
            declare(id->name, type);
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

// The rest parameter's check: it collects the *trailing* arguments, so it has
// to be the last parameter (JS rejects the other order outright), and its
// element type has to be known - cp3 has no dynamic array (the language
// spec's rule). A failing check clears the type, so the parameter lowers to a
// generic one and the diagnostic stands.
void Generator::checkRestParam(const Parameter& p, bool last,
                                std::optional<Type>& type) {
    if (!last) {
        reportTypeError(p, "a rest parameter must be the last parameter");
        type.reset();
        return;
    }
    if (!type || type->kind != TypeKind::Array || type->parts.empty() ||
        type->parts.front().isUnknown()) {
        reportTypeError(p, "a rest parameter needs an element type "
                           "(e.g. `...rest: number[]`)");
        type.reset();
    }
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
    if (a.isAsync) {
        // D40 admits `async () => {}`; lowering them is out of this wave.
        notImplemented(a, "async arrow function");
        return "0.0";
    }
    if (!a.typeParams.empty()) {
        notImplemented(a, "generic arrow");
    }
    if (intOn()) intFail();

    // The context's expected function type (a binding annotation, a call
    // argument, a field, a return type) fixes the lambda's parameters and
    // return value where the arrow's own resolved types are not concrete.
    std::optional<Type> expectedFn;
    if (expected_ && expected_->kind == TypeKind::Function) expectedFn = *expected_;

    ++nestingDepth_;
    pushScope();
    const std::vector<std::string> params =
        arrowParamText(a, contextParams, expectedFn);
    const std::string ret = arrowReturnType(a, expectedFn);
    const std::optional<Type> retCp = arrowReturnCpType(a, expectedFn);

    // A lambda body returns from the LAMBDA, and a lambda is never a
    // coroutine: swap in the arrow's own context for the enclosing one.
    const std::string savedReturn = currentReturn_;
    const std::optional<Type> savedReturnType = currentReturnType_;
    const std::optional<Type> savedExpected = expected_;
    const std::uint32_t savedFrame = currentFrameId_;
    const bool savedInAsync = inAsyncBody_;
    currentReturn_ = ret.empty() ? "void" : ret;
    currentReturnType_ = retCp;
    currentFrameId_ = a.nodeId;
    inAsyncBody_ = false;
    expected_ = retCp;

    std::string body;
    if (a.body && a.expressionBody) {
        body = "return " + expr(*static_cast<const Expr*>(a.body.get())) + ";";
    } else if (a.body) {
        stmt(*static_cast<const Stmt*>(a.body.get()), body, 1);
    }

    expected_ = savedExpected;
    inAsyncBody_ = savedInAsync;
    currentFrameId_ = savedFrame;
    currentReturnType_ = savedReturnType;
    currentReturn_ = savedReturn;
    popScope();
    --nestingDepth_;

    // `[=] mutable` for a value the enclosing scope may not outlive (a
    // returned closure, a stored field, a call argument, a listener box):
    // copies of a Cell handle still share the binding, and the copy semantics
    // are exactly what spec 10 requires for a non-reassigned local. `mutable`
    // keeps a `count.set(...)` through a by-value cell handle well-formed.
    std::string out = byValue ? "[=](" : "[&](";
    out += joinStrings(params, ", ") + ")";
    if (byValue) out += " mutable";
    if (!ret.empty()) out += " -> " + ret;
    out += " {\n" + body + "}";
    return out;
}

}  // namespace cp3
