// =============================================================================
// src/sema/src/checks/call/event_emitter_check.cpp - typed EventEmitter calls.
//
// A `EventEmitter<Events>` receiver (the Events map lives in parts[0]) checks
// its `on` / `once` / `off` / `onAny` / `offAny` / `isValidEventName` / `emit`
// arguments against that map (v0.2.4). The event name must name a member of
// the map; a map with an index signature accepts any dynamic name (members
// win; the index type answers the rest). A statically proven literal name
// (`bus.emit("hello", …)` / `const e = "hello"; bus.emit(e, …)` / `const e:
// "hello" | "login" = …`) is looked up the same way. Anything else is an
// error with the two sanctioned escapes (a `try/catch` at runtime, or a guard
// like `if (bus.isValidEventName(name))`).
//
// Listeners are checked as functions against the map's value signature: beta
// parameters and arity are checked, annotations must agree with the map
// (mutual assignability), and unannotated parameters are contextually typed
// by the map (the payload follows the event name, like fs' listener typing).
// For multi-valued rest maps (`[key: string]: (...args: any[]) => void`) a
// trailing array parameter of the value signature is the rest indicator: any
// number of payloads, each checked against the element type.
//
// `onAny` is the wildcard channel for the dynamic tier only: it fires only
// for events outside the map's declared member set, and is illegal on a
// member-only map (`onAny` hears only dynamic events).
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

// A trailing array parameter in an Events-map value signature is the rest
// indicator (`...args: any[]`): any number of payloads, each checked against
// the element type. Returns the fixed prefix length and the rest element.
struct MapSignature {
    std::size_t prefix = 0;   // fixed parameter slots
    bool rest = false;        // the signature has a rest parameter
    Type restElem;            // the rest element type (any when the array is)
};

MapSignature mapSignatureOf(const Type& fn) {
    MapSignature out;
    if (fn.kind != TypeKind::Function || fn.parts.size() < 2) return out;
    const std::size_t count = fn.parts.size() - 1;  // return slot excluded
    if (count > 0 && fn.parts[count - 1].kind == TypeKind::Array) {
        out.rest = true;
        out.prefix = count - 1;
        const Type& array = fn.parts[count - 1];
        if (!array.parts.empty()) out.restElem = array.parts.front();
        else out.restElem = Type::unknown();
    } else {
        out.prefix = count;
    }
    return out;
}

// The payload slot type of a value signature at parameter index `i`.
Type mapSlotType(const Type& fn, std::size_t i) {
    const MapSignature sig = mapSignatureOf(fn);
    if (!sig.rest) {
        if (i >= fn.parts.size() - 1) return Type::unknown();
        return fn.parts[i];
    }
    if (i < sig.prefix) return fn.parts[i];
    return sig.restElem;
}

}  // namespace

// The one listener function of an event: the named member's type (or the
// index signature's when the map has one and no member matches), unknown when
// the event doesn't exist, function-type kinds only.
Type Analyzer::eventMapListenerFor(const Type& map, const std::string& event,
                                   const SourceSpan& span) {
    const Shape* sh = shapeOf(map);
    if (sh == nullptr) return Type::unknown();
    if (const Field* f = findField(sh, event)) {
        return f->type;
    }
    if (sh->hasIndex) {
        // Dynamic names of one payload type: the index signature is the
        // universal event slot.
        return sh->indexType;
    }
    report(diagcode::kMemberNotFound, span,
           "no event '" + event + "' in the Events map");
    return Type::voidType();
}

// The proven literal values of an event-name expression: a string literal, a
// literal-typed variable, or a union of all-literal parts. Empty optional when
// nothing about the name is known (an index-signature map takes any string).
std::optional<std::vector<std::string>> Analyzer::eventNameLiteralValues(
    Expr& first) {
    if (first.kind == NodeKind::StringLiteral) {
        return std::vector<std::string>{static_cast<StringLiteral&>(first).value};
    }
    const Type t = typeOfExpr(first);
    if (t.isLiteral() && t.literalBase == TypeKind::String) {
        return std::vector<std::string>{t.stringValue};
    }
    if (t.kind == TypeKind::Union) {
        std::vector<std::string> out;
        for (const Type& part : t.parts) {
            if (!part.isLiteral() || part.literalBase != TypeKind::String)
                return std::nullopt;
            out.push_back(part.stringValue);
        }
        return out;
    }
    return std::nullopt;
}

// Contextual function type for an `onAny` handler: the event name string
// followed by the index signature's parameter slots. A rest index signature
// (`...args: any[]`) keeps every positional slot typed by the element type.
Type Analyzer::onAnyContextType(const Shape* mapShape) {
    const std::size_t slots = 7;  // handler arity <= 8 (name + 7 payloads)
    std::vector<Type> params;
    params.reserve(slots + 1);
    params.push_back(Type::string());
    const Type fn = mapShape != nullptr ? mapShape->indexType
                                        : Type::unknown();
    if (fn.kind != TypeKind::Function || fn.parts.size() < 2) {
        for (std::size_t i = 0; i < slots; ++i) params.push_back(Type::unknown());
        return Type::function(std::move(params), Type::voidType());
    }
    for (std::size_t i = 0; i < slots; ++i) {
        params.push_back(mapSlotType(fn, i));
    }
    return Type::function(std::move(params), Type::voidType());
}

// `bus.isValidEventName(name)` in a condition: the true branch narrows the
// checked identifier to the union of the map's declared event keys.
std::optional<Analyzer::EventNameNarrowing> Analyzer::detectEventNameNarrowing(
    Expr& cond) {
    if (cond.kind != NodeKind::CallExpr) return std::nullopt;
    const auto& call = static_cast<const CallExpr&>(cond);
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr)
        return std::nullopt;
    const auto& m = static_cast<const MemberExpr&>(*call.callee);
    if (m.property != "isValidEventName") return std::nullopt;
    const Expr* objectArg = m.object.get();
    if (!objectArg) return std::nullopt;
    const Type recv = typeOfExpr(const_cast<Expr&>(*objectArg));
    if (!isEventEmitterType(recv) || recv.parts.empty()) return std::nullopt;
    const Shape* sh = shapeOf(recv.parts[0]);
    if (sh == nullptr || sh->hasIndex || sh->fields.empty()) return std::nullopt;
    if (call.arguments.size() != 1 || !call.arguments[0] ||
        call.arguments[0]->kind != NodeKind::Identifier)
        return std::nullopt;
    std::vector<Type> literals;
    literals.reserve(sh->fields.size());
    for (const Field& f : sh->fields) literals.push_back(Type::stringLiteral(f.name));
    return EventNameNarrowing{
        static_cast<const Identifier&>(*call.arguments[0]).name,
        literals.size() == 1 ? literals.front()
                             : Type::unionOf(std::move(literals))};
}

// The annotated parameters of a listener must agree with the map's listener
// signature (mutual assignability), and its positional slot count must not
// exceed the signature's.
void Analyzer::checkListenerAnnotations(const ArrowFunction& arrow,
                                        const Type& fn, const SourceSpan& span) {
    const MapSignature sig = mapSignatureOf(fn);
    const std::size_t slots = fn.kind == TypeKind::Function
                                  ? (sig.rest ? sig.prefix : fn.parts.size() - 1)
                                  : 0;
    for (std::size_t i = 0; i < arrow.params.size(); ++i) {
        const Parameter* p = arrow.params[i].get();
        if (!p) continue;
        const Type expected = mapSlotType(fn, i);
        if (p->type) {
            const Type actual = resolveType(*p->type);
            if (actual.isUnknown() || actual.isError()) continue;
            if (!wouldAccept(expected, actual, false) &&
                !wouldAccept(actual, expected, false)) {
                report(diagcode::kTypeMismatch, p->span,
                       "type mismatch: the handler parameter #" +
                           std::to_string(i + 1) +
                           " is not compatible with the Events map's "
                           "listener signature");
            }
        }
        if (i >= slots && !sig.rest) {
            report(diagcode::kTypeMismatch, p->span,
                   "type mismatch: the handler declares more parameters than "
                   "the Events map's listener signature provides");
            return;
        }
    }
}

bool Analyzer::tryInferEventEmitterCall(CallExpr& call, MemberExpr& m,
                                        Type& out) {
    if (!m.object) return false;
    Expr* objectArg = m.object.get();
    const Type recv = typeOfExpr(*objectArg);
    if (!isEventEmitterType(recv)) return false;
    const bool typed = !recv.parts.empty();
    if (m.property == "onAny" || m.property == "offAny") {
        // The wildcard channel (v0.2.4): the handler sees the event name and
        // the emitted values. A member-only map has no dynamic events, so the
        // subscription can never fire.
        if (call.arguments.size() != 1 || !call.arguments[0] ||
            call.arguments[0]->kind == NodeKind::SpreadElement) {
            report(diagcode::kTypeMismatch, call.span,
                   "arity mismatch: EventEmitter." + m.property +
                       " takes exactly 1 argument (the handler)");
            out = Type::voidType();
            return true;
        }
        const Type& map = recv.parts[0];
        const Shape* mapShape = typed ? shapeOf(map) : nullptr;
        if (mapShape == nullptr || !mapShape->hasIndex) {
            if (mapShape != nullptr) {
                report(diagcode::kTypeMismatch, call.span,
                       "type mismatch: `onAny` hears only dynamic events; the "
                       "Events map has no index signature, so all its events are "
                       "named");
                out = Type::voidType();
                return true;
            }
        }
        Expr* arg = call.arguments[0].get();
        if (arg && arg->kind == NodeKind::ArrowFunction) {
            auto* arrow = static_cast<ArrowFunction*>(arg);
            const Type contextual = onAnyContextType(mapShape);
            contextualArrowTypes_[arrow->nodeId] = contextual;
            const Type at = inferExpr(*arg);
            contextualArrowTypes_.erase(arrow->nodeId);
            if (at.kind != TypeKind::Function) {
                report(diagcode::kTypeMismatch, arg->span,
                       "the handler must be a function");
            }
        } else if (arg) {
            inferExpr(*arg);
        }
        out = Type::voidType();
        return true;
    }
    if (m.property == "isValidEventName") {
        if (call.arguments.size() != 1 || !call.arguments[0] ||
            call.arguments[0]->kind == NodeKind::SpreadElement) {
            report(diagcode::kTypeMismatch, call.span,
                   "arity mismatch: EventEmitter.isValidEventName takes exactly "
                   "1 argument (the event name)");
            out = Type::boolean();
            return true;
        }
        inferExpr(*call.arguments[0]);
        out = Type::boolean();
        return true;
    }
    if (!typed || (m.property != "on" && m.property != "once" &&
                   m.property != "off" && m.property != "emit")) {
        return false;
    }
    const Type& map = recv.parts[0];
    const Shape* sh = shapeOf(map);
    if (sh == nullptr) {
        // Already reported at construction.
        out = Type::voidType();
        return true;
    }
    const bool emitting = m.property == "emit";
    const std::size_t minArgs = emitting ? 1 : 2;
    const std::size_t maxArgs = emitting ? 9 : 2;
    if (call.arguments.size() < minArgs) {
        report(diagcode::kArityMismatch, call.span,
               "EventEmitter." + m.property + "() takes " +
                   (emitting ? std::to_string(minArgs) + " or more arguments "
                             : "exactly 2 arguments ") +
                   "(got " + std::to_string(call.arguments.size()) + ")");
        out = Type::voidType();
        return true;
    }
    if (call.arguments.size() > maxArgs) {
        report(diagcode::kArityMismatch, call.span,
               "EventEmitter." + m.property + "() takes at most " +
                   std::to_string(maxArgs) + " arguments (got " +
                   std::to_string(call.arguments.size()) + ")");
        out = Type::voidType();
        return true;
    }
    // The event name must be resolvable: a string literal, a literal-typed
    // variable (or an all-literal union), or any string on a map with an index
    // signature. A dynamic name on a member-only map is an error - guard it
    // with `if (bus.isValidEventName(name))` or wrap the emit in try/catch.
    const std::optional<std::vector<std::string>> proven =
        call.arguments.front()
            ? eventNameLiteralValues(*call.arguments.front())
            : std::nullopt;
    if (!proven && !sh->hasIndex) {
        inferExpr(*call.arguments.front());
        report(diagcode::kTypeMismatch, call.arguments.front()->span,
               "the Events map has concrete event names; the event name must "
               "be a string literal - guard it at runtime first "
               "(`if (bus.isValidEventName(name))`) or wrap the emit in "
               "try/catch");
        out = Type::voidType();
        return true;
    }
    if (proven && proven->size() > 1) {
        // A literal union of event keys (`const e = "a" | "b"`): every member
        // must name a declared event (the map has no index signature here).
        for (const std::string& name : *proven) {
            if (findField(sh, name) == nullptr && !sh->hasIndex) {
                report(diagcode::kMemberNotFound, call.arguments.front()->span,
                       "no event '" + name + "' in the Events map");
                out = Type::voidType();
                return true;
            }
        }
        inferExpr(*call.arguments.front());
        out = Type::voidType();
        return true;
    }
    const std::string event =
        proven ? proven->front() : std::string();  // a dynamic name is allowed
    if (call.arguments.front()) inferExpr(*call.arguments.front());  // side effects
    const SourceSpan nameSpan = call.arguments.front()->span;
    // A literal name lands here; a dynamic name uses the index signature.
    const Type function = eventMapListenerFor(map, event, nameSpan);
    if (function.isUnknown()) {
        // Map lookup failed: report already. Continue quietly.
        out = Type::voidType();
        return true;
    }
    if (emitting) {
        // `emit(name, ...args)`: args must match the listener's parameter
        // types (minus its return value). A trailing array parameter of the
        // value signature is the rest indicator (`...args: any[]`).
        const MapSignature sig = mapSignatureOf(function);
        if (!sig.rest && call.arguments.size() != sig.prefix + 1) {
            report(diagcode::kArityMismatch, call.span,
                   "event '" + event + "' takes " + std::to_string(sig.prefix) +
                       " payload argument(s), not " +
                       std::to_string(call.arguments.size() - 1));
            out = Type::voidType();
            return true;
        }
        for (std::size_t i = 1; i < call.arguments.size(); ++i) {
            Expr* arg = call.arguments[i].get();
            if (!arg) continue;
            Type at = inferExpr(*arg);
            const std::size_t paramIndex = i - 1;
            checkAssign(at, mapSlotType(function, paramIndex), arg->span, false);
        }
        out = Type::voidType();
        return true;
    }
    // on / once / off: the listener is the second argument.
    Expr* listener = call.arguments[1].get();
    if (!listener) {
        out = Type::voidType();
        return true;
    }
    if (const auto* arrow = static_cast<const ArrowFunction*>(
            listener->kind == NodeKind::ArrowFunction ? listener : nullptr)) {
        // Contextually type the arrow's parameters from the map, so an
        // annotationless `(m) => log(m.text)` resolves `m` to the map's
        // parameter type.
        contextualArrowTypes_[arrow->nodeId] = function;
        Type at = inferExpr(*listener);
        contextualArrowTypes_.erase(arrow->nodeId);
        if (at.kind != TypeKind::Function) {
            report(diagcode::kTypeMismatch, listener->span,
                   "the listener must be a function");
            out = Type::voidType();
            return true;
        }
        // The annotations must agree with the map (mutual assignability), and
        // the handler may not declare more parameters than the map provides.
        checkListenerAnnotations(*arrow, function, arrow->span);
        out = Type::voidType();
        return true;
    }
    Type at = inferExpr(*listener);
    if (!wouldAccept(at, function, false)) {
        report(diagcode::kTypeMismatch, listener->span,
               "the listener does not match the event '" + event + "'s "
               "signature in the Events map");
    }
    out = Type::voidType();
    return true;
}

}  // namespace cp3
