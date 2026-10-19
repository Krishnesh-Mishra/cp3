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
#include "detail/generator.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kFetchResponse = "$network:FetchResponse";
constexpr const char* kHttpRequest = "$network:HttpRequest";
constexpr const char* kHttpResponse = "$network:HttpResponse";
constexpr const char* kHttpServer = "$network:HttpServer";
constexpr const char* kWebSocket = "$network:WebSocket";
constexpr const char* kWebSocketServer = "$network:WebSocketServer";

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
    if (cpp == "std::string") return Type::string();
    if (cpp == "double") return Type::number();
    return Type::unknown();
}

/// The C++ spelling of the explicit type arguments on a network member call
/// (`res.json<T>()`, docs 04 §network). `json<T>` is the only generic member;
/// a primitive T spells directly, and a named/interface T is routed through
/// the json record tables before this function runs (networkJsonRecord).
/// Anything else (a compound type, a non-registered named type) keeps the
/// CP3002 skip.
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
    return property;
}

}  // namespace

/// The documented Map-shaped field read behind an expression (`res.headers`,
/// `req.params`, `req.query`, `req.headers`), as `Map<string, string>`
/// (docs 04 §network), or nullopt. The `get(k)` result of such a field is
/// `string | undefined` (std/collections Map), so a `??`/`=== undefined` on
/// it classifies as the optional shape even when the receiver is a handler
/// parameter that sema only sees as ambient.
std::optional<Type> Generator::networkMapFieldType(const Expr& e) const {
    // A recorded hint wins; it is how the Map type reaches `effectiveType`
    // for handler parameters that sema only sees as ambient.
    if (const auto it = exprTypeHints_.find(e.nodeId); it != exprTypeHints_.end()) {
        return it->second;
    }
    if (e.kind == NodeKind::CallExpr) {
        const auto& c = static_cast<const CallExpr&>(e);
        if (!c.callee || c.callee->kind != NodeKind::MemberExpr) return std::nullopt;
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        if (m.property == "get" && m.object && networkMapFieldType(*m.object)) {
            return Type::unionOf({Type::string(), Type::undefined()});
        }
        return std::nullopt;
    }
    if (e.kind != NodeKind::MemberExpr) return std::nullopt;
    const auto& m = static_cast<const MemberExpr&>(e);
    if (!m.object) return std::nullopt;
    const std::string& recv = effectiveType(*m.object).name;
    bool field = false;
    if (recv == kFetchResponse) {
        field = m.property == "headers";
    } else if (recv == kHttpRequest) {
        field = m.property == "params" || m.property == "query" || m.property == "headers";
    }
    if (!field) return std::nullopt;
    return stringMapType();
}

/// A Map-shaped field's member call (`req.headers.get(k)`,
/// `res.headers.has(k)`): registers the field read as a Map in the codegen
/// type hints and re-uses the collections lowering unchanged. The hint is
/// needed because a handler parameter is ambient in sema, so the open marker
/// never carries the Map type to `effectiveType`.
std::string Generator::emitNetworkMapFieldCall(const CallExpr& c, const MemberExpr& m) {
    if (!m.object) return std::string();
    const std::optional<Type> type = networkMapFieldType(*m.object);
    if (!type) return std::string();
    exprTypeHints_[m.object->nodeId] = *type;
    return emitCollectionsMemberCall(c, m);
}

/// The `.size` read of a Map-shaped field (`req.params.size`); same hint as
/// the call form above.
std::string Generator::emitNetworkMapFieldMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const std::optional<Type> type = networkMapFieldType(*m.object);
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
        } else if (property == "use") {
            context = {kReqType, kResType, kNextType};
        } else {
            context = {kReqType, kResType};
        }
    } else if (receiver.name == kWebSocket || receiver.name == kWebSocketServer) {
        // The event name is the first literal argument.
        std::string event = "open";
        if (index > 0 && !c.arguments.empty() && c.arguments.front() &&
            c.arguments.front()->kind == NodeKind::StringLiteral) {
            event = static_cast<const StringLiteral&>(*c.arguments.front()).value;
        }
        context = eventParams(event);
    }
    const auto& arrow = static_cast<const ArrowFunction&>(arg);
    // Type the lambda's parameter names for the body emission only: sema keeps
    // callback parameters permissive, so without this a member call inside the
    // handler (`res.text(...)`, `ws.send(...)`) would not reach the network
    // tables and would report CP3002. The temporary scope keeps the names out of
    // the enclosing scope.
    pushScope();
    for (std::size_t i = 0; i < arrow.params.size() && i < context.size(); ++i) {
        const Parameter* p = arrow.params[i].get();
        if (!p || !p->name || p->name->kind != NodeKind::Identifier) continue;
        const Type type = contextType(context[i]);
        if (type.isUnknown()) continue;
        declare(static_cast<const Identifier&>(*p->name).name, type);
    }
    std::string text = emitArrow(arrow, context);
    popScope();
    return text;
}

std::string Generator::networkArgs(const Type& receiver, const std::string& property,
                                   const CallExpr& c) {
    std::string args;
    for (std::size_t i = 0; i < c.arguments.size(); ++i) {
        const Expr* arg = c.arguments[i].get();
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            notImplemented(*arg, "spread argument");
            continue;
        }
        if (!args.empty()) args += ", ";
        args += networkArg(receiver, property, c, *arg, i);
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
    const std::string args = networkArgs(receiver, m.property, c);
    // Explicit generics (`res.json<T>()`); a T with no C++ spelling keeps the
    // CP3002 skip (see networkTypeArgsText).
    std::string typeArgs;
    if (!networkTypeArgsText(c.typeArgs, typeArgs)) {
        notImplemented(c, "std/network member call with a type argument that has "
                          "no C++ spelling yet (json<T> supports a primitive T or a "
                          "reified interface)");
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
    if (!c.arguments.empty()) {
        notImplemented(c, "std/network json<T>() takes no arguments");
        return "0.0";
    }
    const auto builderIt = jsonShapeBuilders_.find(name);
    if (builderIt == jsonShapeBuilders_.end()) return std::string();
    // Usage-based reification: materialize the struct on first use, exactly
    // like jsonRecordParse (src/std/json/codegen.cpp).
    needsJson_ = true;
    if (jsonShapeUsed_.insert(name).second) {
        jsonShapeDecls_ += structIt->second + "\n";
    }
    return "(" + expr(*m.object) + ").json<" + mapIdent(name) +
           ">([](const ::cp3::runtime::json::Fields& cp3_f) { " + builderIt->second +
           " })";
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
    const std::string value = jsonValueText(payload);
    if (value.empty()) {
        notImplemented(payload, "res.json(payload)");
        return "0.0";
    }
    needsJson_ = true;
    std::string text = "(" + expr(*m.object) + ").json(" + value;
    if (c.arguments.size() >= 2 && c.arguments[1]) {
        text += ", " + expr(*c.arguments[1]);
    }
    return text + ")";
}

std::string Generator::emitNetworkMemberRead(const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    const std::string& name = effectiveType(*m.object).name;
    bool known = false;
    if (name == kFetchResponse) {
        known = m.property == "ok" || m.property == "status" ||
                m.property == "statusText" || m.property == "headers";
    } else if (name == kHttpRequest) {
        known = m.property == "method" || m.property == "path" ||
                m.property == "params" || m.property == "query" ||
                m.property == "headers";
    }
    if (known) {
        needsNetwork_ = true;
        return "(" + expr(*m.object) + ")." + m.property;
    }
    // `req.body` is the one documented read whose runtime member still does not
    // exist: the body is buffered, and the documented `ReadStream` shape is the
    // fs family's (SEAM(readstream/fs)). Keep the CP3002 skip instead of
    // emitting a raw C++ read that would die in g++.
    if (name == kHttpRequest && m.property == "body") {
        notImplemented(m, "req.body (needs the fs ReadStream; SEAM(readstream/fs))");
        return "0.0";
    }
    return std::string();
}

}  // namespace cp3
