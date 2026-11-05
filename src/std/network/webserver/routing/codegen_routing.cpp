// =============================================================================
// src/std/network/webserver/routing/codegen_routing.cpp - webserver complex
// routing lowering (codegen fragment).
//
// OWNERSHIP: the std/network family, the `webserver/routing` sub-family
// (src/std/network/webserver/routing/** ONLY). Like sema_routing.cpp this is a
// wirable fragment against the Generator vocabulary (detail/generator/
// generator.h); the lead's splice lines (WIRING.md) add these methods and
// dispatch to them from emitNetworkMemberCall / emitNetworkIdentifier.
//
// Lowering rules:
//   * app.route(path).get(h).post(h)  -> ((app).route(path)).get(h).post(h)
//     (RouteChain verbs return RouteChain&, chaining is the C++ reference).
//   * handler arrays: app.get(path, [h1, h2]) becomes the HandlerList overload
//     `get(path, {makeHandlerWithNext(h1), makeHandlerWithNext(h2)})`.
//     Array elements are lowered with the 3-arity context (req, res, next) so
//     next() inside an array handler advances; a single handler keeps the
//     existing 2-arity get(F) (next unavailable, as on HttpServer today).
//   * variadic tails: get(path, h1, h2, h3) collects h1..hn into the array.
//   * app.param(name, fn) -> param(name, makeParam(fn)) with the 4-arity
//     context (req, res, next, value: string).
//   * Router() / webserver() -> webserver::makeRouter() / webserver::makewebserverApp().
//     Router({ mergeParams: true, strict: true }) reads the literal flags
//     into makeRouter(mergeParams, strict) (non-literal values are reported
//     as a type error; the flags are construction-time constants).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kwebserver = "::cp3::runtime::network::webserver::";
constexpr const char* kRouterMarker = "$network:Router";
constexpr const char* kRouteChainMarker = "$network:RouteChain";
constexpr const char* kwebserverAppMarker = "$network:webserverApp";
constexpr const char* kHttpServerMarker = "$network:HttpServer";

constexpr const char* kReqType = "::cp3::runtime::network::HttpRequest&";
constexpr const char* kResType = "::cp3::runtime::network::HttpResponse&";
constexpr const char* kNextType = "::cp3::runtime::network::Next";

/// The context type a param callback's `value` parameter carries.
constexpr const char* kValueType = "std::string";

Type contextType(const std::string& cpp) {
    if (cpp == kReqType || cpp == "::cp3::runtime::network::HttpRequest") {
        return Type::named(TypeKind::Object, "$network:HttpRequest");
    }
    if (cpp == kResType || cpp == "::cp3::runtime::network::HttpResponse") {
        return Type::named(TypeKind::Object, "$network:HttpResponse");
    }
    if (cpp == kNextType) return Type::named(TypeKind::Object, "$network:Next");
    if (cpp == "std::string") return Type::string();
    return Type::unknown();
}

}  // namespace

/// Lowers one handler argument with the (req, res, next) context so an
/// array/variadic handler may call next(); wraps the lambda in
/// makeHandlerWithNext so the next() actually advances the handler list.
std::string Generator::routingHandler(const Expr& arg) {
    if (arg.kind != NodeKind::ArrowFunction) return expr(arg);
    const std::vector<std::string> context = {kReqType, kResType, kNextType};
    const auto& arrow = static_cast<const ArrowFunction&>(arg);
    pushScope();
    for (std::size_t i = 0; i < arrow.params.size() && i < context.size(); ++i) {
        const Parameter* p = arrow.params[i].get();
        if (!p || !p->name || p->name->kind != NodeKind::Identifier) continue;
        const Type type = contextType(context[i]);
        if (type.isUnknown()) continue;
        declare(static_cast<const Identifier&>(*p->name).name, type);
    }
    std::string text;
    if (arrow.isAsync) {
        // Same coroutine shape as networkArg's async HTTP handler arm.
        std::optional<Type> expectedFn;
        if (expected_ && expected_->kind == TypeKind::Function) expectedFn = *expected_;
        ++nestingDepth_;
        pushScope();
        const std::vector<std::string> params = arrowParamText(arrow, context, expectedFn);
        std::string signature;
        const std::string body = arrowBodyText(arrow, true, expectedFn, signature);
        popScope();
        --nestingDepth_;
        text = "[&](" + joinStrings(params, ", ") + ") -> " + signature + " {\n" + body + "}";
    } else {
        text = emitArrow(arrow, context);
    }
    popScope();
    return std::string(kwebserver) + "detail_ext::makeHandlerWithNext(" + text + ")";
}

/// The handler-list text for `path` + `handlerArgs` where handlerArgs is
/// arguments[1..]: one array literal, or one-or-more variadic handlers.
std::string Generator::routingHandlers(const ExprList& args) {
    std::vector<std::string> handlers;
    bool arrayForm = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const Expr* arg = args[i].get();
        if (!arg) continue;
        if (arg->kind == NodeKind::ArrayLiteral) {
            arrayForm = true;
            const auto& literal = static_cast<const ArrayLiteral&>(*arg);
            for (const auto& element : literal.elements) {
                if (!element) continue;
                handlers.push_back(routingHandler(*element));
            }
            continue;
        }
        handlers.push_back(routingHandler(*arg));
    }
    (void)arrayForm;
    if (handlers.empty()) return std::string(kwebserver) + "HandlerList{}";
    return std::string(kwebserver) + "HandlerList{" + joinStrings(handlers, ", ") + "}";
}

/// `app.param(name, fn)`: the callback is lowered with the 4-arity context
/// and routed through webserver::detail_ext::makeParam.
std::string Generator::routingParamCallback(const Expr& arg) {
    if (arg.kind != NodeKind::ArrowFunction) return expr(arg);
    const std::vector<std::string> context = {kReqType, kResType, kNextType, kValueType};
    const auto& arrow = static_cast<const ArrowFunction&>(arg);
    pushScope();
    for (std::size_t i = 0; i < arrow.params.size() && i < context.size(); ++i) {
        const Parameter* p = arrow.params[i].get();
        if (!p || !p->name || p->name->kind != NodeKind::Identifier) continue;
        const Type type = contextType(context[i]);
        if (type.isUnknown()) continue;
        declare(static_cast<const Identifier&>(*p->name).name, type);
    }
    std::string text;
    if (arrow.isAsync) {
        std::optional<Type> expectedFn;
        if (expected_ && expected_->kind == TypeKind::Function) expectedFn = *expected_;
        ++nestingDepth_;
        pushScope();
        const std::vector<std::string> params = arrowParamText(arrow, context, expectedFn);
        std::string signature;
        const std::string body = arrowBodyText(arrow, true, expectedFn, signature);
        popScope();
        --nestingDepth_;
        text = "[&](" + joinStrings(params, ", ") + ") -> " + signature + " {\n" + body + "}";
    } else {
        text = emitArrow(arrow, context);
    }
    popScope();
    return std::string(kwebserver) + "detail_ext::makeParam(" + text + ")";
}

/// `Router({ mergeParams?, strict? })` option flags from an object literal.
/// `webserver()` with no argument uses the defaults (false, false).
std::string Generator::routingOptions(const Expr* options) {
    if (!options || options->kind != NodeKind::ObjectLiteral) {
        return "cp3::runtime::network::webserver::RouterOptions{}";
    }
    const auto& literal = static_cast<const ObjectLiteral&>(*options);
    std::string mergeParams = "false";
    std::string strict = "false";
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* property = propertyPtr.get();
        if (!property || !property->value || property->isMethod || !property->key ||
            property->key->kind != NodeKind::Identifier) {
            continue;
        }
        const std::string field = static_cast<Identifier&>(*property->key).name;
        if (field == "mergeParams" || field == "strict") {
            if (property->value->kind != NodeKind::BoolLiteral) {
                reportTypeError(*property->value,
                                "type mismatch: Router option '" + field +
                                    "' must be a boolean literal");
                continue;
            }
            const bool value = static_cast<const BoolLiteral&>(*property->value).value;
            (field == "mergeParams" ? mergeParams : strict) = value ? "true" : "false";
        }
    }
    return "cp3::runtime::network::webserver::RouterOptions{" + mergeParams + ", " +
           strict + "}";
}

/// The member-call dispatch for routing receivers (HttpServer with the
/// forwarding members, Router, webserverApp, RouteChain). "" = not ours.
std::string Generator::emitRoutingMemberCall(const CallExpr& c, const MemberExpr& m) {
    needsNetwork_ = true;
    needsEventLoop_ = true;
    const Type receiver = effectiveType(*m.object);
    const bool isRouter = receiver.name == kRouterMarker;
    const bool isApp = receiver.name == kwebserverAppMarker;
    const bool isChain = receiver.name == kRouteChainMarker;
    if (!isRouter && !isApp && !isChain && receiver.name != kHttpServerMarker) {
        return std::string();
    }

    if (m.property == "route") {
        if (c.arguments.empty() || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "route(path) takes a path argument");
            return "0.0";
        }
        return "(" + expr(*m.object) + ").route(" + expr(*c.arguments.front()) + ")";
    }
    if (m.property == "param") {
        if (c.arguments.size() < 2 || !c.arguments[0] || !c.arguments[1]) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "param(name, callback) takes two arguments");
            return "0.0";
        }
        return "(" + expr(*m.object) + ").param(" + expr(*c.arguments[0]) + ", " +
               routingParamCallback(*c.arguments[1]) + ")";
    }
    if (m.property == "use" && !isChain) {
        // use(mw) stays on the generic network path; use(mount, router)
        // forwards verbatim (Router& overload).
        if (c.arguments.size() >= 2) {
            return "(" + expr(*m.object) + ").use(" + expr(*c.arguments[0]) + ", " +
                   expr(*c.arguments[1]) + ")";
        }
    }
    // Verb registrations (RouteChain verbs take no path argument).
    const bool isVerb = m.property == "get" || m.property == "post" ||
                        m.property == "put" || m.property == "delete" ||
                        m.property == "patch" || m.property == "head" ||
                        m.property == "options" || m.property == "all";
    if (isVerb && !isChain) {
        if (c.arguments.empty() || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               m.property + "(path, handler) takes two arguments");
            return "0.0";
        }
    }
    if (isVerb) {
        if (isChain) {
            // Single handler keeps the 2-arity template overload; an array or
            // variadic tail uses the HandlerList overload.
            bool arrayTail = false;
            for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                if (c.arguments[i] && c.arguments[i]->kind == NodeKind::ArrayLiteral) {
                    arrayTail = true;
                }
            }
            if (arrayTail || c.arguments.size() > 1) {
                return "(" + expr(*m.object) + ")." + m.property + "(" +
                       routingHandlers(c.arguments) + ")";
            }
            return "(" + expr(*m.object) + ")." + m.property + "(" + networkArgs(receiver, m.property, c) + ")";
        }
        // Router / webserverApp / HttpServer verbs: single handler via the
        // existing networkArgs path, array/variadic via routingHandlers.
        bool single = c.arguments.size() == 2 && c.arguments[1] &&
                      c.arguments[1]->kind != NodeKind::ArrayLiteral;
        if (single) {
            return "(" + expr(*m.object) + ")." + m.property + "(" + networkArgs(receiver, m.property, c) + ")";
        }
        return "(" + expr(*m.object) + ")." + m.property + "(" +
               expr(*c.arguments.front()) + ", " + routingHandlers(c.arguments) + ")";
    }
    return std::string();
}

}  // namespace cp3
