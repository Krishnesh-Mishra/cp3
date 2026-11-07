// =============================================================================
// src/codegen/src/emit/worker_listener.cpp - the worker message listener.
//
// A worker listener is the `on("message", l)` / `on("error", l)` callback of
// either side of a channel (D80, spec §13). The listener is an inline arrow:
//
//   * a plain name parameter receives the message payload, typed by the
//     channel direction (the `In` type on the worker side, the `Out` type on
//     the main side);
//   * a destructuring parameter receives the whole payload and binds its
//     fields at the top of the body, each field keeping its own type;
//   * a void direction has no payload at all, so the listener is nullary.
//
// The "error" event carries the thrown value, not a channel payload.
//
// The handle member surface itself lives in emit/worker_handles.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"
#include "detail/worker/worker.h"

namespace cp3 {

using namespace detail;

namespace {

// The member name a pattern property key spells (`{ name }`, `{ "x": y }`),
// or "" for a key form the shape machinery cannot read.
std::string workerPatternKey(const Expr& key) {
    if (key.kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(key).name;
    }
    if (key.kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(key).value;
    }
    return std::string();
}

}  // namespace

// A message listener's destructuring parameter: the payload arrives whole
// (the channel's `In`/`Out` type) and the pattern binds its fields at the
// top of the body. Each field keeps its own type, so the body sees the same
// types a plain parameter would give it.
void Generator::workerListenerPattern(const Parameter& p, const Type& payload,
                                      const std::string& src,
                                      std::string& body) {
    if (!p.name || p.name->kind != NodeKind::ObjectPattern) {
        reportWorkerCode(p, diagcode::kWorkerListenerParameter,
                         "this worker listener's parameter must be a name or an "
                         "object pattern over the message payload");
        return;
    }
    const auto& op = static_cast<const ObjectPattern&>(*p.name);
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(payload, structName, fields)) {
        reportWorkerCode(p, diagcode::kWorkerListenerParameter,
                         "this worker listener's parameter does not match the "
                         "message payload type");
        return;
    }
    detail::PatternEmitter emitter(*this);
    for (const auto& prop : op.properties) {
        if (!prop || prop->computed || !prop->value || !prop->key) {
            reportWorkerCode(p, diagcode::kWorkerListenerParameter,
                             "this worker listener's parameter does not match "
                             "the message payload type");
            return;
        }
        const std::string name = workerPatternKey(*prop->key);
        const ObjectField* field = nullptr;
        for (const ObjectField& candidate : fields) {
            if (candidate.name == name) {
                field = &candidate;
                break;
            }
        }
        if (name.empty() || field == nullptr) {
            reportWorkerCode(p, diagcode::kWorkerListenerParameter,
                             "this worker listener's parameter names '" + name +
                                 "', which the message payload does not carry");
            return;
        }
        const std::string access = "((" + src + ")->" + mapIdent(name) + ")";
        emitter.walkPattern(*prop->value,
                            detail::PatternEmitter::Site{access, field->type},
                            body, 1);
    }
}

std::string Generator::workerListenerText(const ArrowFunction& arrow,
                                          const Type& direction,
                                          const std::string& ownerNS,
                                          bool error,
                                          bool workerSide) {
    if (arrow.params.size() > 1) {
        reportWorkerCode(arrow, diagcode::kWorkerHandleMember,
                         "a worker listener takes at most one parameter");
        return "0.0";
    }
    if (error) {
        return workerListenerLambda(arrow, "::cp3::runtime::Thrown",
                                    Type::unknown(), workerSide);
    }
    if (direction.isVoid()) {
        return workerListenerLambda(arrow, std::string(), Type::undefined(), workerSide);
    }
    const std::string cpp = workerPayloadText(direction, ownerNS);
    if (cpp.empty()) {
        reportWorkerCode(arrow, diagcode::kWorkerPayloadNotCopyable,
                         "this worker listener's payload must be a copyable "
                         "value type");
        return "0.0";
    }
    return workerListenerLambda(arrow, cpp, direction, workerSide);
}

std::string Generator::workerListenerLambda(const ArrowFunction& arrow,
                                            const std::string& paramType,
                                            const Type& bound,
                                            bool workerSide) {
    ++nestingDepth_;
    pushScope();
    const std::string savedReturn = currentReturn_;
    const bool savedInAsync = inAsyncBody_;
    currentReturn_ = "void";
    inAsyncBody_ = false;
    const Parameter* p = arrow.params.empty() ? nullptr : arrow.params[0].get();
    const bool plainName = p != nullptr && p->name &&
                           p->name->kind == NodeKind::Identifier;
    std::string paramList;
    std::string body;
    if (paramType.empty()) {
        // A void direction: a nullary lambda; a declared parameter is dead
        // code, so it is declared (never read) to keep the body compiling.
        if (plainName) {
            const std::string raw =
                static_cast<const Identifier&>(*p->name).name;
            const std::string name = mapIdent(raw);
            body += "::cp3::runtime::Undefined " + name +
                    " = ::cp3::runtime::undefined;\n";
            body += "(void)" + name + ";\n";
            declare(raw, Type::undefined());
        } else if (p != nullptr) {
            // A pattern parameter with no payload to bind: the channel
            // direction is empty, so there is nothing to destructure.
            reportWorkerCode(*p, diagcode::kWorkerListenerParameter,
                             "this worker listener has no message payload (the "
                             "channel direction is empty), so its parameter "
                             "cannot bind anything");
        }
    } else {
        std::string name = "cp3_value";
        if (plainName) {
            const std::string raw =
                static_cast<const Identifier&>(*p->name).name;
            name = mapIdent(raw);
            declare(raw, bound);
        } else if (p != nullptr && p->name) {
            // A destructuring parameter: the payload arrives whole and the
            // pattern binds its fields at the top of the body, each with the
            // field's own type (the payload is a record, so the fields and
            // their types are known).
            workerListenerPattern(*p, bound, name, body);
        } else if (p != nullptr) {
            reportWorkerCode(*p, diagcode::kWorkerListenerParameter,
                             "this worker listener's parameter must be a name or "
                             "a binding pattern");
        }
        paramList = paramType + " " + name;
    }
    if (arrow.body && arrow.expressionBody) {
        body += expr(*static_cast<const Expr*>(arrow.body.get())) + ";";
    } else if (arrow.body) {
        stmt(*static_cast<const Stmt*>(arrow.body.get()), body, 1);
    }
    inAsyncBody_ = savedInAsync;
    currentReturn_ = savedReturn;
    popScope();
    --nestingDepth_;
    // A listener lives in the channel's listener vector until terminate.
    // The worker-side entry returns immediately, so its captures must be
    // copied (`[=] mutable`): a reference capture would dangle; `thread`
    // captures fine because the facade is copyable. The main-side handle is
    // non-copyable, and its lifetime already outlives the invocations (the
    // listener's lifetime is the handle's; `terminate()` closes the ports
    // before the handle dies), so capture by reference there.
    if (workerSide) {
        return "[=] (" + paramList + ") mutable {\n" + body + "}";
    }
    return "[&] (" + paramList + ") {\n" + body + "}";
}

}  // namespace cp3
