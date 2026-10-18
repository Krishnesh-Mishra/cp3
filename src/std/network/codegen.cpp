// =============================================================================
// src/std/network/codegen.cpp - `std/network` lowering.
//
// OWNERSHIP: the std/network family (src/std/network/**). The namespace dispatch
// row lives in emit/std/common.cpp (emitBuiltinCall / emitBuiltinMember); the
// named-import hooks live where bare calls and `new` are lowered
// (emit/call.cpp, emit/expr.cpp). Everything lowers onto cp3/std/network.hpp:
//
//   fetch(url[, init])       -> ::cp3::runtime::network::fetch(url[, FetchInit])
//   createServer()           -> ::cp3::runtime::network::HttpServer()
//   new WebSocket(url)       -> ::cp3::runtime::network::WebSocket(url)
//   new WebSocketServer(o)   -> ::cp3::runtime::network::WebSocketServer(port[, path])
//   every other documented member (app.get/post/use/listen, ws.on/send/close,
//   wss.on/close, res.status/text/json/send/write/end, res.status/ok/statusText,
//   req.method/path) becomes the plain C++ member call/read.
//   `app.delete(...)` maps to the runtime `del` member (`delete` is a C++
//   keyword), `res.json<T>()` carries its explicit T through, and a middleware
//   body calling `next()` lowers to the runtime continuation call.
//
// PENDING (blocked on the in-flight std families; each has a SEAM marker
// below): RequestInit.headers, FetchResponse.headers, req.params/query/headers
// (collections Map), req.body (fs ReadStream), arrayBuffer() -> Buffer
// (buffer), json<T> for a class/interface T (json reification + interface
// codegen). Documented reads with no runtime member keep a named CP3002 skip
// (emitNetworkMemberRead) instead of a raw C++ member read.
//
// Receivers are recognised by the `$network:` marker names sema publishes
// (src/std/network/sema.cpp). Handler arrows are emitted with declared parameter types
// (`contextParams`), because the sema side keeps callback parameters permissive.
// Each path sets needsNetwork_ (the include) and needsEventLoop_ (handlers run
// on the runtime loop, so `main` must drive it even without an `await`).
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

constexpr const char* kRuntimeNs = "::cp3::runtime::network::";
constexpr const char* kReqType = "::cp3::runtime::network::HttpRequest&";
constexpr const char* kResType = "::cp3::runtime::network::HttpResponse&";
constexpr const char* kNextType = "::cp3::runtime::network::Next";
constexpr const char* kThrownType = "::cp3::runtime::Thrown";

/// The marker type a middleware's `next` parameter carries in the temporary
/// handler scope below: the `$network:` prefix matches the sema markers, so a
/// body call to `next()` is recognised as the runtime continuation and lowers
/// to the plain `next()` member call (`Next::operator()`).
constexpr const char* kNextMarker = "$network:Next";

/// The literal property name of an object-literal entry ("" when computed).
std::string objectKey(const ObjectProperty& p) {
    if (p.computed || !p.key) return std::string();
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    return std::string();
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
/// (`res.json<T>()`, docs 04 §network). `json<T>` is the only generic member,
/// and today only a primitive T has a C++ spelling. A class/interface T needs
/// two things that do not exist yet: interface codegen (a top-level interface
/// is CP3002) and the usage-based reification the json wave emits for
/// `JSON.parse<T>`.
/// SEAM(json): when the json wave lands, route the named/compound arm through
/// its reification type spelling (the same one `JSON.parse<T>` emits) and
/// return true for it; the call site already carries `<T>` unchanged.
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

bool Generator::isNetworkObject(const Expr* e) const {
    if (e == nullptr) return false;
    const std::string& name = effectiveType(*e).name;
    if (name == kFetchResponse || name == kHttpRequest || name == kHttpResponse ||
        name == kHttpServer || name == kWebSocket || name == kWebSocketServer) {
        return true;
    }
    // `res.status(404).text(...)` / `res.header(...).send(...)`: the chainable
    // members hand back the same receiver, and sema types the chain permissively
    // (its `res` is an un-annotated handler parameter), so the enclosing member
    // call still belongs to the receiver's table.
    if (e->kind != NodeKind::CallExpr) return false;
    const auto& call = static_cast<const CallExpr&>(*e);
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr) return false;
    const auto& inner = static_cast<const MemberExpr&>(*call.callee);
    if (inner.property != "status" && inner.property != "header") return false;
    return isNetworkObject(inner.object.get());
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
// fetch / createServer
// =============================================================================

/// `{ method, body, timeout }` -> `FetchInit{method, body, hasBody, timeoutMs}`
/// (the positional generated-code contract in cp3/std/network.hpp).
std::string Generator::fetchInitText(const Expr& init) {
    if (init.kind != NodeKind::ObjectLiteral) {
        notImplemented(init, "fetch options (an object literal)");
        return std::string(kRuntimeNs) + "FetchInit{}";
    }
    const auto& literal = static_cast<const ObjectLiteral&>(init);
    std::string method = "std::string(\"GET\")";
    std::string body = "std::string()";
    std::string timeout = "0.0";
    bool hasBody = false;
    for (const auto& p : literal.properties) {
        if (!p || !p->value) continue;
        const std::string key = objectKey(*p);
        if (key == "method") {
            method = expr(*p->value);
        } else if (key == "body") {
            body = expr(*p->value);
            hasBody = true;
        } else if (key == "timeout") {
            timeout = expr(*p->value);
        } else if (key == "headers") {
            // SEAM(collections): RequestInit.headers is `Map<string, string>`
            // (docs 04 §network). Once std/collections lands the Map reads as
            // its pair list here and the pairs go into a FetchInit.headers
            // field, which buildRequest (runtime_fetch.cpp) turns into
            // `Name: value` lines. Until then this stays the CP3002 skip.
            notImplemented(*p, "fetch headers (needs the Map type)");
        }
    }
    return std::string(kRuntimeNs) + "FetchInit{" + method + ", " + body + ", " +
           (hasBody ? "true" : "false") + ", " + timeout + "}";
}

std::string Generator::emitNetworkFetch(const CallExpr& c, const ExprList& args) {
    needsNetwork_ = true;
    needsEventLoop_ = true;
    if (args.empty() || !args.front()) {
        notImplemented(c, "fetch(...)");
        return "0.0";
    }
    const std::string url = expr(*args.front());
    if (args.size() < 2 || !args[1]) {
        return std::string(kRuntimeNs) + "fetch(" + url + ")";
    }
    return std::string(kRuntimeNs) + "fetch(" + url + ", " + fetchInitText(*args[1]) + ")";
}

/// The named-import call sites: `fetch(...)` / `createServer()`. Returns "" when
/// the identifier is not part of the documented surface.
std::string Generator::emitNetworkIdentifier(const CallExpr& c, const std::string& name) {
    if (name == "fetch") return emitNetworkFetch(c, c.arguments);
    if (name == "createServer") {
        needsNetwork_ = true;
        needsEventLoop_ = true;
        return std::string(kRuntimeNs) + "HttpServer()";
    }
    return std::string();
}

/// The fallback for a bare call to a name that is not a function declared in
/// this unit: the `std/network` named imports are lowered first, everything else
/// keeps reporting CP3002 (an intrinsic outside this surface, console.* handled
/// earlier, must be treated as out of scope rather than reaching an undeclared
/// C++ name). The seam lives here so emitCall itself stays inside the
/// function-length budget.
std::string Generator::emitUnresolvedCall(const CallExpr& c, const std::string& name) {
    std::string net = emitNetworkIdentifier(c, name);
    if (!net.empty()) return net;
    // A middleware body calling `next()` (docs 04 §network): the parameter is
    // typed with the `$network:Next` marker in the temporary handler scope
    // (networkArg), so the call lowers to the runtime continuation.
    const std::optional<Type> binding = lookup(name);
    if (binding && binding->name == kNextMarker) {
        needsNetwork_ = true;
        if (!c.arguments.empty()) {
            notImplemented(c, "arguments to the std/network next() continuation");
            return "0.0";
        }
        return mapIdent(name) + "()";
    }
    notImplemented(c, "call to function '" + name + "' not declared in this unit");
    return "0.0";
}

// =============================================================================
// `new WebSocket(...)` / `new WebSocketServer(...)`
// =============================================================================

std::string Generator::emitNetworkNew(const NewExpr& n) {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::string();
    const std::string name = static_cast<const Identifier&>(*n.callee).name;
    if (name == "WebSocket") {
        needsNetwork_ = true;
        needsEventLoop_ = true;
        if (n.arguments.empty() || !n.arguments.front()) {
            notImplemented(n, "new WebSocket(...)");
            return "0.0";
        }
        return std::string(kRuntimeNs) + "WebSocket(" + expr(*n.arguments.front()) + ")";
    }
    if (name != "WebSocketServer") return std::string();
    needsNetwork_ = true;
    needsEventLoop_ = true;
    if (n.arguments.empty() || !n.arguments.front()) {
        notImplemented(n, "new WebSocketServer(...)");
        return "0.0";
    }
    const Expr* first = n.arguments.front().get();
    if (first->kind != NodeKind::ObjectLiteral) {
        return std::string(kRuntimeNs) + "WebSocketServer(" + expr(*first) + ")";
    }
    const auto& literal = static_cast<const ObjectLiteral&>(*first);
    std::string port = "0.0";
    std::string path;
    for (const auto& p : literal.properties) {
        if (!p || !p->value) continue;
        const std::string key = objectKey(*p);
        if (key == "port") {
            port = expr(*p->value);
        } else if (key == "path") {
            path = expr(*p->value);
        }
    }
    std::string text = std::string(kRuntimeNs) + "WebSocketServer(" + port;
    if (!path.empty()) text += ", " + path;
    return text + ")";
}

// =============================================================================
// Member calls / reads on network receivers
// =============================================================================

std::string Generator::emitNetworkMemberCall(const CallExpr& c, const MemberExpr& m) {
    needsNetwork_ = true;
    needsEventLoop_ = true;
    const Type receiver = effectiveType(*m.object);
    const std::string args = networkArgs(receiver, m.property, c);
    // Explicit generics (`res.json<T>()`); a T with no C++ spelling yet keeps
    // the CP3002 skip (see networkTypeArgsText for the json seam).
    std::string typeArgs;
    if (!networkTypeArgsText(c.typeArgs, typeArgs)) {
        notImplemented(c, "std/network member call with a type argument that has "
                          "no C++ spelling yet (json<T> supports a primitive T)");
        return "0.0";
    }
    return "(" + expr(*m.object) + ")." + networkMemberName(receiver, m.property) +
           typeArgs + "(" + args + ")";
}

std::string Generator::emitNetworkMemberRead(const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    const std::string& name = effectiveType(*m.object).name;
    bool known = false;
    if (name == kFetchResponse) {
        known = m.property == "ok" || m.property == "status" || m.property == "statusText";
    } else if (name == kHttpRequest) {
        known = m.property == "method" || m.property == "path";
    }
    if (known) {
        needsNetwork_ = true;
        return "(" + expr(*m.object) + ")." + m.property;
    }
    // Documented reads whose runtime members do not exist yet: keep the CP3002
    // skip (naming the seam) instead of emitting a raw C++ read that would die
    // inside g++ with "no member named ...".
    if (name == kFetchResponse && m.property == "headers") {
        notImplemented(m, "res.headers (needs the Map type; SEAM(collections))");
        return "0.0";
    }
    if (name == kHttpRequest &&
        (m.property == "params" || m.property == "query" || m.property == "headers" ||
         m.property == "body")) {
        notImplemented(m, "req." + m.property +
                              " (needs the Map/stream families; SEAM(collections) / "
                              "SEAM(readstream/fs))");
        return "0.0";
    }
    return std::string();
}

// =============================================================================
// The namespace corner: `network.fetch(...)` and friends
// =============================================================================

std::string Generator::emitNetworkCall(const CallExpr& c, const MemberExpr& m) {
    if (m.property == "fetch") return emitNetworkFetch(c, c.arguments);
    if (m.property == "createServer") return emitNetworkIdentifier(c, "createServer");
    if (m.property == "WebSocket" || m.property == "WebSocketServer") {
        // The class-shaped namespace member: `network.WebSocket(url)`.
        needsNetwork_ = true;
        needsEventLoop_ = true;
        if (c.arguments.empty() || !c.arguments.front()) return std::string();
        return std::string(kRuntimeNs) + m.property + "(" + expr(*c.arguments.front()) + ")";
    }
    return std::string();
}

}  // namespace cp3
