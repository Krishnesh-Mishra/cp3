// =============================================================================
// src/codegen/src/emit/call_args.cpp - argument lowering and value calls
// (STYLE.md section 8).
//
// `emit/call.cpp` keeps the dispatch entry point; this unit owns the pieces a
// call needs once its callee is known: optional/undefined handling, rest
// packing, the `std::function` vs boxed `Listener` forms, and the static
// dispatch for identifier and member callees. They live on a friend helper
// (`detail::CallBuilder`) so the Generator declaration stays within budget
// without introducing a second copy of the emitter state.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/timers.h"
#include "detail/type_map.h"
#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace detail {

struct CallBuilder {
    explicit CallBuilder(Generator& generator) : g(generator) {}

    Generator& g;

    // One lowered argument text plus the explicit `undefined`s that were
    // dropped (stored back as the parameter's default or `std::nullopt`).
    struct ArgTexts {
        std::vector<std::string> value;
        std::vector<bool> drop;
        bool spreadRest = false;  // a lone `f(...xs)` passes the array itself
    };

    ArgTexts callArgTexts(const CallExpr& c, const ParamList* params,
                          const std::vector<std::optional<Type>>& ptypes,
                          bool applyDefaults, bool reifyObjectArgs);
    bool findRestParam(const ParamList* params, std::size_t& restAt) const;
    std::optional<Type> restElemType(
        const std::vector<std::optional<Type>>& ptypes, std::size_t restAt) const;
    std::vector<std::string> packRestArgs(const CallExpr& c,
                                          const ArgTexts& texts,
                                          const std::optional<Type>& elem,
                                          std::size_t restAt);
    std::vector<std::string> plainArgList(const ArgTexts& texts,
                                          const ParamList* params,
                                          bool applyDefaults);
    std::vector<std::string> callArgs(
        const CallExpr& c, const ParamList* params,
        const std::vector<std::optional<Type>>& ptypes, bool applyDefaults,
        bool reifyObjectArgs = false);
    std::string emitFunctionCall(const CallExpr& c, const std::string& text,
                                 const Type& fnType, const ParamList* params);
    std::string emitIdentifierCall(const CallExpr& c);
    std::string emitMemberCall(const CallExpr& c, const MemberExpr& m);
};

// A parameter that may absorb an explicit `undefined` argument: it carries a
// default expression, or is declared optional (`a?: T` lowers to
// `std::optional<T> = std::nullopt`).
bool optionalParamAt(const ParamList& params, std::size_t i) {
    if (i >= params.size()) return false;
    const Parameter* p = params[i].get();
    return p && (p->optional || p->defaultValue != nullptr);
}

const Parameter* paramAt(const ParamList& params, std::size_t i) {
    if (i >= params.size()) return nullptr;
    return params[i].get();
}

// The index of the (first) rest parameter of `params`: it absorbs the whole
// argument vector, so the fixed parameters before it are still passed
// positionally. false when there is none.
bool CallBuilder::findRestParam(const ParamList* params,
                                std::size_t& restAt) const {
    if (!params) return false;
    for (std::size_t i = 0; i < params->size(); ++i) {
        const Parameter* p = (*params)[i].get();
        if (p && p->isRest) {
            restAt = i;
            return true;
        }
    }
    return false;
}

// The element type the rest parameter packs into (`E` of its `E[]`).
std::optional<Type> CallBuilder::restElemType(
    const std::vector<std::optional<Type>>& ptypes, std::size_t restAt) const {
    if (restAt >= ptypes.size() || !ptypes[restAt]) return std::nullopt;
    const Type& t = *ptypes[restAt];
    if (t.kind == TypeKind::Array && !t.parts.empty()) return t.parts.front();
    return std::nullopt;
}

// Stage 1: the text of every argument, plus the explicit `undefined`s that
// must not be passed. Each positional argument is emitted against its
// parameter's expected type (`ptypes`).
CallBuilder::ArgTexts CallBuilder::callArgTexts(
    const CallExpr& c, const ParamList* params,
    const std::vector<std::optional<Type>>& ptypes, bool applyDefaults,
    bool reifyObjectArgs) {
    const std::size_t count = c.arguments.size();
    const std::size_t paramCount = params ? params->size() : 0;
    std::size_t restAt = paramCount;
    const bool hasRest = findRestParam(params, restAt);
    const std::optional<Type> elem =
        hasRest ? restElemType(ptypes, restAt) : std::nullopt;

    ArgTexts texts;
    texts.value.resize(count);
    texts.drop.assign(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        const Expr* arg = c.arguments[i].get();
        if (!arg) {
            texts.value[i] = "0.0";
            continue;
        }
        if (arg->kind == NodeKind::SpreadElement) {
            const auto& spread = static_cast<const SpreadElement&>(*arg);
            if (hasRest && restAt == 0 && i == 0 && count == 1 &&
                spread.argument) {
                // `f(...xs)` for a rest-only callee: the array itself.
                texts.spreadRest = true;
                const std::optional<Type> saved = g.expected_;
                if (restAt < ptypes.size()) g.expected_ = ptypes[restAt];
                texts.value[i] = g.expr(*spread.argument);
                g.expected_ = saved;
            } else {
                g.notImplemented(*arg, "spread argument");
            }
            continue;
        }
        // A generic function's parameter type is opaque at the call site, so
        // an object-literal argument is reified into a generated aggregate
        // struct (support/generic.cpp).
        if (reifyObjectArgs && arg->kind == NodeKind::ObjectLiteral) {
            texts.value[i] =
                g.reifyObjectArg(static_cast<const ObjectLiteral&>(*arg));
            continue;
        }
        const bool optionalHere = i < paramCount && optionalParamAt(*params, i);
        const bool inRest = hasRest && i >= restAt;
        if (arg->kind == NodeKind::UndefinedLiteral && optionalHere && !inRest) {
            texts.drop[i] = true;
            continue;
        }
        if (arg->kind == NodeKind::UndefinedLiteral && inRest &&
            !(elem && elem->isUndefined())) {
            // `undefined` is not an element of the packed vector.
            g.notImplemented(*arg, "undefined in a rest argument");
            texts.value[i] = "0.0";
            continue;
        }
        const std::optional<Type> saved = g.expected_;
        if (inRest) {
            g.expected_ = elem;
        } else if (i < ptypes.size()) {
            g.expected_ = ptypes[i];
        }
        texts.value[i] = g.expr(*arg);
        g.expected_ = saved;
    }
    // An explicit `undefined` that is not trailing must be substituted (a
    // C++ `nullptr`/`undefined` cannot bind the parameter).
    for (std::size_t i = 0; i < count; ++i) {
        if (!texts.drop[i]) continue;
        const Parameter* p = paramAt(*params, i);
        if (applyDefaults || i + 1 < count || hasRest) {
            texts.value[i] = (p && p->defaultValue) ? g.expr(*p->defaultValue)
                                                    : "std::nullopt";
        }
    }
    return texts;
}

// Stage 2a: the rest callee's argument vector.
std::vector<std::string> CallBuilder::packRestArgs(
    const CallExpr& c, const ArgTexts& texts, const std::optional<Type>& elem,
    std::size_t restAt) {
    const std::size_t count = texts.value.size();
    std::vector<std::string> out;
    for (std::size_t i = 0; i < restAt && i < count; ++i) {
        out.push_back(texts.value[i]);
    }
    const CppInfo ec = elem ? classify(*elem) : CppInfo{};
    std::string elemCpp;
    if (ec.representable && ec.shape != Shape::Void) elemCpp = ec.cpp;
    if (elemCpp.empty()) {
        g.notImplemented(c, "this rest argument");
        elemCpp = "double";
    }
    if (texts.spreadRest) {
        out.push_back(texts.value[0]);
    } else if (count == 0) {
        out.push_back("::cp3::runtime::makeArray<" + elemCpp + ">()");
    } else {
        std::vector<std::string> elems;
        elems.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            elems.push_back(texts.value[i].empty() ? "0.0" : texts.value[i]);
        }
        out.push_back("::cp3::runtime::makeArray<" + elemCpp +
                      ">(std::vector<" + elemCpp + ">{" +
                      joinStrings(elems, ", ") + "})");
    }
    return out;
}

// Stage 2b: the plain callee's argument vector. A trailing dropped argument is
// omitted so the C++ default applies; `applyDefaults` appends the callee's
// defaults at the call site (a value callee - a lambda - cannot carry C++
// default arguments).
std::vector<std::string> CallBuilder::plainArgList(const ArgTexts& texts,
                                                   const ParamList* params,
                                                   bool applyDefaults) {
    const std::size_t paramCount = params ? params->size() : 0;
    std::size_t passed = texts.value.size();
    if (!applyDefaults) {
        while (passed > 0 && texts.drop[passed - 1]) --passed;
    }
    std::vector<std::string> out(texts.value.begin(),
                                 texts.value.begin() +
                                     static_cast<std::ptrdiff_t>(passed));
    if (applyDefaults) {
        for (std::size_t i = out.size(); i < paramCount; ++i) {
            const Parameter* p = paramAt(*params, i);
            if (!p || p->isRest) break;
            if (p->defaultValue) {
                out.push_back(g.expr(*p->defaultValue));
            } else if (p->optional) {
                out.push_back("std::nullopt");
            } else {
                break;
            }
        }
    }
    return out;
}

std::vector<std::string> CallBuilder::callArgs(
    const CallExpr& c, const ParamList* params,
    const std::vector<std::optional<Type>>& ptypes, bool applyDefaults,
    bool reifyObjectArgs) {
    std::size_t restAt = params ? params->size() : 0;
    const bool hasRest = findRestParam(params, restAt);
    const ArgTexts texts =
        callArgTexts(c, params, ptypes, applyDefaults, reifyObjectArgs);
    if (hasRest) {
        return packRestArgs(c, texts, restElemType(ptypes, restAt), restAt);
    }
    return plainArgList(texts, params, applyDefaults);
}

// A call through a function-typed value: `text(...)` for the
// `std::function` spelling, `text.invoke(...)` for the boxed Listener.
// `params` is the callee's parameter list when it has one (a value callee's
// call-site defaults / rest packing); an unsupported callee reports and stubs.
std::string CallBuilder::emitFunctionCall(const CallExpr& c,
                                          const std::string& text,
                                          const Type& fnType,
                                          const ParamList* params) {
    const CppInfo ci = classify(fnType);
    if (ci.shape == Shape::Function) {
        std::vector<std::optional<Type>> ptypes;
        ptypes.reserve(fnType.parts.size());
        for (std::size_t i = 0; i + 1 < fnType.parts.size(); ++i) {
            ptypes.push_back(fnType.parts[i]);
        }
        // A lambda cannot carry C++ default arguments, so a value callee's
        // defaults are passed at the call site.
        const std::vector<std::string> args =
            callArgs(c, params, ptypes, params != nullptr);
        return text + "(" + joinStrings(args, ", ") + ")";
    }
    if (ci.shape == Shape::Listener) {
        g.needsEventEmitter_ = true;
        if (c.arguments.empty()) return text + ".invoke()";
        if (c.arguments.size() == 1 && c.arguments.front() &&
            c.arguments.front()->kind != NodeKind::SpreadElement) {
            return text + ".invoke(::cp3::runtime::EventValue(" +
                   g.expr(*c.arguments.front()) + "))";
        }
        g.notImplemented(c, "this listener call");
        return "0.0";
    }
    g.notImplemented(c, "this call");
    return "0.0";
}

std::string CallBuilder::emitIdentifierCall(const CallExpr& c) {
    const std::string name = static_cast<const Identifier&>(*c.callee).name;
    // A local module's imported binding (emit/program.cpp's link table): the
    // target unit's qualified C++ name. A link miss reports at the use site.
    if (std::string linked = g.emitLinkedCall(c, name); !linked.empty()) {
        return linked;
    }
    if (g.functions_.find(name) == g.functions_.end() &&
        isTimerBuiltinName(name)) {
        return g.emitTimerBuiltin(c, name);
    }
    // `String(value)` (bug 9 surface): the scalar conversion global. A
    // user-declared `String` function wins (checked above).
    if (name == "String" && g.functions_.find(name) == g.functions_.end()) {
        if (c.arguments.size() == 1 && c.arguments.front() &&
            c.arguments.front()->kind != NodeKind::SpreadElement) {
            return "::cp3::runtime::toString(" + g.expr(*c.arguments.front()) +
                   ")";
        }
        g.notImplemented(c, "String(...)");
        return "0.0";
    }
    if ((name == "parseInt" || name == "parseFloat") &&
        g.functions_.find(name) == g.functions_.end()) {
        return g.emitParseBuiltin(c, name);
    }
    const auto declIt = g.functionDecls_.find(name);
    const FunctionDeclaration* decl =
        declIt != g.functionDecls_.end() ? declIt->second : nullptr;
    const bool generic = decl != nullptr && !decl->typeParams.empty();
    if (!c.typeArgs.empty() && !generic) {
        g.notImplemented(c, "calls with type arguments");
        return "0.0";
    }
    // A name that is not a function declared in this unit: a locally bound
    // function value (`let add = (a, b) => ...; add(1, 2)`) still calls as
    // a value; anything else is unresolved.
    if (g.functions_.find(name) == g.functions_.end()) {
        if (auto b = g.lookup(name); b && b->type.kind == TypeKind::Function) {
            // A `let f = (...) => ...` binding carries the arrow's declared
            // parameter defaults, which its C++ lambda cannot: pass them at
            // this call site.
            const ParamList* params = nullptr;
            if (auto arrow = g.captures_.arrowOf.find(name);
                arrow != g.captures_.arrowOf.end()) {
                params = &arrow->second->params;
            }
            return emitFunctionCall(
                c, g.emitIdentifier(static_cast<const Identifier&>(*c.callee)),
                b->type, params);
        }
        return g.emitUnresolvedCall(c, name);
    }
    if (g.asyncFunctions_.count(name) != 0) {
        // Calling an async function starts async work whose continuations
        // only run when the loop does.
        g.needsEventLoop_ = true;
    }
    std::vector<std::optional<Type>> ptypes;
    if (decl) {
        ptypes.reserve(decl->params.size());
        for (std::size_t i = 0; i < decl->params.size(); ++i) {
            ptypes.push_back(decl->params[i]
                                 ? g.parameterType(*decl->params[i], *decl, i)
                                 : std::nullopt);
        }
    }
    const std::string callee =
        generic ? g.calleeText(name, c) : g.moduleRef(name);
    return callee + "(" +
           joinStrings(callArgs(c, decl ? &decl->params : nullptr, ptypes,
                                /*applyDefaults=*/false, generic),
                       ", ") +
           ")";
}

std::string CallBuilder::emitMemberCall(const CallExpr& c,
                                        const MemberExpr& m) {
    // A call through an import form with no C++ lowering (a namespace
    // import/re-export): the use site is a program error.
    if (std::string miss = g.linkedMemberMiss(m); !miss.empty()) {
        g.reportCodegenError(*m.object, diagcode::kUnresolvedImport,
                             "unresolved import: " + miss);
        return "0.0";
    }
    if (m.object && m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "parallel") {
        // `parallel.for` / `parallel.reduce` / `parallel.run` (spec 13).
        return g.emitParallelCall(c, m);
    }
    // The typed worker handle surface (D80, spec 13): `w.postMessage`,
    // `w.on(...)`, `await w.request(v)`, `w.terminate()`, and the worker
    // side's `thread.on(...)` / `thread.postMessage(v)`.
    if (std::string handle = g.emitWorkerHandleCall(c, m); !handle.empty()) {
        return handle;
    }
    // `Promise.all/race/allSettled(...)`.
    if (m.object && m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "Promise") {
        return g.emitPromiseStatic(c, m);
    }
    // A Promise method (`then`/`catch`/`finally`); `on/once/off/emit` (D74).
    if (m.object && detail::isPromiseType(g.effectiveType(*m.object))) {
        return g.emitPromiseMemberCall(c, m);
    }
    if (m.object && detail::isEventEmitterType(g.effectiveType(*m.object))) {
        return g.emitEventEmitterCall(c, m);
    }
    // A method call on a generic function's type parameter (`s.area()` where
    // `s: T extends Shape`): `cp3_member_base` dereferences a Ref<T> class
    // handle and leaves a value struct alone.
    if (m.object && g.info(*m.object).shape == Shape::TypeParam) {
        return "cp3_member_base(" + g.expr(*m.object) + ")." +
               mapIdent(m.property) + "(" +
               joinStrings(g.emitPlainArgs(c), ", ") + ")";
    }
    // A class method / interface method call (spec 15) before the builtin
    // namespace surface: a class member name owns its spelling. This hook lived
    // in the pre-redrive dispatcher and was lost in the call_args split.
    if (std::string model = g.emitModelCall(c, m); !model.empty()) {
        return model;
    }
    // `Math.*`, `path.*`, `os.*`, `JSON.stringify`, `process.*`, and the
    // primitive `string` method surface (emit_builtin.cpp).
    if (std::string builtin = g.emitBuiltinCall(c, m); !builtin.empty()) {
        return builtin;
    }
    // The primitive value formatters (`age.toString()`).
    if (c.arguments.empty() && m.property == "toString" && m.object) {
        const CppInfo recv = g.info(*m.object);
        if (recv.shape == Shape::Number || recv.shape == Shape::Bool) {
            return "::cp3::runtime::toString(" + g.expr(*m.object) + ")";
        }
    }
    g.notImplemented(c, "this method call");
    return "0.0";
}

}  // namespace detail

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
        // The imported name has no C++ lowering (a namespace import, a
        // type-only export, a binding whose type cp3 cannot spell): calling
        // it is a program error, not a missing feature.
        reportCodegenError(c, diagcode::kUnresolvedImport,
                           "unresolved import: " + ln.miss);
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
    detail::CallBuilder calls(*this);
    std::vector<std::optional<Type>> ptypes;
    ptypes.reserve(ln.fn->params.size());
    for (std::size_t i = 0; i < ln.fn->params.size(); ++i) {
        ptypes.push_back(ln.fn->params[i]
                             ? parameterType(*ln.fn->params[i], *ln.fn, i)
                             : std::nullopt);
    }
    return ln.cpp + "(" +
           joinStrings(calls.callArgs(c, &ln.fn->params, ptypes,
                                      /*applyDefaults=*/false),
                       ", ") +
           ")";
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

std::string Generator::emitCall(const CallExpr& c) {
    if (isConsoleMember(c)) {
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        return emitConsoleCall(c, m);
    }
    CallBuilder calls(*this);
    if (c.callee->kind == NodeKind::Identifier) {
        return calls.emitIdentifierCall(c);
    }
    if (c.callee->kind == NodeKind::MemberExpr) {
        return calls.emitMemberCall(c, static_cast<const MemberExpr&>(*c.callee));
    }
    // A call through an indexed function value (`closures[i](x)`): the element
    // expression is the callee.
    if (c.callee->kind == NodeKind::IndexExpr) {
        const Type fnType = effectiveType(*c.callee);
        if (fnType.kind == TypeKind::Function) {
            return calls.emitFunctionCall(c, expr(*c.callee), fnType, nullptr);
        }
    }
    notImplemented(c, "this call");
    return "0.0";
}

}  // namespace cp3
