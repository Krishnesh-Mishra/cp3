// =============================================================================
// src/sema/src/checks/call/event_emitter_check.cpp - typed EventEmitter calls.
//
// A `EventEmitter<Events>` receiver (the Events map lives in parts[0]) checks
// its `on` / `once` / `off` / `emit` arguments against that map (v0.2.4): a
// literal event name picks the member (or the index signature) of the map, the
// listener is checked against the chosen function type, and an `emit`'s
// arguments are checked like a call of that function. This mirrors the fs
// listener strategy (the payload follows the event name) at the sema level,
// which gives contextual typing of the listener's parameters. Codegen
// (emit/call/call.cpp) erases the values the same way the untyped bus does.
// =============================================================================
#include "detail/analyzer.h"

#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

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
        // Dynamic names of one payload type (design doc): the index signature
        // is the universal event slot.
        return sh->indexType;
    }
    report(diagcode::kMemberNotFound, span,
           "no event '" + event + "' in the Events map");
    return Type::voidType();
}

bool Analyzer::tryInferEventEmitterCall(CallExpr& call, MemberExpr& m,
                                        Type& out) {
    if (!m.object) return false;
    Expr* objectArg = m.object.get();
    const Type recv = typeOfExpr(*objectArg);
    const bool typed = isEventEmitterType(recv) && !recv.parts.empty();
    // The untyped EventEmitter stays on the generic path.
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
    // The event name must be a string literal for the map check to bite.
    std::string event;
    if (Expr* first = call.arguments.front().get()) {
        if (first->kind == NodeKind::StringLiteral) {
            event = static_cast<const StringLiteral&>(*first).value;
        } else {
            inferExpr(*first);
            const Shape* toShape = shapeOf(map);
            if (toShape == nullptr || !toShape->hasIndex) {
                report(diagcode::kTypeMismatch, call.span,
                       "the Events map has concrete event names; the event "
                       "name must be a string literal");
                out = Type::voidType();
                return true;
            }
            // An index-signature map accepts a dynamic name (design doc).
            event = std::string();
        }
    }
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
        // types (minus its return value).
        inferExpr(*call.arguments.front());  // already done above, cheap
        const std::size_t want = function.parts.size() > 1
                                     ? function.parts.size() - 1
                                     : 0;
        if (call.arguments.size() != want + 1) {
            report(diagcode::kArityMismatch, call.span,
                   "event '" + event + "' takes " + std::to_string(want) +
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
            checkAssign(at, function.parts[paramIndex], arg->span, false);
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
    inferExpr(*call.arguments.front());  // known-done; keep side effects
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
        }
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
