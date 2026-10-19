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
//   req.method/path/params/query/headers, res.headers/arrayBuffer)
//   becomes the plain C++ member call/read.
//   `app.delete(...)` maps to the runtime `del` member (`delete` is a C++
//   keyword), `res.json<T>()` carries its explicit T through (a registered
//   interface T through the json record tables), `res.json({ ... })` through
//   the json dynamic value tree, the Map-shaped fields (`res.headers`,
//   `req.params/query/headers`) delegate their member calls to the collections
//   lowering (exprTypeHints_), and a middleware body calling `next()` lowers to
//   the runtime continuation call.
//
// The member call/read surface (argument typing, the Map-shaped field reads,
// the json dispatch and the known read list) lives in the sibling
// codegen_methods.cpp; this unit owns receiver recognition plus the
// construction/entry lowering.
//
// SEAMS (blocked on an in-flight or unlanded family; each is marked where it
// fires): req.body -> the fs ReadStream (fs), async handlers (the async codegen
// still reports CP3002 for `async () =>`), and the fetch-headers object-literal
// form (the `_ambiguous/network_headers_object_vs_map` proposal: the Map wins).
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
// fetch / createServer
// =============================================================================

/// `{ method, body, timeout, headers }` ->
/// `FetchInit{method, body, hasBody, timeoutMs, headers}` (the positional
/// generated-code contract in cp3/std/network.hpp).
std::string Generator::fetchInitText(const Expr& init) {
    if (init.kind != NodeKind::ObjectLiteral) {
        notImplemented(init, "fetch options (an object literal)");
        return std::string(kRuntimeNs) + "FetchInit{}";
    }
    const auto& literal = static_cast<const ObjectLiteral&>(init);
    std::string method = "std::string(\"GET\")";
    std::string body = "std::string()";
    std::string timeout = "0.0";
    std::string headers;
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
            // RequestInit.headers is `Map<string, string>` (docs 04 §network):
            // the Map expression reaches FetchInit.headers and buildRequest
            // (runtime_fetch.cpp) writes one `Name: value` line per entry. An
            // object-literal form stays a CP3002 skip: that is precisely the
            // `_ambiguous/network_headers_object_vs_map` question, and the wave
            // report's proposal answers it with the Map (the docs example
            // aside), so the skipped shape is the one the ambiguity names.
            if (p->value->kind == NodeKind::ObjectLiteral) {
                notImplemented(*p->value,
                               "fetch headers as an object literal (the documented "
                               "shape is a Map<string, string>; headers ambiguity)");
                continue;
            }
            headers = expr(*p->value);
            needsCollections_ = true;
        }
    }
    std::string text = std::string(kRuntimeNs) + "FetchInit{" + method + ", " + body +
                       ", " + (hasBody ? "true" : "false") + ", " + timeout;
    if (!headers.empty()) text += ", " + headers;
    return text + "}";
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
