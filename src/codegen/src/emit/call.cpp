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

#include "cp3/diagnostic_codes.hpp"
#include "detail/mangle.h"
#include "detail/type_map.h"

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
        const auto& a = static_cast<const ArrowFunction&>(e);
        std::optional<Type> fnType;
        if (expected_ && expected_->kind == TypeKind::Function) {
            fnType = *expected_;
        } else {
            const Type own = effectiveType(a);
            if (own.kind == TypeKind::Function) fnType = own;
        }
        if (!fnType || classify(*fnType).shape != Shape::Listener) {
            // A value-returning arrow has no boxed spelling.
            notImplemented(e, "this function value");
            return "::cp3::runtime::Listener()";
        }
        needsEventEmitter_ = true;
        return "::cp3::runtime::Listener::box(" +
               emitArrow(a, {}, /*byValue=*/true) + ")";
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
