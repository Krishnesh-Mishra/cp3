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
// A callee with no callable lowering reports a type error rather than
// emitting a call to an undeclared C++ name.
// =============================================================================
#include "detail/generator/generator.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/expr/listener.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

// The value shapes `EventValue` can carry (cp3/async/emitter.hpp): the
// primitive values plus the two nullish ones.
bool eventValueShape(Shape s) {
    return s == Shape::Number || s == Shape::String || s == Shape::Bool ||
           s == Shape::Undefined || s == Shape::Null;
}

}  // namespace

std::string Generator::emitParseBuiltin(const CallExpr& c,
                                        const std::string& name) {
    // The documented global numeric parsers (STDLIB docs section 3).
    // `toString` gives JS's argument coercion for non-string inputs.
    if (c.arguments.size() == 1 && c.arguments.front() &&
        c.arguments.front()->kind != NodeKind::SpreadElement) {
        return "::cp3::runtime::" + name + "(::cp3::runtime::toString(" +
               expr(*c.arguments.front()) + "))";
    }
    // The documented surface is one argument (the text to parse); sema reports
    // any other argument list as CP3403 before codegen runs. This is the
    // defensive path for a unit lowered without a sema table.
    reportCodegenError(c, diagcode::kArityMismatch,
                        name + "() takes exactly one argument (the text to "
                        "parse)");
    return "0.0";
}

// The runtime call for one coercion global applied to a value expression.
// `Number` coerces via toNumber; `Boolean` via toBoolean; `isNaN`/`isFinite`
// coerce via toNumber first, then test (JS semantics).
std::string coercionText(const std::string& name, const std::string& value) {
    if (name == "Number") return "::cp3::runtime::toNumber(" + value + ")";
    if (name == "Boolean") return "::cp3::runtime::toBoolean(" + value + ")";
    if (name == "isNaN") return "::cp3::runtime::isNaN(::cp3::runtime::toNumber(" + value + "))";
    return "::cp3::runtime::isFiniteNumber(::cp3::runtime::toNumber(" + value + "))";
}

// `atob(text)` / `btoa(text)`: base64 <-> byte text (stdlib §03). The
// helpers live in the buffer family header; one string argument, exactly
// like the parse globals.
std::string Generator::emitBase64Builtin(const CallExpr& c,
                                         const std::string& name) {
    if (c.arguments.size() == 1 && c.arguments.front() &&
        c.arguments.front()->kind != NodeKind::SpreadElement) {
        needsBuffer_ = true;
        return "::cp3::runtime::buffer::" + name +
               "(::cp3::runtime::toString(" + expr(*c.arguments.front()) + "))";
    }
    reportCodegenError(c, diagcode::kArityMismatch,
                       name + "() takes exactly one argument (the text to "
                       "convert)");
    return "0.0";
}

// `structuredClone(value)`: the runtime deep copy (`cp3::runtime::clone`,
// the worker boundary primitive). One argument, any type.
std::string Generator::emitCloneBuiltin(const CallExpr& c) {
    if (c.arguments.empty() || !c.arguments.front() ||
        c.arguments.front()->kind == NodeKind::SpreadElement) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "structuredClone() takes exactly one argument");
        return "0.0";
    }
    return "::cp3::runtime::clone(" + expr(*c.arguments.front()) + ")";
}

// `parseTo(value, Type)` / `parseTo<Type>(value)`: the bidirectional scalar
// converter (ROADMAP). The target decides the coercion; a native source is
// cast to double first so the runtime overload set is unambiguous. A string
// target formats natively instead: the integral `toString` overload prints
// 64-bit values exactly, while a literal boolean source is folded to its
// value (sema already proved it is exactly `true` or `false`).
std::string Generator::emitParseToBuiltin(const CallExpr& c) {
    if (c.arguments.empty() || !c.arguments.front() ||
        c.arguments.front()->kind == NodeKind::SpreadElement) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "parseTo expects a value and a target type");
        return "0.0";
    }
    const Expr& arg = *c.arguments.front();
    const std::string value = expr(arg);
    const Type target = effectiveType(c);
    CppInfo tc = classify(target);
    if (!tc.representable) {
        reportTypeError(c, "type mismatch: parseTo's target type is not "
                           "supported");
        return "0.0";
    }
    const Type source = effectiveType(arg).literalBaseType();
    const bool nativeSource = source.kind == TypeKind::Native;
    const std::string asDouble =
        nativeSource ? "static_cast<double>(" + value + ")" : value;
    if (target.kind == TypeKind::Native) {
        return "static_cast<" + tc.cpp + ">(::cp3::runtime::toNumber(" +
               asDouble + "))";
    }
    if (target.kind == TypeKind::Boolean) {
        if (source.kind == TypeKind::String) {
            // parseTo parses the boolean text, so "false" is false (plain
            // Boolean("false") is true; parseTo is not truthiness).
            if (arg.kind == NodeKind::StringLiteral) {
                return static_cast<const StringLiteral&>(arg).value == "true"
                           ? "true"
                           : "false";
            }
            return "(" + value + " == \"true\")";
        }
        return "::cp3::runtime::toBoolean(" + asDouble + ")";
    }
    if (target.kind == TypeKind::String) {
        return "::cp3::runtime::toString(" + value + ")";
    }
    return "::cp3::runtime::toNumber(" + asDouble + ")";
}

// `Number(value)`, `Boolean(value)`, `isNaN(value)`, `isFinite(value)`:
// the scalar coercion globals (STDLIB docs §03). Extra arguments are ignored;
// a spread contributes its first element, exactly like `String`.
std::string Generator::emitCoercionBuiltin(const CallExpr& c, const std::string& name) {
    const Expr* arg = c.arguments.empty() ? nullptr : c.arguments.front().get();
    if (arg == nullptr) {
        if (name == "Number") return "std::numeric_limits<double>::quiet_NaN()";
        if (name == "Boolean") return "false";
        if (name == "isNaN") return "true";
        return "true";  // isFinite
    }
    if (arg->kind == NodeKind::SpreadElement) {
        const auto& spread = static_cast<const SpreadElement&>(*arg);
        if (!spread.argument) {
            if (name == "Number") return "std::numeric_limits<double>::quiet_NaN()";
            if (name == "Boolean") return "false";
            if (name == "isNaN") return "true";
            return "true";
        }
        if (info(*spread.argument).shape != Shape::Array) {
            reportUnsupported(*arg, diagcode::kTypeMismatch,
                              "type mismatch: a spread argument must be an array");
            return "0.0";
        }
        const std::string src = expr(*spread.argument);
        std::string fallback;
        if (name == "Number") fallback = "std::numeric_limits<double>::quiet_NaN()";
        else if (name == "Boolean") fallback = "false";
        else if (name == "isNaN") fallback = "true";
        else fallback = "true";
        return "[&]{ const auto& cp3_spread = " + src +
               "; if (cp3_spread->empty()) return " + fallback + "; return " +
               coercionText(name, "cp3_spread->at(0)") + "; }()";
    }
    return coercionText(name, expr(*arg));
}

// `String(value)`: the scalar conversion global (DECISIONS D46). JS
// semantics (spec §03): no argument converts `undefined`, the extra
// arguments are ignored, and a spread contributes its elements as arguments -
// so only the first one is converted.
std::string Generator::emitStringBuiltin(const CallExpr& c) {
    const Expr* arg = c.arguments.empty() ? nullptr : c.arguments.front().get();
    if (arg == nullptr) {
        return "::cp3::runtime::toString(::cp3::runtime::undefined)";
    }
    if (arg->kind != NodeKind::SpreadElement) {
        return "::cp3::runtime::toString(" + expr(*arg) + ")";
    }
    const auto& spread = static_cast<const SpreadElement&>(*arg);
    if (!spread.argument) {
        return "::cp3::runtime::toString(::cp3::runtime::undefined)";
    }
    if (info(*spread.argument).shape != Shape::Array) {
        reportUnsupported(*arg, diagcode::kTypeMismatch,
                          "type mismatch: a spread argument must be an array");
        return "0.0";
    }
    // `String(...xs)` == `String(xs[0])`, and `String()` when the spread is
    // empty.
    const std::string src = expr(*spread.argument);
    return "[&]{ const auto& cp3_spread = " + src +
           "; if (cp3_spread->empty()) return "
           "::cp3::runtime::toString(::cp3::runtime::undefined); return "
           "::cp3::runtime::toString(cp3_spread->at(0)); }()";
}

bool Generator::isConsoleMember(const CallExpr& c) const {
    if (c.callee->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    if (!m.object || m.object->kind != NodeKind::Identifier) return false;
    if (static_cast<const Identifier&>(*m.object).name != "console") return false;
    // A user binding named `console` shadows the standard module: the call
    // belongs to that object, not to the std/console family.
    if (hasValueBinding("console")) return false;
    // The std/console family needs its import: without it the module's ABI is
    // not part of the program (the sema rejects the un-imported name with
    // CP3301 and the exact import statement).
    return consoleImported_;
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
            reportCodegenError(
                c, diagcode::kArityMismatch,
                "arity mismatch: EventEmitter." + m.property +
                    "() takes exactly 2 arguments (event, listener)");
            return "0.0";
        }
        return recv + "->" + m.property + "(" + expr(*c.arguments[0]) + ", " +
               emitListenerValue(*c.arguments[1]) + ")";
    }
    if (m.property == "emit") {
        // `emit(event, ...values)`: the values reach the listeners as erased
        // `EventValue`s (the runtime carries a list of them, not one). A
        // spread contributes its elements at its source position.
        if (c.arguments.empty() || !c.arguments.front()) {
            reportCodegenError(
                c, diagcode::kArityMismatch,
                "arity mismatch: EventEmitter.emit() takes an event name");
            return "0.0";
        }
        bool hasSpread = false;
        for (const auto& argPtr : c.arguments) {
            const Expr* arg = argPtr.get();
            if (arg && arg->kind == NodeKind::SpreadElement) hasSpread = true;
        }
        if (hasSpread) {
            std::string event;
            const Expr* head = c.arguments.front().get();
            if (head->kind == NodeKind::SpreadElement) {
                // `emit(...args)`: the first element names the event.
                const auto& spread = static_cast<const SpreadElement&>(*head);
                if (!spread.argument) {
                    reportCodegenError(
                        c, diagcode::kArityMismatch,
                        "arity mismatch: EventEmitter.emit() takes an event "
                        "name");
                    return "0.0";
                }
                event = spreadFirstText(spread, "std::string");
                if (event.empty()) {
                    reportTypeError(
                        *head,
                        "type mismatch: the event name must be a string");
                    return "0.0";
                }
            } else {
                event = expr(*head);
            }
            std::string values =
                "std::vector<::cp3::runtime::EventValue> cp3_vals;";
            for (std::size_t i = 1; i < c.arguments.size(); ++i) {
                const Expr* arg = c.arguments[i].get();
                if (!arg) continue;
                if (arg->kind == NodeKind::SpreadElement) {
                    const auto& spread =
                        static_cast<const SpreadElement&>(*arg);
                    if (!spread.argument) continue;
                    const Type at = effectiveType(*spread.argument);
                    const CppInfo ec =
                        at.kind == TypeKind::Array && !at.parts.empty()
                            ? classify(at.parts.front())
                            : CppInfo{};
                    if (!ec.representable || !eventValueShape(ec.shape)) {
                        reportTypeError(
                            *arg,
                            "type mismatch: an emitted value must be a "
                            "primitive value");
                        return "0.0";
                    }
                    values += " { const auto& cp3_s = " +
                              expr(*spread.argument) +
                              "; for (const auto& cp3_e : *cp3_s) "
                              "cp3_vals.push_back("
                              "::cp3::runtime::EventValue(cp3_e)); }";
                    continue;
                }
                values +=
                    " cp3_vals.push_back(::cp3::runtime::EventValue(" +
                    expr(*arg) + "));";
            }
            return "[&]{ " + values + " return " + recv + "->emit(" + event +
                   ", cp3_vals); }()";
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
    reportCodegenError(c, diagcode::kTypeMismatch,
                       "type mismatch: EventEmitter has no method '" +
                           m.property + "'");
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
        if (!paramValue(params[i], i, value)) {
            g.reportTypeError(
                a,
                "type mismatch: a listener parameter must be a primitive "
                "value (number, string, boolean, null or undefined)");
            return std::string();
        }
        typed = typed || value.typed;
        values.push_back(value);
    }
    g.needsEventEmitter_ = true;
    const std::string lambda = g.emitArrow(a, {}, /*byValue=*/true);
    if (lambda.empty()) {
        g.reportTypeError(
            a, "type mismatch: this function value cannot be used as a "
               "listener");
        return std::string();
    }
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
    if (decl == nullptr) {
        g.reportTypeError(
            e, "type mismatch: this function cannot be used as a listener "
               "value");
        return std::string();
    }
    if (!decl->typeParams.empty()) {
        g.reportTypeError(
            e,
            "type mismatch: a generic function cannot be used as a listener "
            "value");
        return std::string();
    }
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
        if (!p) {
            g.reportTypeError(
                e, "type mismatch: a listener parameter has no type");
            return std::string();
        }
        const std::optional<Type> type = g.parameterType(*p, *decl, i);
        if (!type) {
            g.reportTypeError(
                e, "type mismatch: a listener parameter has no type");
            return std::string();
        }
        ParamValue value;
        if (!paramValue(*type, i, value)) {
            g.reportTypeError(
                e,
                "type mismatch: a listener parameter must be a primitive "
                "value (number, string, boolean, null or undefined)");
            return std::string();
        }
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
        if (!paramValue(fnType.parts[i], i, value)) {
            g.reportTypeError(
                e,
                "type mismatch: a listener parameter must be a primitive "
                "value (number, string, boolean, null or undefined)");
            return std::string();
        }
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
    // An inline arrow: the lambda is the boxed callable. A failure inside the
    // arrow lowering has already reported the precise parameter error.
    if (e.kind == NodeKind::ArrowFunction) {
        const std::string boxed =
            arrow(g, static_cast<const ArrowFunction&>(e));
        if (!boxed.empty()) return boxed;
        return "::cp3::runtime::Listener()";
    }
    // A named function declaration: the C++ function is the boxed callable.
    if (e.kind == NodeKind::Identifier &&
        g.functions_.count(static_cast<const Identifier&>(e).name) != 0) {
        const std::string boxed = named(g, e);
        if (!boxed.empty()) return boxed;
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
        return "::cp3::runtime::Listener()";
    }
    g.reportTypeError(
        e, "type mismatch: this value is not callable as a listener");
    return "::cp3::runtime::Listener()";
}

}  // namespace detail

}  // namespace cp3
