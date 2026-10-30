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
// A destructuring parameter (`({ a, b }) => ...`) keeps the source value as the
// C++ parameter (the generated `cp3_param<i>`); the body prologue unpacks it
// (emit/closures.cpp's arrowBodyText).
//
// The lambda's return type comes from the annotation, then sema, then the
// expected function type; a `void` arrow stays deduced (which also covers a
// block body with a bare `return;`).
//
// An `async` arrow (DECISIONS D40) is a C++20 coroutine lambda: its signature
// is the runtime's `Promise<T>`, its body yields with `co_return`, and it
// captures by value - the coroutine frame outlives the call that started it,
// so a reference capture could dangle (emit/closures.cpp's arrowBodyText).
//
// A generic arrow (`<T,>(x: T) => x`, spec §16) lowers to a C++ generic
// lambda: its type parameters are the lambda's own template parameters, so
// C++ deduces them at the call site (`f(1)`) or takes them explicitly
// (`f.operator()<double>(1.0)`).
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

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
//
// A destructuring parameter binds through its pattern (the body prologue
// unpacks `cp3_param<i>`), so it has no single C++ name here.
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
        // `app.use`'s third parameter is represented as a callable in sema so
        // `next()` type-checks, but its native ABI is the network continuation
        // object, not the general-purpose `runtime::Listener`. The callback
        // context is authoritative for this compiler-owned parameter.
        if (!p->type && i < contextParams.size() &&
            contextParams[i] == "::cp3::runtime::network::Next") {
            type = Type::named(TypeKind::Object, "$network:Next");
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

// The lambda's own template parameter list (`<typename T, typename U>`), "" for
// an ordinary arrow. A generic arrow (spec §16) is a C++ generic lambda: its
// type parameters are the lambda's own template parameters, so C++ deduces
// them at the call site - exactly like the untyped-parameter path, which
// makes the lambda generic through `auto` parameters.
std::string Generator::arrowTemplatePrefix(const ArrowFunction& a) {
    if (a.typeParams.empty()) return std::string();
    std::vector<std::string> names;
    names.reserve(a.typeParams.size());
    for (const auto& tp : a.typeParams) {
        if (!tp) continue;
        names.push_back(mapIdent(tp->name));
    }
    if (names.empty()) return std::string();
    std::string out = "<";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) out += ", ";
        out += "typename " + names[i];
    }
    return out + ">";
}

std::string Generator::emitArrow(const ArrowFunction& a,
                                 const std::vector<std::string>& contextParams,
                                 bool byValue) {
    // An async arrow (D40) is a C++20 coroutine: its signature is the
    // runtime's `Promise<T>` and its body yields the value type with
    // `co_return` (arrowBodyText). A program that creates one can schedule
    // continuations, so the loop must be driven before the process exits.
    const bool isAsync = a.isAsync;
    if (isAsync) needsEventLoop_ = true;
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
    // The body text and the lambda's C++ signature (an async arrow's is its
    // Promise type). arrowBodyText also emits the destructuring-parameter
    // prologue and switches the body into the arrow's own return context.
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
    // the call that started it, so a reference capture could dangle.
    // A generic lambda's template parameter list sits between the capture
    // list and the parameter list: `[=]<typename T>(T x) -> T { ... }`.
    const bool captureByValue = byValue || isAsync;
    std::string out = captureByValue ? "[=]" : "[&]";
    out += arrowTemplatePrefix(a);
    out += "(" + joinStrings(params, ", ") + ")";
    if (captureByValue) out += " mutable";
    if (!ret.empty()) out += " -> " + ret;
    out += " {\n" + body + "}";
    return out;
}

}  // namespace cp3
