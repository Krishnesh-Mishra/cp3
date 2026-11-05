// =============================================================================
// src/std/network/codegen_methods.cpp - `std/network` member surface.
//
// OWNERSHIP: the std/network family (src/std/network/**). The member call/read
// lowering on the documented receivers: the handler argument typing
// (networkArg/networkArgs), the Map-shaped field reads (`res.headers`,
// `req.params/query/headers`; exprTypeHints_), the json dispatch
// (`res.json<T>()`, `res.json(payload)`) and the known read list. Split out of
// the sibling codegen.cpp (construction: fetch, createServer, new WebSocket,
// the named imports and the `network.*` namespace corner) so both files stay
// under the source-size budget (STYLE.md sec.2).
// =============================================================================
#include "detail/generator/generator.h"

#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/class/class_model.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/std_integer.hpp"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kFetchResponse = "$network:FetchResponse";
constexpr const char* kHttpRequest = "$network:HttpRequest";
constexpr const char* kHttpResponse = "$network:HttpResponse";
constexpr const char* kHttpServer = "$network:HttpServer";
constexpr const char* kWebSocket = "$network:WebSocket";
constexpr const char* kWebSocketServer = "$network:WebSocketServer";
constexpr const char* kHeaders = "$network:Headers";  // Ledger L (appended)
constexpr const char* kRequest = "$network:Request";
constexpr const char* kResponse = "$network:Response";
constexpr const char* kFormData = "$network:FormData";
constexpr const char* kAbortSignal = "$network:AbortSignal";
constexpr const char* kRouter = "$network:Router";        // Router agent
constexpr const char* kRoute = "$network:Route";          // Router agent
constexpr const char* kRouteChain = "$network:RouteChain";  // Routing agent
constexpr const char* kwebserverApp = "$network:webserverApp";  // Routing agent
constexpr const char* kHttpError = "$network:HttpError";  // Errors agent

constexpr const char* kReqType = "::cp3::runtime::network::HttpRequest&";
constexpr const char* kResType = "::cp3::runtime::network::HttpResponse&";
constexpr const char* kNextType = "::cp3::runtime::network::Next";
constexpr const char* kThrownType = "::cp3::runtime::Thrown";

/// The marker type a middleware's `next` parameter carries in the temporary
/// handler scope below: the `$network:` prefix matches the sema markers, so a
/// body call to `next()` is recognised as the runtime continuation and lowers
/// to the plain `next()` member call (`Next::operator()`).
constexpr const char* kNextMarker = "$network:Next";

/// `Map<string, string>` (std/collections): the type behind the documented
/// Map-shaped network fields (`res.headers`, `req.params`, `req.query`,
/// `req.headers`). The marker objects are open, so codegen hands this type to
/// the collections lowering itself (exprTypeHints_).
Type stringMapType() {
    Type map(TypeKind::Class, "Map");
    map.parts.push_back(Type::string());
    map.parts.push_back(Type::string());
    return map;
}

/// The documented listener parameter types for one event name.
std::vector<std::string> eventParams(const std::string& event) {
    if (event == "message") return {std::string("std::string")};
    if (event == "close") return {"double", "std::string"};
    if (event == "error") return {std::string(kThrownType)};
    if (event == "connection") {
        return {std::string("::cp3::runtime::network::WebSocket&"),
                std::string("::cp3::runtime::network::HttpRequest")};
    }
    return {};  // "open" and anything unlisted: no parameters
}

/// The event name of a listener registration: the first argument's literal.
/// A spread argument carries the name inside its array literal, so the lookup
/// looks through it (`ws.on(...["message", handler])`).
std::string networkEventName(const CallExpr& c) {
    if (c.arguments.empty() || !c.arguments.front()) return std::string();
    const Expr* first = c.arguments.front().get();
    if (first->kind == NodeKind::SpreadElement) {
        const auto& spread = static_cast<const SpreadElement&>(*first);
        if (!spread.argument ||
            spread.argument->kind != NodeKind::ArrayLiteral) {
            return std::string();
        }
        const auto& literal = static_cast<const ArrayLiteral&>(*spread.argument);
        if (literal.elements.empty() || !literal.elements.front()) {
            return std::string();
        }
        const Expr* head = literal.elements.front().get();
        if (!head || head->kind != NodeKind::StringLiteral) return std::string();
        return static_cast<const StringLiteral&>(*head).value;
    }
    if (first->kind != NodeKind::StringLiteral) return std::string();
    return static_cast<const StringLiteral&>(*first).value;
}

/// The sema marker type behind one documented C++ parameter. Sema keeps the
/// documented callback parameters permissive, so the emitter types the lambda's
/// own parameter names itself: a member call inside the body (`res.text(...)`,
/// `ws.send(...)`) must still find the network tables.
Type contextType(const std::string& cpp) {
    if (cpp == kReqType || cpp == "::cp3::runtime::network::HttpRequest") {
        return Type::named(TypeKind::Object, kHttpRequest);
    }
    if (cpp == kResType || cpp == "::cp3::runtime::network::HttpResponse") {
        return Type::named(TypeKind::Object, kHttpResponse);
    }
    if (cpp == "::cp3::runtime::network::WebSocket&" ||
        cpp == "::cp3::runtime::network::WebSocket") {
        return Type::named(TypeKind::Object, kWebSocket);
    }
    if (cpp == kNextType) return Type::named(TypeKind::Object, kNextMarker);
    if (cpp == kThrownType) return Type::unknown();  // Ledger L: onError err param stays ambient in sema
    if (cpp == "std::string" || cpp == "const std::string&") return Type::string();  // Router [C7]
    if (cpp == "::cp3::runtime::network::webserver::errors::HttpError" ||
        cpp == "::cp3::runtime::network::webserver::errors::HttpError&") {
        return Type::named(TypeKind::Object, kHttpError);
    }
    if (cpp == "double") return Type::number();
    return Type::unknown();
}

/// The C++ spelling of the explicit type arguments on a network member call
/// (`res.json<T>()`, docs 04 §network). `json<T>` is the only generic member;
/// a primitive T spells directly, and a named/interface T is routed through
/// the json record tables before this function runs (networkJsonRecord).
/// Anything else (a compound type, a non-registered named type) is a type
/// error at the use site.
bool networkTypeArgsText(const TypeList& typeArgs, std::string& out) {
    if (typeArgs.empty()) return true;
    std::vector<std::string> spellings;
    for (const auto& arg : typeArgs) {
        if (!arg) return false;
        const std::optional<Type> t = typeFromTypeNode(*arg);
        if (!t) return false;
        const CppInfo ci = classify(*t);
        if (!ci.representable || !isPrimitiveShape(ci.shape)) return false;
        spellings.push_back(ci.cpp);
    }
    out = "<" + joinStrings(spellings, ", ") + ">";
    return true;
}

/// `app.delete(...)` -> the runtime member `del`: `delete` is a C++ keyword,
/// so the HttpServer handle cannot spell it (docs 04 §network lists `.delete`).
std::string networkMemberName(const Type& receiver, const std::string& property) {
    if (receiver.name == kHttpServer && property == "delete") return "del";
    // Router agent [C5].
    if ((receiver.name == kRouter || receiver.name == kRoute ||
         receiver.name == kRouteChain || receiver.name == kwebserverApp) &&
        property == "delete") return "del";
    // Ledger L (appended): JS `delete` is not a C++ identifier.
    if ((receiver.name == kHeaders || receiver.name == kFormData) &&
        property == "delete") return "remove";
    return property;
}

bool isRegexRoutingVerb(const std::string& property) {
    return property == "get" || property == "post" || property == "put" ||
           property == "delete" || property == "patch" || property == "head" ||
           property == "options" || property == "any" || property == "all";
}

}  // namespace

/// The documented field read behind an webserverion (`res.headers`,
/// `req.params`, `req.query`, `req.headers`, `req.method`, `req.path`,
/// `res.ok`, `res.status`, `res.statusText`), with the Map fields as
/// `Map<string, string>` (docs 04 §network), or nullopt. The `get(k)` result
/// of such a field is `string | undefined` (std/collections Map), so a
/// `??`/`=== undefined` on it classifies as the optional shape even when the
/// receiver is a handler parameter that sema only sees as ambient.
std::optional<Type> Generator::networkFieldType(const Expr& e) const {
    // A recorded hint wins; it is how the Map type reaches `effectiveType`
    // for handler parameters that sema only sees as ambient.
    if (const auto it = exprTypeHints_.find(e.nodeId); it != exprTypeHints_.end()) {
        return it->second;
    }
    if (e.kind == NodeKind::CallExpr) {
        const auto& c = static_cast<const CallExpr&>(e);
        if (!c.callee || c.callee->kind != NodeKind::MemberExpr) return std::nullopt;
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        if (m.property == "get" && m.object && networkFieldType(*m.object)) {
            return Type::unionOf({Type::string(), Type::undefined()});
        }
        return std::nullopt;
    }
    if (e.kind != NodeKind::MemberExpr) return std::nullopt;
    const auto& m = static_cast<const MemberExpr&>(e);
    if (!m.object) return std::nullopt;
    const std::string& recv = effectiveType(*m.object).name;
    if (recv == kFetchResponse) {
        if (m.property == "headers") return stringMapType();
        if (m.property == "ok") return Type::boolean();
        if (m.property == "status") {
            return stdIntegerType(StdIntegerKind::Status, options_.typeSystem);
        }
        if (m.property == "statusText") return Type::string();
        return std::nullopt;
    }
    // Ledger L (appended) reads.
    if (recv == kRequest) {
        if (m.property == "url" || m.property == "method") return Type::string();
        if (m.property == "headers") return Type::named(TypeKind::Object, kHeaders);
        if (m.property == "signal") return Type::named(TypeKind::Object, kAbortSignal);
        return std::nullopt;
    }
    if (recv == kResponse) {
        if (m.property == "ok") return Type::boolean();
        if (m.property == "status") return stdIntegerType(StdIntegerKind::Status, options_.typeSystem);
        if (m.property == "statusText" || m.property == "url") return Type::string();
        if (m.property == "headers") return Type::named(TypeKind::Object, kHeaders);
        return std::nullopt;
    }
    if (recv == kAbortSignal && m.property == "aborted") return Type::boolean();
    if (recv == kHttpRequest) {
        if (m.property == "params" || m.property == "query" ||
            m.property == "headers" || m.property == "cookies" ||
            m.property == "signedCookies") {
            return stringMapType();
        }
        if (m.property == "method" || m.property == "path" ||
            m.property == "ip") {
            return Type::string();
        }
        // Router agent [C3].
        if (m.property == "baseUrl" || m.property == "originalUrl") {
            return Type::string();
        }
    }
    // Response agent: res.headersSent read type.
    if (recv == kHttpResponse && m.property == "headersSent") {
        return Type::boolean();
    }
    return std::nullopt;
}

/// A Map-shaped field's member call (`req.headers.get(k)`,
/// `res.headers.has(k)`): registers the field read as a Map in the codegen
/// type hints and re-uses the collections lowering unchanged. The hint is
/// needed because a handler parameter is ambient in sema, so the open marker
/// never carries the Map type to `effectiveType`.
std::string Generator::emitNetworkMapFieldCall(const CallExpr& c, const MemberExpr& m) {
    if (!m.object) return std::string();
    const std::optional<Type> type = networkFieldType(*m.object);
    if (!type) return std::string();
    exprTypeHints_[m.object->nodeId] = *type;
    return emitCollectionsMemberCall(c, m);
}

/// The `.size` read of a Map-shaped field (`req.params.size`); same hint as
/// the call form above.
std::string Generator::emitNetworkMapFieldMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const std::optional<Type> type = networkFieldType(*m.object);
    if (!type) return std::string();
    exprTypeHints_[m.object->nodeId] = *type;
    return emitCollectionsMember(m);
}

// =============================================================================
// Argument lowering
// =============================================================================

/// Lowers one argument; an arrow function becomes a lambda with the parameter
/// types the runtime callback expects for this call.
std::string Generator::networkArg(const Type& receiver, const std::string& property,
                                  const CallExpr& c, const Expr& arg, std::size_t index) {
    if (arg.kind != NodeKind::ArrowFunction) return expr(arg);
    std::vector<std::string> context;
    if (receiver.name == kHttpServer) {
        if (property == "listen") {
            context = {};
        } else if (property == "onError") {
            // Ledger L (appended): `(err, req, res)`. Errors agent: a 4-arg
            // `(err, req, res, next)` handler is also accepted; the 4th
            // parameter is the Next continuation.
            context = {kThrownType, kReqType, kResType};
            if (arg.kind == NodeKind::ArrowFunction &&
                static_cast<const ArrowFunction&>(arg).params.size() == 4) {
                context.push_back(kNextType);
            }
        } else if (property == "use") {
            context = {kReqType, kResType, kNextType};
        } else if (property == "param") {
            context = {kReqType, kResType, kNextType, "const std::string&"};
        } else {
            context = {kReqType, kResType};
        }
    } else if (receiver.name == kRouter || receiver.name == kwebserverApp) {
        // Router agent [C6].
        if (property == "param") {
            context = {kReqType, kResType, kNextType, "const std::string&"};
        } else if (property == "use") {
            context = {kReqType, kResType, kNextType};
        } else {
            context = {kReqType, kResType};
        }
    } else if (receiver.name == kRoute || receiver.name == kRouteChain) {
        // Router [C6]: router.route(path).get(handler): (req, res).
        // Routing agent: RouteChain verbs take handler lists, but the
        // single-handler form keeps the 2-arity context.
        context = {kReqType, kResType};
    } else if (receiver.name == kWebSocket || receiver.name == kWebSocketServer) {
        // The event name is the first literal argument.
        std::string event = "open";
        if (index > 0) {
            const std::string named = networkEventName(c);
            if (!named.empty()) event = named;
        }
        context = eventParams(event);
    }
    const auto& arrow = static_cast<const ArrowFunction&>(arg);
    // Type the lambda's parameter names for the body emission only: sema keeps
    // callback parameters permissive, so without this a member call inside the
    // handler (`res.text(...)`, `ws.send(...)`) would fall through to the
    // generic call path. The temporary scope keeps the names out of the
    // enclosing scope.
    pushScope();
    for (std::size_t i = 0; i < arrow.params.size() && i < context.size(); ++i) {
        const Parameter* p = arrow.params[i].get();
        if (!p || !p->name || p->name->kind != NodeKind::Identifier) continue;
        const Type type = contextType(context[i]);
        if (type.isUnknown()) continue;
        declare(static_cast<const Identifier&>(*p->name).name, type);
    }
    std::string text;
    if (arrow.isAsync && (receiver.name == kHttpServer ||
                          receiver.name == kRouter ||
                          receiver.name == kRoute ||
                          receiver.name == kRouteChain ||
                          receiver.name == kwebserverApp)) {
        // An `async` HTTP handler is a coroutine lambda. The runtime waits for
        // its promise before it continues the chain or answers 404, so
        // `await req.text()` inside the handler is safe. WebSocket event
        // callbacks are not in this path: their runtime callbacks do not await.
        if (intOn()) intFail();
        std::optional<Type> expectedFn;
        if (expected_ && expected_->kind == TypeKind::Function) {
            expectedFn = *expected_;
        }
        ++nestingDepth_;
        pushScope();
        const std::vector<std::string> params =
            arrowParamText(arrow, context, expectedFn);
        std::string signature;
        const std::string body =
            arrowBodyText(arrow, true, expectedFn, signature);
        popScope();
        --nestingDepth_;
        text = "[&](" + joinStrings(params, ", ") + ") -> " + signature +
               " {\n" + body + "}";
    } else {
        text = emitArrow(arrow, context);
    }
    popScope();
    return text;
}

std::string Generator::networkArgs(const Type& receiver, const std::string& property,
                                   const CallExpr& c) {
    std::string args;
    std::size_t index = 0;
    for (std::size_t i = 0; i < c.arguments.size(); ++i) {
        const Expr* arg = c.arguments[i].get();
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            // A spread argument in a std/network member call: the runtime
            // members are fixed-arity (`get(path, handler)`), so the spread
            // source is expanded into the positional arguments. An array
            // literal is the shape that can be expanded at compile time; any
            // other source is a type error (the C++ member has no variadic
            // form to forward it into).
            const auto& spread = static_cast<const SpreadElement&>(*arg);
            if (!spread.argument ||
                spread.argument->kind != NodeKind::ArrayLiteral) {
                reportTypeError(*arg,
                                "type mismatch: a spread argument of a "
                                "std/network member call needs an array "
                                "literal");
                continue;
            }
            const auto& literal = static_cast<const ArrayLiteral&>(*spread.argument);
            for (const auto& elementPtr : literal.elements) {
                const Expr* element = elementPtr.get();
                if (!element) continue;
                if (element->kind == NodeKind::SpreadElement) {
                    reportTypeError(*element,
                                    "type mismatch: a spread argument of a "
                                    "std/network member call cannot nest a "
                                    "spread");
                    continue;
                }
                if (!args.empty()) args += ", ";
                args += networkArg(receiver, property, c, *element, index);
                ++index;
            }
            continue;
        }
        if (!args.empty()) args += ", ";
        args += networkArg(receiver, property, c, *arg, index);
        ++index;
    }
    return args;
}

// =============================================================================
// Member calls / reads on network receivers
// =============================================================================

std::string Generator::emitNetworkMemberCall(const CallExpr& c, const MemberExpr& m) {
    needsNetwork_ = true;
    needsEventLoop_ = true;
    const Type receiver = effectiveType(*m.object);
    if (m.property == "json") {
        // `res.json<T>()` for a reified interface T: the usage-based json
        // record tables parse the buffered body into the generated struct.
        if (std::string record = networkJsonRecord(m, c); !record.empty()) {
            return record;
        }
        // `res.json(payload, status?)` with a literal payload: the json
        // dynamic value tree serializes it (cp3/std/json.hpp).
        if (std::string payload = networkResponseJson(c, m); !payload.empty()) {
            return payload;
        }
    }
    // Errors/middleware agents: `app.use((err, req, res, next) => ...)`
    // (4-arg arrow) is error middleware -> onError slot.
    if (receiver.name == kHttpServer && m.property == "use" &&
        c.arguments.size() == 1 && c.arguments[0] &&
        c.arguments[0]->kind == NodeKind::ArrowFunction &&
        static_cast<const ArrowFunction&>(*c.arguments[0]).params.size() == 4) {
        return "(" + expr(*m.object) + ").useError(" +
               networkArg(receiver, "onError", c, *c.arguments[0], 0) + ")";
    }
    // Router agent [C9]: `app.use("/api", router)` / `app.use(router)`.
    if (receiver.name == kHttpServer && m.property == "use") {
        const std::string pathArg =
            (c.arguments.size() >= 2 && c.arguments.front())
                ? expr(*c.arguments.front()) : std::string();
        const Expr* routerExpr = nullptr;
        if (c.arguments.size() >= 2 && c.arguments[1] &&
            effectiveType(*c.arguments[1]).name == kRouter) {
            routerExpr = c.arguments[1].get();
        } else if (c.arguments.size() == 1 && c.arguments.front() &&
                   effectiveType(*c.arguments.front()).name == kRouter) {
            routerExpr = c.arguments.front().get();
        }
        if (routerExpr != nullptr) {
            needsNetwork_ = true;
            const std::string routerText = expr(*routerExpr);
            if (pathArg.empty()) {
                return "(" + expr(*m.object) + ").use((" + routerText +
                       ").handler())";
            }
            return "(" + expr(*m.object) + ").use((" + routerText +
                   ").mountMiddleware(" + pathArg + "))";
        }
    }
    // Regex-routing agent: settings dispatch (before the verb hunk).
    if (receiver.name == kHttpServer &&
        (m.property == "set" || m.property == "enable" ||
         m.property == "disable" || m.property == "get" ||
         m.property == "enabled" || m.property == "disabled")) {
        if (m.property == "set" && c.arguments.size() == 2 &&
            c.arguments[0] && c.arguments[1] &&
            c.arguments[0]->kind == NodeKind::StringLiteral) {
            const std::string key =
                static_cast<StringLiteral&>(*c.arguments[0]).value;
            // Routing settings (bool value) vs cookie secret (string value).
            if (key == "case sensitive routing" || key == "strict routing") {
                return "(" + expr(*m.object) + ").setRoutingSetting(" +
                       expr(*c.arguments[0]) + ", " + expr(*c.arguments[1]) + ")";
            }
        }
        if (m.property == "enable" || m.property == "disable") {
            if (c.arguments.size() != 1 || !c.arguments[0]) return "0.0";
            return "(" + expr(*m.object) + ").setRoutingSetting(" +
                   expr(*c.arguments[0]) + ", " +
                   (m.property == "enable" ? "true" : "false") + ")";
        }
        if ((m.property == "get" || m.property == "enabled" ||
             m.property == "disabled") &&
            c.arguments.size() == 1 && c.arguments[0]) {
            std::string read = "(" + expr(*m.object) +
                               ").getRoutingSetting(" + expr(*c.arguments[0]) + ")";
            return m.property == "disabled" ? "(!" + read + ")" : read;
        }
        // app.set with a non-routing key falls through (cookie arm below).
        if (m.property != "set") {
            // get with 2 args is the route verb; handled by the verb hunk.
        }
    }
    // Regex-routing agent: verb dispatch (PathSpec / options forms).
    if (isRegexRoutingVerb(m.property) && receiver.name == kHttpServer) {
        const std::string methodName = networkMemberName(receiver, m.property);
        std::function<std::string(const Expr&)> pathText = [this, &pathText](const Expr& e) -> std::string {
            if (e.kind == NodeKind::RegExpLiteral) {
                const auto& re = static_cast<const RegExpLiteral&>(e);
                needsRegex_ = true;
                return "::cp3::runtime::network::regex_routing::PathSpec::makeRegex("
                       "::cp3::runtime::regex::Regex::compile(" +
                       cppStringLiteral(re.pattern) + ", " +
                       cppStringLiteral(re.flags) + "))";
            }
            if (e.kind == NodeKind::ArrayLiteral) {
                const auto& lit = static_cast<const ArrayLiteral&>(e);
                std::string inner;
                for (const auto& el : lit.elements) {
                    if (!el || el->kind == NodeKind::SpreadElement) {
                        reportTypeError(e, "type mismatch: a spread element in a route-path array cannot be classified at compile time");
                        continue;
                    }
                    if (!inner.empty()) inner += ", ";
                    inner += pathText(*el);
                }
                return "::cp3::runtime::network::regex_routing::PathSpec::makeArray({" + inner + "})";
            }
            return "::cp3::runtime::network::regex_routing::PathSpec::makeString(" + expr(e) + ")";
        };
        auto optionsText = [this](const Expr& arg) -> std::string {
            if (arg.kind != NodeKind::ObjectLiteral) {
                reportTypeError(arg, "type mismatch: route options must be an object literal { caseSensitive?: boolean, strict?: boolean }");
                return "::cp3::runtime::network::regex_routing::RoutingOptions{}";
            }
            const auto& lit = static_cast<const ObjectLiteral&>(arg);
            std::string cs = "std::nullopt", st = "std::nullopt";
            for (const auto& prop : lit.properties) {
                if (!prop || !prop->value || prop->isMethod || !prop->key) continue;
                std::string key;
                if (prop->key->kind == NodeKind::Identifier) key = static_cast<Identifier&>(*prop->key).name;
                else if (prop->key->kind == NodeKind::StringLiteral) key = static_cast<StringLiteral&>(*prop->key).value;
                else continue;
                if (key == "caseSensitive") cs = expr(*prop->value);
                else if (key == "strict") st = expr(*prop->value);
            }
            return "::cp3::runtime::network::regex_routing::RoutingOptions{" + cs + ", " + st + "}";
        };
        if (c.arguments.size() == 3 && c.arguments[0] && c.arguments[1] && c.arguments[2]) {
            needsNetwork_ = true;
            const std::string path = pathText(*c.arguments[0]);
            const std::string opts = optionsText(*c.arguments[1]);
            const std::string handler = networkArg(receiver, m.property, c, *c.arguments[2], 2);
            const std::string methodLit = methodName == "del" ? "\"DELETE\""
                : methodName == "all" || methodName == "any" ? "\"*\""
                : "\"" + [&] { std::string u = methodName; for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); return u; }() + "\"";
            return "::cp3::runtime::network::addRegexRoute((" + expr(*m.object) + "), " + methodLit + ", " + path + ", " + opts + ", " + handler + ")";
        }
        if (c.arguments.size() == 2 && c.arguments[0] && c.arguments[1]) {
            const Expr* first = c.arguments[0].get();
            const bool isStringPath = first && first->kind != NodeKind::RegExpLiteral &&
                                      first->kind != NodeKind::ArrayLiteral &&
                                      effectiveType(*first).name != "RegExp";
            const bool hasHandlerList = c.arguments[1]->kind == NodeKind::ArrayLiteral;
            if (isStringPath && !hasHandlerList) {
                // Keep today's call shape exactly for string paths.
                return "(" + expr(*m.object) + ")." + methodName + "(" +
                       networkArg(receiver, m.property, c, *first, 0) + ", " +
                       networkArg(receiver, m.property, c, *c.arguments[1], 1) + ")";
            }
            if (isStringPath && hasHandlerList) {
                // Routing agent: array handlers on a string path.
                std::vector<std::string> handlers;
                const auto& literal = static_cast<const ArrayLiteral&>(*c.arguments[1]);
                for (const auto& element : literal.elements) {
                    if (!element) continue;
                    handlers.push_back(networkArg(receiver, m.property, c, *element, 1));
                }
                std::string list;
                for (std::size_t i = 0; i < handlers.size(); ++i) { if (i) list += ", "; list += handlers[i]; }
                return "(" + expr(*m.object) + ")." + methodName + "(" +
                       expr(*first) + ", ::cp3::runtime::network::detail::HandlerList{" + list + "})";
            }
            needsNetwork_ = true;
            const std::string path = pathText(*first);
            const std::string handler = networkArg(receiver, m.property, c, *c.arguments[1], 1);
            const std::string methodLit = methodName == "del" ? "\"DELETE\""
                : methodName == "all" || methodName == "any" ? "\"*\""
                : "\"" + [&] { std::string u = methodName; for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); return u; }() + "\"";
            return "::cp3::runtime::network::addRegexRoute((" + expr(*m.object) + "), " + methodLit + ", " + path + ", ::cp3::runtime::network::regex_routing::RoutingOptions{}, " + handler + ")";
        }
        if (c.arguments.size() > 2) {
            // Routing agent: variadic handler tail on HttpServer verbs.
            // Falls through to the generic path for Router/RouteChain below;
            // for HttpServer the HandlerList overload is reached via the
            // array-form check in the routing block.
        }
    }
    // Routing agent: route()/param()/HandlerList verbs (inlined from
    // codegen_routing.cpp emitRoutingMemberCall).
    {
        const bool isRouter = receiver.name == kRouter;
        const bool isApp = receiver.name == kwebserverApp;
        const bool isChain = receiver.name == kRouteChain || receiver.name == kRoute;
        if (isRouter || isApp || isChain || receiver.name == kHttpServer) {
            if (m.property == "route" && !isChain) {
                if (c.arguments.empty() || !c.arguments.front()) {
                    reportCodegenError(c, diagcode::kArityMismatch, "route(path) takes a path argument");
                    return "0.0";
                }
                return "(" + expr(*m.object) + ").route(" + expr(*c.arguments.front()) + ")";
            }
            if (m.property == "param" && !isChain) {
                if (c.arguments.size() < 2 || !c.arguments[0] || !c.arguments[1]) {
                    reportCodegenError(c, diagcode::kArityMismatch, "param(name, callback) takes two arguments");
                    return "0.0";
                }
                return "(" + expr(*m.object) + ").param(" + expr(*c.arguments[0]) + ", " +
                       networkArg(receiver, m.property, c, *c.arguments[1], 1) + ")";
            }
            const bool isVerb = m.property == "get" || m.property == "post" ||
                                m.property == "put" || m.property == "delete" ||
                                m.property == "patch" || m.property == "head" ||
                                m.property == "options" || m.property == "all";
            if (isVerb && isChain) {
                // Single handler: generic path below (RouteChain& / Route&).
                bool arrayTail = false;
                for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                    if (c.arguments[i] && c.arguments[i]->kind == NodeKind::ArrayLiteral) arrayTail = true;
                }
                if (arrayTail || c.arguments.size() > 1) {
                    std::vector<std::string> handlers;
                    for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                        const Expr* arg = c.arguments[i].get();
                        if (!arg) continue;
                        if (arg->kind == NodeKind::ArrayLiteral) {
                            const auto& literal = static_cast<const ArrayLiteral&>(*arg);
                            for (const auto& element : literal.elements) {
                                if (!element) continue;
                                handlers.push_back(networkArg(receiver, m.property, c, *element, i));
                            }
                            continue;
                        }
                        handlers.push_back(networkArg(receiver, m.property, c, *arg, i));
                    }
                    std::string list;
                    for (std::size_t i = 0; i < handlers.size(); ++i) { if (i) list += ", "; list += handlers[i]; }
                    return "(" + expr(*m.object) + ")." + networkMemberName(receiver, m.property) +
                           "(::cp3::runtime::network::detail::HandlerList{" + list + "})";
                }
            }
            if (isVerb && (isRouter || isApp) && c.arguments.size() == 2 &&
                c.arguments[1] && c.arguments[1]->kind == NodeKind::ArrayLiteral) {
                std::vector<std::string> handlers;
                const auto& literal = static_cast<const ArrayLiteral&>(*c.arguments[1]);
                for (const auto& element : literal.elements) {
                    if (!element) continue;
                    handlers.push_back(networkArg(receiver, m.property, c, *element, 1));
                }
                std::string list;
                for (std::size_t i = 0; i < handlers.size(); ++i) { if (i) list += ", "; list += handlers[i]; }
                return "(" + expr(*m.object) + ")." + networkMemberName(receiver, m.property) +
                       "(" + expr(*c.arguments[0]) + ", ::cp3::runtime::network::detail::HandlerList{" + list + "})";
            }
            if (isVerb && receiver.name == kHttpServer && c.arguments.size() > 2) {
                // Variadic tail: collect handlers[1..] into a HandlerList.
                std::vector<std::string> handlers;
                for (std::size_t i = 1; i < c.arguments.size(); ++i) {
                    if (!c.arguments[i]) continue;
                    handlers.push_back(networkArg(receiver, m.property, c, *c.arguments[i], i));
                }
                std::string list;
                for (std::size_t i = 0; i < handlers.size(); ++i) { if (i) list += ", "; list += handlers[i]; }
                return "(" + expr(*m.object) + ")." + networkMemberName(receiver, m.property) +
                       "(" + expr(*c.arguments[0]) + ", ::cp3::runtime::network::detail::HandlerList{" + list + "})";
            }
        }
    }
    // Response agent (§5b, inlined emitwebserverResponseCall).
    if (receiver.name == kHttpResponse) {
        const std::string recv = "(" + expr(*m.object) + ")";
        if ((m.property == "set" || m.property == "links") && c.arguments.size() == 1 &&
            c.arguments.front() && c.arguments.front()->kind == NodeKind::ObjectLiteral) {
            const auto& lit = static_cast<const ObjectLiteral&>(*c.arguments.front());
            std::string pairs;
            for (const auto& p : lit.properties) {
                if (!p || !p->value || p->isMethod) continue;
                std::string key;
                if (p->key && p->key->kind == NodeKind::Identifier && !p->computed) key = static_cast<Identifier&>(*p->key).name;
                else if (p->key && p->key->kind == NodeKind::StringLiteral && !p->computed) key = static_cast<StringLiteral&>(*p->key).value;
                else if (p->key) key = expr(*p->key);
                else continue;
                if (!pairs.empty()) pairs += ", ";
                pairs += "{\"" + key + "\", " + expr(*p->value) + "}";
            }
            return recv + "." + m.property +
                   "(std::vector<std::pair<std::string, std::string>>{" + pairs + "})";
        }
        if (m.property == "set" && c.arguments.size() == 1 && c.arguments.front()) {
            const Type argType = effectiveType(*c.arguments.front());
            if (argType.name == "Map" || argType.kind == TypeKind::Class) {
                needsCollections_ = true;
                return recv + ".set(" + expr(*c.arguments.front()) + ")";
            }
        }
        if (m.property == "format" && c.arguments.size() == 1 && c.arguments.front() &&
            c.arguments.front()->kind == NodeKind::ObjectLiteral) {
            const auto& lit = static_cast<const ObjectLiteral&>(*c.arguments.front());
            std::string entries;
            for (const auto& p : lit.properties) {
                if (!p || !p->value || p->isMethod) continue;
                std::string key;
                if (p->key && p->key->kind == NodeKind::Identifier && !p->computed) key = static_cast<Identifier&>(*p->key).name;
                else if (p->key && p->key->kind == NodeKind::StringLiteral && !p->computed) key = static_cast<StringLiteral&>(*p->key).value;
                else continue;
                if (!entries.empty()) entries += ", ";
                entries += "{\"" + key + "\", " + expr(*p->value) + "}";
            }
            return recv + ".format(std::vector<std::pair<std::string, std::function<void()>>>{" + entries + "})";
        }
        if (m.property == "render" && c.arguments.size() >= 1 && c.arguments.front()) {
            if (c.arguments.size() == 1) {
                return recv + ".render(" + expr(*c.arguments.front()) + ")";
            }
            if (c.arguments.size() >= 2 && c.arguments[1]) {
                const std::string locals = networkJsonValueText(*c.arguments[1]);
                if (locals.empty()) {
                    reportTypeError(*c.arguments[1], "type mismatch: res.render(view, locals) needs a serialisable locals object");
                    return "0.0";
                }
                needsJson_ = true;
                return recv + ".render(" + expr(*c.arguments.front()) + ", " + locals + ")";
            }
        }
        if (m.property == "vary" && c.arguments.size() == 1 && c.arguments.front() &&
            c.arguments.front()->kind == NodeKind::ArrayLiteral) {
            const auto& arr = static_cast<const ArrayLiteral&>(*c.arguments.front());
            std::string items;
            for (const auto& e : arr.elements) {
                if (!e) continue;
                if (!items.empty()) items += ", ";
                items += expr(*e);
            }
            return recv + ".vary(std::vector<std::string>{" + items + "})";
        }
    }
    if ((m.property == "cookie" || m.property == "clearCookie") &&
        effectiveType(*m.object).name == kHttpResponse) {
        // Cookie agent (codegen_cookie.inc A, verbatim logic).
        if (c.arguments.size() < (m.property == "cookie" ? 2u : 1u) ||
            !c.arguments[0]) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: res." + m.property +
                                   "() expects a name (and a value for cookie)");
            return "0.0";
        }
        std::string opts = "([&]{ ::cp3::runtime::network::webserver::CookieOptions o;";
        if (c.arguments.size() >= (m.property == "cookie" ? 3u : 2u) &&
            c.arguments[m.property == "cookie" ? 2 : 1]) {
            const Expr& optArg = *c.arguments[m.property == "cookie" ? 2 : 1];
            if (optArg.kind != NodeKind::ObjectLiteral) {
                reportCodegenError(c, diagcode::kTypeMismatch,
                                   "res." + m.property +
                                       "() options must be an object literal");
                return "0.0";
            }
            for (const auto& propertyPtr :
                 static_cast<const ObjectLiteral&>(optArg).properties) {
                const ObjectProperty* p = propertyPtr.get();
                if (!p || !p->value || p->isMethod || !p->key ||
                    p->key->kind != NodeKind::Identifier) continue;
                const std::string field = static_cast<Identifier&>(*p->key).name;
                const Expr& v = *p->value;
                if (field == "maxAge") {
                    opts += " o.hasMaxAge=true; o.maxAge=(" + expr(v) + ")/1000.0;";
                } else if (field == "expires") {
                    if (v.kind == NodeKind::NumberLiteral)
                        opts += " o.expires=::cp3::runtime::network::webserver::httpDateFromEpochMs(" + expr(v) + ");";
                    else
                        opts += " o.expires=::cp3::runtime::toString(" + expr(v) + ");";
                } else if (field == "path") {
                    opts += " o.path=::cp3::runtime::toString(" + expr(v) + ");";
                } else if (field == "domain") {
                    opts += " o.domain=::cp3::runtime::toString(" + expr(v) + ");";
                } else if (field == "secure") {
                    opts += " o.secure=(" + expr(v) + ")!=0;";
                } else if (field == "httpOnly") {
                    opts += " o.httpOnly=(" + expr(v) + ")!=0;";
                } else if (field == "signed") {
                    opts += " o.signed_= (" + expr(v) + ")!=0;";
                } else if (field == "sameSite") {
                    if (v.kind == NodeKind::StringLiteral) {
                        const std::string s = static_cast<StringLiteral&>(v).value;
                        if (s == "strict") opts += " o.sameSite=::cp3::runtime::network::webserver::SameSite::kStrict;";
                        else if (s == "lax") opts += " o.sameSite=::cp3::runtime::network::webserver::SameSite::kLax;";
                        else if (s == "none") opts += " o.sameSite=::cp3::runtime::network::webserver::SameSite::kNone;";
                        else opts += " o.sameSite=::cp3::runtime::network::webserver::SameSite::kUnset;";
                    } else {
                        reportCodegenError(c, diagcode::kTypeMismatch,
                                           "res.cookie() 'sameSite' must be a string literal: \"strict\"|\"lax\"|\"none\"");
                        return "0.0";
                    }
                }
            }
        }
        opts += " return o; }())";
        if (m.property == "cookie") {
            return "::cp3::runtime::network::webserver::setCookie((" + expr(*m.object) +
                   "), " + expr(*c.arguments[0]) + ", " + expr(*c.arguments[1]) +
                   ", " + opts + ")";
        }
        return "::cp3::runtime::network::webserver::clearCookie((" + expr(*m.object) +
               "), " + expr(*c.arguments[0]) + ", " + opts + ")";
    }
    if (m.property == "set" && effectiveType(*m.object).name == kHttpServer) {
        // Cookie agent: app.set("secret", s) (routing settings handled above).
        if (c.arguments.size() == 2 && c.arguments[0] && c.arguments[1] &&
            c.arguments[0]->kind == NodeKind::StringLiteral &&
            static_cast<StringLiteral&>(*c.arguments[0]).value == "secret") {
            return "(::cp3::runtime::network::webserver::setCookieSecret(" +
                   expr(*c.arguments[1]) + "), (" + expr(*m.object) + "))";
        }
        return "(" + expr(*m.object) + ")"; // other keys: accepted, no-op (yet)
    }
    const std::string args = networkArgs(receiver, m.property, c);
    // Explicit generics (`res.json<T>()`): a T with no C++ spelling (a
    // compound type, a type the runtime's json<T> cannot carry, a member that
    // takes no type argument at all) is a type error at the use site, not a
    // missing feature.
    std::string typeArgs;
    if (!networkTypeArgsText(c.typeArgs, typeArgs)) {
        if (m.property == "json") {
            reportTypeError(c,
                            "type mismatch: json<T>() supports number, string, "
                            "boolean or a record interface");
        } else {
            reportTypeError(c, "type mismatch: " + m.property +
                                   "() does not take a type argument");
        }
        return "0.0";
    }
    return "(" + expr(*m.object) + ")." + networkMemberName(receiver, m.property) +
           typeArgs + "(" + args + ")";
}

/// `res.json<T>()` / `req.json<T>()` for an interface T that the json family
/// registered as a record shape (collectJsonShapes): the generated builder
/// extracts the fields from the buffered body, and the runtime's builder
/// overload (cp3/std/network.hpp) wraps it in the promise. "" when T is not a
/// registered shape, so the scalar path still runs.
std::string Generator::networkJsonRecord(const MemberExpr& m, const CallExpr& c) {
    if (c.typeArgs.size() != 1 || !c.typeArgs.front()) return std::string();
    const TypeNode& tn = *c.typeArgs.front();
    if (tn.kind != NodeKind::NamedType) return std::string();
    const std::string& name = static_cast<const NamedType&>(tn).name;
    const auto structIt = jsonShapeStructs_.find(name);
    if (structIt == jsonShapeStructs_.end()) return std::string();
    const auto builderIt = jsonShapeBuilders_.find(name);
    if (builderIt == jsonShapeBuilders_.end()) return std::string();
    // The response's `json` is a writer (`res.json<T>(data, status?)`, docs
    // 04 §network); the request's and the fetch response's `json` is a reader
    // (`await res.json<T>()`). The receiver decides which one it is.
    if (effectiveType(*m.object).name == kHttpResponse) {
        return networkJsonWriter(m, c, name);
    }
    if (!c.arguments.empty()) {
        // The reader takes no arguments: the body is already buffered (D73).
        // Sema reports the arity error for the fetch/request receivers; this
        // is the defensive path.
        reportCodegenError(c, diagcode::kArityMismatch,
                           "json<T>() takes no arguments");
        return "0.0";
    }
    // Usage-based reification: materialize the struct on first use, exactly
    // like jsonRecordParse (src/std/json/codegen.cpp).
    needsJson_ = true;
    if (jsonShapeUsed_.insert(name).second) {
        jsonShapeDecls_ += structIt->second + "\n";
    }
    // A registered record is a reference value (spec 10), like the
    // `JSON.parse<T>` path: the builder produces the field struct and the
    // reference model wraps it before it reaches an interface binding.
    return "(" + expr(*m.object) + ").json<::cp3::runtime::Ref<" + mapIdent(name) +
           ">>([](const ::cp3::runtime::json::Fields& cp3_f) { auto cp3_value = [&]() { " +
           builderIt->second + " }(); return ::cp3::runtime::makeRef<" + mapIdent(name) +
           ">(cp3_value); })";
}

/// `res.json<T>(data, status?)`: the response's typed JSON writer (docs 04
/// §network). The payload is either a literal (the json dynamic value tree
/// serializes it) or a record value of the interface T (the record's fields
/// become the members of the json object); the response's
/// `json(const Value&)` overload sends it.
std::string Generator::networkJsonWriter(const MemberExpr& m, const CallExpr& c,
                                         const std::string& name) {
    if (c.arguments.empty() || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "res.json<T>(data) needs a payload");
        return "0.0";
    }
    const Expr& payload = *c.arguments.front();
    std::string value;
    if (payload.kind == NodeKind::ArrayLiteral ||
        payload.kind == NodeKind::ObjectLiteral) {
        // A literal payload: the json value tree carries it (the explicit T
        // names the shape the literal has).
        value = networkJsonValueText(payload);
        if (value.empty()) {
            reportTypeError(payload,
                            "type mismatch: res.json<T>(data) cannot serialize "
                            "this payload");
            return "0.0";
        }
    } else {
        // A record value of T: the shape label of its type names the
        // interface (the json family's label -> interface table), and the
        // record's fields become the json object's members.
        const auto fieldsIt = jsonShapeFields_.find(name);
        const Type payloadType = effectiveType(payload);
        const auto labelIt = jsonShapeByLabel_.find(payloadType.name);
        if (fieldsIt == jsonShapeFields_.end() || labelIt == jsonShapeByLabel_.end() ||
            labelIt->second != name) {
            reportTypeError(payload,
                            "type mismatch: res.json<T>(data) needs a value of "
                            "the interface " + name);
            return "0.0";
        }
        const std::string record = expr(payload);
        std::string members;
        for (const std::string& field : fieldsIt->second) {
            const std::string read = networkFieldRead(record, payloadType, field);
            if (read.empty()) {
                reportTypeError(payload,
                                "type mismatch: res.json<T>(data) cannot read the "
                                "field '" + field + "' of the interface " + name);
                return "0.0";
            }
            if (!members.empty()) members += ", ";
            // The record's field carries the value; the json value tree's
            // overloads pick the field's own type.
            members += "{\"" + field + "\", ::cp3::runtime::json::value(" + read +
                       ")}";
        }
        value = "::cp3::runtime::json::objectValue({" + members + "})";
    }
    needsJson_ = true;
    std::string text = "(" + expr(*m.object) + ").json(" + value;
    if (c.arguments.size() >= 2 && c.arguments[1]) {
        text += ", " + expr(*c.arguments[1]);
    }
    return text + ")";
}

/// `res.json(payload, status?)` for an array/object literal payload: the json
/// dynamic value tree serializes it and the runtime's `json(const Value&)`
/// overload answers it (docs 04 §network's `res.json({ ... })` example).
/// "" for anything else, so the scalar overload path still runs.
std::string Generator::networkResponseJson(const CallExpr& c, const MemberExpr& m) {
    if (effectiveType(*m.object).name != kHttpResponse) return std::string();
    if (c.arguments.empty() || !c.arguments.front()) return std::string();
    const Expr& payload = *c.arguments.front();
    if (payload.kind != NodeKind::ArrayLiteral && payload.kind != NodeKind::ObjectLiteral) {
        return std::string();
    }
    const std::string value = networkJsonValueText(payload);
    if (value.empty()) {
        // A payload form the json value tree cannot carry (a method property,
        // a computed key that is not a string, a nested spread). It is a type
        // error at the use site, not a missing feature.
        reportTypeError(payload,
                        "type mismatch: res.json(payload) cannot serialize this "
                        "payload (a computed key must be a string and a spread "
                        "element cannot nest)");
        return "0.0";
    }
    needsJson_ = true;
    std::string text = "(" + expr(*m.object) + ").json(" + value;
    if (c.arguments.size() >= 2 && c.arguments[1]) {
        text += ", " + expr(*c.arguments[1]);
    }
    return text + ")";
}

/// The C++ spelling of one field read of a record value (`u.id`): the
/// interface wrapper's accessor, the value shape's member, or "" when the
/// value is not a record (or the field has no C++ spelling).
std::string Generator::networkFieldRead(const std::string& recv, const Type& type,
                                        const std::string& field) {
    const detail::ObjectModel* model = detail::objectModelOf(type);
    if (model == nullptr) return std::string();
    if (!ModelRegistry::get().objectUsable(model->label)) return std::string();
    const detail::ObjectFieldModel* f = detail::findObjectField(*model, field);
    if (f == nullptr || f->isFunction) return std::string();
    if (model->isInterface) {
        return "(" + recv + ")." + mapIdent(f->name) + "()";
    }
    return "(" + recv + ")->" + mapIdent(f->name);
}

/// The json value tree text of a response payload literal (docs 04
/// §network). The json family's jsonValueText carries the plain literal
/// forms; a payload also accepts a computed key (`{ [k]: 1 }`, whose key is a
/// runtime webserverion) and a spread element (`{ ...base, b: 2 }`, whose source
/// fields become members). "" when the payload holds a form the tree cannot
/// carry (a method property, a computed key that is not a string, a nested
/// spread).
std::string Generator::networkJsonValueText(const Expr& e) {
    if (e.kind == NodeKind::ArrayLiteral) {
        const auto& literal = static_cast<const ArrayLiteral&>(e);
        std::vector<std::string> items;
        for (const auto& elementPtr : literal.elements) {
            const Expr* element = elementPtr.get();
            if (!element) return std::string();
            if (element->kind == NodeKind::SpreadElement) return std::string();
            const std::string text = networkJsonValueText(*element);
            if (text.empty()) return std::string();
            items.push_back(std::move(text));
        }
        return "::cp3::runtime::json::arrayValue({" + joinStrings(items, ", ") +
               "})";
    }
    if (e.kind == NodeKind::ObjectLiteral) {
        const auto& literal = static_cast<const ObjectLiteral&>(e);
        // The members in source order: the key's C++ text (a literal name or
        // a computed webserverion), the value's text, and the key the dedupe
        // compares (a later member with the same key wins, the JS object
        // semantics, so an earlier one is dropped).
        struct Member {
            std::string key;
            std::string keyText;
            std::string value;
        };
        std::vector<Member> members;
        for (const auto& propertyPtr : literal.properties) {
            const ObjectProperty* property = propertyPtr.get();
            if (!property) return std::string();
            if (property->isMethod) return std::string();
            if (!property->value) return std::string();
            if (property->value->kind == NodeKind::SpreadElement) {
                // A spread element: the source's fields become members (the
                // source is a record value, so its fields are read through
                // the model's own spelling).
                const auto& spread =
                    static_cast<const SpreadElement&>(*property->value);
                if (!spread.argument) return std::string();
                const Type sourceType = effectiveType(*spread.argument);
                const detail::ObjectModel* model =
                    detail::objectModelOf(sourceType);
                if (model == nullptr) {
                    return std::string();
                }
                const std::string source = expr(*spread.argument);
                for (const detail::ObjectFieldModel& field : model->fields) {
                    if (field.isFunction) return std::string();
                    const CppInfo ci = classify(field.type);
                    if (!ci.representable ||
                        (ci.shape != Shape::Number && ci.shape != Shape::String &&
                         ci.shape != Shape::Bool && ci.shape != Shape::Array)) {
                        return std::string();
                    }
                    const std::string read =
                        networkFieldRead(source, sourceType, field.name);
                    if (read.empty()) return std::string();
                    members.push_back(Member{field.name, "\"" + field.name + "\"",
                                             "::cp3::runtime::json::value(" +
                                                 read + ")"});
                }
                continue;
            }
            if (property->computed) {
                // A computed key: the key is a runtime webserverion and must be
                // a string.
                if (!property->key) return std::string();
                const CppInfo ci = info(*property->key);
                if (!ci.representable || ci.shape != Shape::String) {
                    return std::string();
                }
                const std::string text = networkJsonValueText(*property->value);
                if (text.empty()) return std::string();
                const std::string keyText = expr(*property->key);
                members.push_back(Member{keyText, keyText, text});
                continue;
            }
            std::string key;
            if (property->key) {
                if (property->key->kind == NodeKind::Identifier) {
                    key = static_cast<const Identifier&>(*property->key).name;
                } else if (property->key->kind == NodeKind::StringLiteral) {
                    key = static_cast<const StringLiteral&>(*property->key).value;
                }
            }
            if (key.empty()) return std::string();
            const std::string text = networkJsonValueText(*property->value);
            if (text.empty()) return std::string();
            members.push_back(Member{key, "\"" + key + "\"", text});
        }
        std::vector<std::string> pairs;
        for (std::size_t i = 0; i < members.size(); ++i) {
            bool shadowed = false;
            for (std::size_t j = i + 1; j < members.size(); ++j) {
                if (members[j].key == members[i].key) shadowed = true;
            }
            if (shadowed) continue;
            pairs.push_back("{" + members[i].keyText + ", " + members[i].value +
                            "}");
        }
        return "::cp3::runtime::json::objectValue({" + joinStrings(pairs, ", ") +
               "})";
    }
    // A scalar leaf (or an array value): the json family's own builder.
    return jsonValueText(e);
}

std::string Generator::emitNetworkMemberRead(const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    const std::string& name = effectiveType(*m.object).name;
    // Cookie agent: req.signedCookies is computed, not a stored field.
    if (name == kHttpRequest && m.property == "signedCookies") {
        needsNetwork_ = true;
        return "::cp3::runtime::network::webserver::signedCookiesFor(" +
               expr(*m.object) + ")";
    }
    // Router agent [C4a]: baseUrl/originalUrl are stored fields on
    // HttpRequest (maintained by Router::mountMiddleware in network.hpp).
    if (name == kHttpRequest && (m.property == "baseUrl" || m.property == "originalUrl")) {
        needsNetwork_ = true;
        return "(" + expr(*m.object) + ")." + m.property;
    }
    // Response agent: res.headersSent -> headersSent() method.
    if (name == kHttpResponse && m.property == "headersSent") {
        needsNetwork_ = true;
        return "(" + expr(*m.object) + ").headersSent()";
    }
    bool known = false;
    if (name == kFetchResponse) {
        known = m.property == "ok" || m.property == "status" ||
                m.property == "statusText" || m.property == "headers";
    } else if (name == kHttpRequest) {
        known = m.property == "method" || m.property == "path" ||
                m.property == "params" || m.property == "query" ||
                m.property == "headers" || m.property == "cookies" ||
                m.property == "ip";
    } else if (name == kRequest) {
        // Ledger L (appended).
        known = m.property == "url" || m.property == "method" ||
                m.property == "headers" || m.property == "signal";
    } else if (name == kResponse) {
        known = m.property == "ok" || m.property == "status" ||
                m.property == "statusText" || m.property == "headers" ||
                m.property == "url";
    } else if (name == kAbortSignal) {
        known = m.property == "aborted";
    }
    if (known) {
        needsNetwork_ = true;
        const std::string read =
            "(" + expr(*m.object) + ")." + m.property;
        // The runtime holds the status as the exact semantic width (`u16`).
        // A TypeScript project types the read as `number`, so widen it there;
        // a Rust/C++ project already sees the native value.
        if (name == kFetchResponse && m.property == "status" &&
            effectiveType(m).kind == TypeKind::Number) {
            return "static_cast<double>(" + read + ")";
        }
        if (name == kResponse && m.property == "status" &&
            effectiveType(m).kind == TypeKind::Number) {
            return "static_cast<double>(" + read + ")";
        }
        return read;
    }
    // Body-parse agent: `req.body` is the parsed body value tree
    // (webserver::body_parse::parsedBody), replacing the old
    // SEAM(readstream/fs) reportUnsupported arm.
    if (name == kHttpRequest && m.property == "body") {
        needsNetwork_ = true;
        return "::cp3::runtime::network::webserver::body_parse::parsedBody(" +
               expr(*m.object) + ")";
    }
    return std::string();
}

}  // namespace cp3
