// =============================================================================
// src/codegen/src/emit/call.cpp - call lowering side tables (STYLE.md §8).
//
// The call dispatcher and its argument lowering live in emit/call_args.cpp;
// this unit keeps the lowering pieces that only a call needs but that do not
// share the CallBuilder state:
//
//   parseInt/parseFloat       -> the documented global numeric parsers
//   console.<member>(...)     -> the std/console family's lowering unit
//                                (src/std/console/codegen.cpp, D72)
//   new EventEmitter() / e.on/once/off/emit(...)  -> the builtin's lowering
//                                (cp3/async/emitter.hpp, D74)
//
// A callee that is neither a user function nor a lowered intrinsic reports
// CP3002 rather than emitting a call to an undeclared C++ name.
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/listener.h"
#include "detail/mangle.h"
#include "detail/type_map.h"
#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

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
        // The builtin has no constructor parameters (spec 14): an argument is
        // a program error, not a missing feature.
        reportCodegenError(n, diagcode::kArityMismatch,
                           "arity mismatch: EventEmitter's constructor takes no "
                           "arguments, got " +
                               std::to_string(n.arguments.size()));
    }
    return "::cp3::runtime::makeRef<::cp3::runtime::EventEmitter>()";
}

// A listener value is boxed into a `Listener` (cp3/async/emitter.hpp), so
// every copy of the value shares one identity - what makes `off("go", h)`
// remove the earlier `on("go", h)` for the same local. An inline arrow is a
// fresh box per evaluation, so two inline arrows never match (JS function
// identity). The lowering itself lives in detail/listener.h.
std::string Generator::emitListenerValue(const Expr& e) {
    return detail::ListenerBuilder::value(*this, e);
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
        // `emit(event, ...values)`: the values reach the listeners as erased
        // `EventValue`s (the runtime carries a list of them, not one). A
        // spread among the arguments has no lowering.
        if (c.arguments.empty() || !c.arguments.front()) return "0.0";
        for (const auto& argPtr : c.arguments) {
            const Expr* arg = argPtr.get();
            if (!arg) continue;
            if (arg->kind == NodeKind::SpreadElement) {
                notImplemented(c, "this method call");
                return "0.0";
            }
        }
        std::string text = recv + "->emit(" + expr(*c.arguments.front());
        std::vector<std::string> values;
        for (std::size_t i = 1; i < c.arguments.size(); ++i) {
            values.push_back("::cp3::runtime::EventValue(" +
                             expr(*c.arguments[i]) + ")");
        }
        if (values.size() == 1) {
            text += ", " + values.front();
        } else if (values.size() == 2 || values.size() == 3) {
            for (const std::string& value : values) text += ", " + value;
        } else if (values.size() > 3) {
            text += ", std::vector<::cp3::runtime::EventValue>{";
            for (const std::string& value : values) text += value + ", ";
            text += "}";
        }
        return text + ")";
    }
    notImplemented(c, "this method call");
    return "0.0";
}

// -----------------------------------------------------------------------------
// The listener lowering (detail/listener.h)
// -----------------------------------------------------------------------------

namespace detail {

namespace {

// The runtime hands a listener at most this many values (the listener's
// parameter count is the callable's arity).
constexpr std::size_t kMaxListenerParams = 8;

// One listener parameter: the erased value's name, the C++ text the callable
// receives it as, and whether the declared type asks for a conversion.
struct ParamValue {
    std::string name;
    std::string arg;
    bool typed = false;
};

// The parameter's value: the erased `EventValue` when the declared type is
// not a primitive (the erased value carries a primitive only), else the
// converted value. False when the declared type has no conversion at all.
bool paramValue(const Type& paramType, std::size_t index, ParamValue& out) {
    out.name = "cp3_arg" + std::to_string(index);
    const CppInfo ci = classify(paramType);
    if (!ci.representable || ci.shape == Shape::Void) {
        out.arg = out.name;
        return true;
    }
    if (ci.shape == Shape::String) {
        out.typed = true;
        out.arg = out.name + ".toString()";
        return true;
    }
    if (ci.shape == Shape::Number) {
        out.typed = true;
        out.arg = out.name + ".toNumber()";
        return true;
    }
    if (ci.shape == Shape::Bool) {
        out.typed = true;
        out.arg = out.name + ".truthy()";
        return true;
    }
    return false;
}

// The box call: with the identity when the boxed value has an address (the
// same value boxed twice is the same listener), without one otherwise.
std::string boxText(const std::string& callable, const std::string& id) {
    if (id.empty()) return "::cp3::runtime::Listener::box(" + callable + ")";
    return "::cp3::runtime::Listener::boxIdentified(" + callable + ", " + id +
           ")";
}

// The adapter around a listener callable: the erased values arrive as the
// parameters and the call passes them on (converted where the declared type
// asks for it). The captured callable is `mutable` (the arrow's own lambda
// is), so the adapter is too.
std::string adapterText(const std::string& callable,
                        const std::vector<ParamValue>& params) {
    std::string list;
    std::string call;
    for (const ParamValue& param : params) {
        if (!list.empty()) list += ", ";
        list += "const ::cp3::runtime::EventValue& " + param.name;
        if (!call.empty()) call += ", ";
        call += param.arg;
    }
    return "[cp3_l = " + callable + "](" + list + ") mutable { cp3_l(" + call +
           "); }";
}

}  // namespace

std::vector<Type> ListenerBuilder::paramTypes(Generator& g,
                                              const ArrowFunction& a) {
    // The resolution mirrors emit/arrow.cpp's own: the annotation wins, then
    // sema's parameter type, then the type the context expects.
    std::optional<Type> expectedFn;
    if (g.expected_ && g.expected_->kind == TypeKind::Function) {
        expectedFn = *g.expected_;
    }
    const auto fnIt = g.sema_.functions.find(a.nodeId);
    std::vector<Type> out;
    out.reserve(a.params.size());
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        const Parameter* p = a.params[i].get();
        if (!p) {
            out.push_back(Type::unknown());
            continue;
        }
        std::optional<Type> type;
        if (p->type) {
            if (auto t = g.typeOfNode(*p->type)) type = t;
        }
        if (!type && fnIt != g.sema_.functions.end() &&
            i < fnIt->second.paramTypes.size()) {
            type = fnIt->second.paramTypes[i];
        }
        if (!type && expectedFn && expectedFn->kind == TypeKind::Function &&
            i + 1 < expectedFn->parts.size()) {
            type = expectedFn->parts[i];
        }
        out.push_back(type ? *type : Type::unknown());
    }
    return out;
}

std::string ListenerBuilder::arrow(Generator& g, const ArrowFunction& a) {
    const std::vector<Type> params = paramTypes(g, a);
    if (params.size() > kMaxListenerParams) {
        g.reportCodegenError(a, diagcode::kTypeMismatch,
                             "a listener takes at most 8 values");
        return std::string();
    }
    std::vector<ParamValue> values;
    bool typed = false;
    for (std::size_t i = 0; i < params.size(); ++i) {
        ParamValue value;
        if (!paramValue(params[i], i, value)) return std::string();
        typed = typed || value.typed;
        values.push_back(value);
    }
    g.needsEventEmitter_ = true;
    const std::string lambda = g.emitArrow(a, {}, /*byValue=*/true);
    if (!typed) {
        // The lambda takes the emitted values as they are.
        return "::cp3::runtime::Listener::box(" + lambda + ")";
    }
    return "::cp3::runtime::Listener::box(" + adapterText(lambda, values) + ")";
}

std::string ListenerBuilder::named(Generator& g, const Expr& e) {
    const std::string& name = static_cast<const Identifier&>(e).name;
    const auto it = g.functionDecls_.find(name);
    const FunctionDeclaration* decl =
        it != g.functionDecls_.end() ? it->second : nullptr;
    if (decl == nullptr || !decl->typeParams.empty()) return std::string();
    if (decl->params.size() > kMaxListenerParams) {
        g.reportCodegenError(e, diagcode::kTypeMismatch,
                             "a listener takes at most 8 values");
        return std::string();
    }
    const std::string fn = g.moduleRef(name);
    std::vector<ParamValue> values;
    bool typed = false;
    for (std::size_t i = 0; i < decl->params.size(); ++i) {
        const Parameter* p = decl->params[i].get();
        if (!p) return std::string();
        const std::optional<Type> type = g.parameterType(*p, *decl, i);
        if (!type) return std::string();
        ParamValue value;
        if (!paramValue(*type, i, value)) return std::string();
        typed = typed || value.typed;
        values.push_back(value);
    }
    g.needsEventEmitter_ = true;
    // The box carries the function's address: every box of it is the same
    // listener, so `off(event, handler)` removes the one `on` registered.
    const std::string id = "reinterpret_cast<const void*>(&" + fn + ")";
    return boxText(typed ? adapterText(fn, values) : fn, id);
}

std::string ListenerBuilder::functionValue(Generator& g, const Expr& e,
                                            const Type& fnType) {
    if (fnType.parts.size() > kMaxListenerParams) {
        g.reportCodegenError(e, diagcode::kTypeMismatch,
                             "a listener takes at most 8 values");
        return std::string();
    }
    std::vector<ParamValue> values;
    bool typed = false;
    for (std::size_t i = 0; i + 1 < fnType.parts.size(); ++i) {
        ParamValue value;
        if (!paramValue(fnType.parts[i], i, value)) return std::string();
        typed = typed || value.typed;
        values.push_back(value);
    }
    g.needsEventEmitter_ = true;
    const std::string value = g.expr(e);
    // The box carries the value's address: every box of it is the same
    // listener, so `off(event, h)` removes the one `on` registered. A value
    // with no address (a call's result) is boxed without one: it is a fresh
    // listener every time, as it is in JS.
    const std::string id = e.kind == NodeKind::Identifier
                                ? "reinterpret_cast<const void*>(&" + value + ")"
                                : std::string();
    return boxText(typed ? adapterText(value, values) : value, id);
}

std::string ListenerBuilder::value(Generator& g, const Expr& e) {
    // An inline arrow: the lambda is the boxed callable.
    if (e.kind == NodeKind::ArrowFunction) {
        const std::string boxed =
            arrow(g, static_cast<const ArrowFunction&>(e));
        if (!boxed.empty()) return boxed;
        g.notImplemented(e, "this function value");
        return "::cp3::runtime::Listener()";
    }
    // A named function declaration: the C++ function is the boxed callable.
    if (e.kind == NodeKind::Identifier &&
        g.functions_.count(static_cast<const Identifier&>(e).name) != 0) {
        const std::string boxed = named(g, e);
        if (!boxed.empty()) return boxed;
        g.notImplemented(e, "a named function used as a listener value");
        return "::cp3::runtime::Listener()";
    }
    // A function-typed value that is already a box: the same identity.
    const CppInfo ci = g.info(e);
    if (ci.representable && ci.shape == Shape::Listener) {
        g.needsEventEmitter_ = true;
        return g.expr(e);
    }
    // A function value with a typed signature (a `std::function`): box it.
    if (ci.representable && ci.shape == Shape::Function) {
        const std::string boxed = functionValue(g, e, ci.type);
        if (!boxed.empty()) return boxed;
    }
    g.notImplemented(e, "this listener argument");
    return "::cp3::runtime::Listener()";
}

}  // namespace detail

}  // namespace cp3
