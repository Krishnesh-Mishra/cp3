// =============================================================================
// src/std/network/codegen.cpp - `std/network` lowering.
//
// OWNERSHIP: the std/network family (src/std/network/**). The namespace dispatch
// row lives in emit/std/common.cpp (emitBuiltinCall / emitBuiltinMember); the
// named-import hooks live where bare calls and `new` are lowered
// (emit/call.cpp, emit/expr.cpp). Everything lowers onto cp3/std/network.hpp:
//
//   fetch(url[, init])       -> ::cp3::runtime::network::fetch(url[, FetchInit])
//                              (`init` is an options object literal, an options
//                              object in a binding, or `undefined`/`null` for
//                              "no options")
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
// fires): req.body -> the fs ReadStream (fs) and a streaming request pipeline.
// Async HTTP handlers are lowered here as coroutine lambdas
// (codegen_methods.cpp) and awaited by the server dispatcher.
//
// The fetch-headers ambiguity is closed (D77): RequestInit.headers is the
// documented `Map<string, string>`, so an object literal in that position is a
// type error at the use site, not a skip.
//
// Receivers are recognised by the `$network:` marker names sema publishes
// (src/std/network/sema.cpp). Handler arrows are emitted with declared parameter types
// (`contextParams`), because the sema side keeps callback parameters permissive.
// Each path sets needsNetwork_ (the include) and needsEventLoop_ (handlers run
// on the runtime loop, so `main` must drive it even without an `await`).
// =============================================================================
#include "detail/generator/generator.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

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

/// Ledger L (appended): `new Headers/Request/Response/FormData/AbortSignal`
/// construction text, shared by emitNetworkNew and bare-call lowering.
std::string networkClassText(const std::string& name, const ExprList& args,
                             const std::function<std::string(const Expr&)>& emit,
                             const std::function<std::string(const Expr&)>& typeName) {
    const std::string ns = kRuntimeNs;
    if (name == "Headers") {
        if (args.empty() || !args.front()) return ns + "Headers()";
        const Expr& a = *args.front();
        if (a.kind == NodeKind::ObjectLiteral) {
            const auto& lit = static_cast<const ObjectLiteral&>(a);
            std::string pairs;
            for (const auto& p : lit.properties) {
                if (!p || !p->value) continue;
                if (!pairs.empty()) pairs += ", ";
                pairs += "{\"\"" + objectKey(*p) + "\", " + emit(*p->value) + "}";
            }
            return ns + "Headers(std::vector<std::pair<std::string, std::string>>{" + pairs + "})";
        }
        return ns + "Headers(" + emit(a) + ")";
    }
    if (name == "FormData" || name == "AbortSignal") return ns + name + "()";
    if (name == "Request") {
        if (args.empty() || !args.front()) return ns + "Request()";
        std::string text = ns + "Request(" + emit(*args.front());
        if (args.size() >= 2 && args[1] && args[1]->kind == NodeKind::ObjectLiteral) {
            const auto& lit = static_cast<const ObjectLiteral&>(*args[1]);
            std::string init = ns + "RequestInit{";
            std::string method, body, headers, signal, form;
            bool hasBody = false;
            bool headersIsObj = false;
            for (const auto& p : lit.properties) {
                if (!p || !p->value) continue;
                const std::string key = objectKey(*p);
                if (key == "method") method = emit(*p->value);
                else if (key == "body") {
                    if (typeName(*p->value) == kFormData) form = emit(*p->value);
                    else { body = emit(*p->value); hasBody = true; }
                } else if (key == "headers") {
                    if (typeName(*p->value) == kHeaders) { headers = emit(*p->value); headersIsObj = true; }
                    else headers = emit(*p->value);
                } else if (key == "signal") signal = "std::make_shared<" + ns + "AbortSignal>(" + emit(*p->value) + ")";
            }
            init += (method.empty() ? "std::string(\"GET\")" : method) + ", " + body + ", " +
                    (hasBody ? "true" : "false") + ", " +
                    ((headers.empty() || headersIsObj) ? ns + "StringMap()" : headers) + ", " +
                    (headersIsObj ? "std::make_shared<" + ns + "Headers>(" + headers + ")" : "nullptr") + ", ";
            if (!form.empty()) init += "std::make_shared<" + ns + "FormData>(" + form + "), ";
            else init += "nullptr, ";
            if (!signal.empty()) init += signal;
            init += "}";
            text += ", " + init;
        } else if (args.size() >= 2 && args[1]) {
            text += ", " + ns + "RequestInit{}";
        }
        return text + ")";
    }
    if (name == "Response") {
        if (args.empty() || !args.front()) return ns + "Response()";
        std::string text = ns + "Response(" + emit(*args.front());
        if (args.size() >= 2 && args[1] && args[1]->kind == NodeKind::ObjectLiteral) {
            const auto& lit = static_cast<const ObjectLiteral&>(*args[1]);
            std::string status = "200.0", headers = ns + "Headers()";
            for (const auto& p : lit.properties) {
                if (!p || !p->value) continue;
                const std::string key = objectKey(*p);
                if (key == "status") status = emit(*p->value);
                else if (key == "headers") {
                    if (typeName(*p->value) == kHeaders) headers = emit(*p->value);
                    else headers = ns + "Headers(" + emit(*p->value) + ")";
                }
            }
            text += ", " + status + ", " + headers;
        }
        return text + ")";
    }
    return std::string();
}

}  // namespace

bool Generator::isNetworkObject(const Expr* e) const {
    if (e == nullptr) return false;
    const std::string& name = effectiveType(*e).name;
    if (name == kFetchResponse || name == kHttpRequest || name == kHttpResponse ||
        name == kHttpServer || name == kWebSocket || name == kWebSocketServer ||
        name == kHeaders || name == kRequest || name == kResponse ||
        name == kFormData || name == kAbortSignal) {
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

/// The RequestInit fields of an options object held in a binding: the
/// generated value struct carries the fields by name, so the FetchInit reads
/// them off the object. "" when a field has a type the runtime cannot carry.
std::string fetchInitFromFields(const std::string& recv,
                                const std::vector<ObjectField>& fields) {
    std::string method = "std::string(\"GET\")";
    std::string body = "std::string()";
    std::string timeout = "0.0";
    bool hasBody = false;
    for (const ObjectField& field : fields) {
        const std::string member = mapIdent(field.name);
        if (field.name == "method") {
            if (field.cpp != "std::string") return std::string();
            method = recv + "->" + member;
        } else if (field.name == "body") {
            if (field.cpp != "std::string") return std::string();
            body = recv + "->" + member;
            hasBody = true;
        } else if (field.name == "timeout") {
            if (field.cpp != "double") return std::string();
            timeout = recv + "->" + member;
        }
    }
    return std::string(kRuntimeNs) + "FetchInit{" + method + ", " + body + ", " +
           (hasBody ? "true" : "false") + ", " + timeout + "}";
}

/// `{ method, body, timeout, headers }` ->
/// `FetchInit{method, body, hasBody, timeoutMs, headers}` (the positional
/// generated-code contract in cp3/std/network.hpp). "" when the options are
/// `undefined` or `null` (the JS "no options" spelling: `fetch(url, undefined)`
/// is `fetch(url)`).
std::string Generator::fetchInitText(const Expr& init) {
    if (init.kind == NodeKind::UndefinedLiteral ||
        init.kind == NodeKind::NullLiteral) {
        return std::string();
    }
    if (init.kind != NodeKind::ObjectLiteral) {
        // An options object in a binding: the fields are read off the shape.
        const Type shape = effectiveType(init);
        std::string structName;
        std::vector<ObjectField> fields;
        if (shape.kind == TypeKind::Object &&
            objectShapeOf(shape, structName, fields)) {
            const std::string text =
                fetchInitFromFields("(" + expr(init) + ")", fields);
            if (!text.empty()) return text;
        }
        reportTypeError(init,
                        "type mismatch: the fetch options must be an object "
                        "literal (a RequestInit)");
        return std::string(kRuntimeNs) + "FetchInit{}";
    }
    const auto& literal = static_cast<const ObjectLiteral&>(init);
    std::string method = "std::string(\"GET\")";
    std::string body = "std::string()";
    std::string timeout = "0.0";
    std::string headers;
    std::string signal;  // Ledger L (appended)
    std::string formBody;
    bool hasBody = false;
    for (const auto& p : literal.properties) {
        if (!p || !p->value) continue;
        const std::string key = objectKey(*p);
        if (key == "method") {
            method = expr(*p->value);
        } else if (key == "body") {
            if (effectiveType(*p->value).name == kFormData) formBody = expr(*p->value);
            else body = expr(*p->value);
            hasBody = true;
        } else if (key == "signal") {
            signal = expr(*p->value);
        } else if (key == "timeout") {
            timeout = expr(*p->value);
        } else if (key == "headers") {
            // RequestInit.headers is `Map<string, string>` (docs 04 §network,
            // D77): the Map expression reaches FetchInit.headers and
            // buildRequest (runtime_fetch.cpp) writes one `Name: value` line
            // per entry. A plain object literal in that position is off the
            // documented surface, so it is a type error at the use site (the
            // lead's D77 ruling: the Map wins, the doc's object-literal
            // example was the doc side of the disagreement).
            if (p->value->kind == NodeKind::ObjectLiteral) {
                reportTypeError(*p->value,
                                "type mismatch: fetch options 'headers' is a "
                                "Map<string, string>, not an object literal");
                continue;
            }
            // Ledger L: a Headers object lowers through its toMap().
            if (effectiveType(*p->value).name == kHeaders) {
                headers = "(" + expr(*p->value) + ").toMap()";
            } else {
                headers = expr(*p->value);
            }
            needsCollections_ = true;
        }
    }
    std::string text = std::string(kRuntimeNs) + "FetchInit{" + method + ", " + body +
                       ", " + (hasBody ? "true" : "false") + ", " + timeout;
    if (!headers.empty()) text += ", " + headers;
    else if (!signal.empty()) text += ", ::cp3::runtime::network::StringMap()";
    if (!signal.empty()) text += ", std::make_shared<::cp3::runtime::network::AbortSignal>(" + signal + ")";
    text += "}";
    if (!formBody.empty()) text = std::string(kRuntimeNs) + "withFormData(" + text + ", " + formBody + ")";
    return text;
}

std::string Generator::emitNetworkFetch(const CallExpr& c, const ExprList& args) {
    needsNetwork_ = true;
    needsEventLoop_ = true;
    if (args.empty() || !args.front()) {
        // `fetch(url, options?)` (docs 04 §network): the URL is required.
        // Sema reports the arity error (src/std/network/sema.cpp); this is the
        // defensive path for a unit that reaches codegen without it.
        reportCodegenError(c, diagcode::kArityMismatch,
                           "fetch(url, options?) takes one or two arguments");
        return "0.0";
    }
    // Ledger L (appended): `fetch(request)`.
    if (effectiveType(*args.front()).name == kRequest) {
        return std::string(kRuntimeNs) + "fetch(" + expr(*args.front()) + ")";
    }
    const std::string url = expr(*args.front());
    if (args.size() < 2 || !args[1]) {
        return std::string(kRuntimeNs) + "fetch(" + url + ")";
    }
    // `fetch(url, undefined)` (and `null`) is `fetch(url)`: the options are
    // optional.
    const std::string init = fetchInitText(*args[1]);
    if (init.empty()) return std::string(kRuntimeNs) + "fetch(" + url + ")";
    return std::string(kRuntimeNs) + "fetch(" + url + ", " + init + ")";
}

/// The named-import call sites: `fetch(...)` / `createServer()`. Returns "" when
/// the identifier is not part of the documented surface.
std::string Generator::emitNetworkIdentifier(const CallExpr& c, const std::string& name) {
    if (name == "fetch") return emitNetworkFetch(c, c.arguments);
    if (name == "lookup") {
        needsNetwork_ = true;
        needsEventLoop_ = true;
        if (c.arguments.empty() || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "dns.lookup(hostname) takes exactly one argument");
            return "0.0";
        }
        return std::string(kRuntimeNs) + "dnsResolve(" +
               expr(*c.arguments.front()) + ")";
    }
    if (name == "createServer") {
        needsNetwork_ = true;
        needsEventLoop_ = true;
        // D79: `createServer({ threads: n })` lowers to the runtime's scaled
        // constructor. No argument (and an object without `threads`) keeps the
        // exact no-arg lowering.
        std::string threads;
        if (!c.arguments.empty() && c.arguments.front()) {
            const Expr* options = c.arguments.front().get();
            if (options->kind == NodeKind::ObjectLiteral) {
                const auto& literal = static_cast<const ObjectLiteral&>(*options);
                for (const auto& prop : literal.properties) {
                    if (!prop || !prop->value) continue;
                    if (objectKey(*prop) == "threads") threads = expr(*prop->value);
                }
            }
        }
        if (threads.empty()) return std::string(kRuntimeNs) + "HttpServer()";
        return std::string(kRuntimeNs) + "HttpServer(" + threads + ")";
    }
    // Ledger L (appended): middleware factories + bare class constructions.
    if (name == "static") {
        needsNetwork_ = true;
        std::string dir = c.arguments.empty() || !c.arguments.front() ? "std::string()" : expr(*c.arguments.front());
        return std::string(kRuntimeNs) + "makeStaticMiddleware(" + dir + ")";
    }
    if (name == "bodyLimit") {
        needsNetwork_ = true;
        std::string limit = c.arguments.empty() || !c.arguments.front() ? "0.0" : expr(*c.arguments.front());
        return std::string(kRuntimeNs) + "makeBodyLimitMiddleware(" + limit + ")";
    }
    if (name == "cors") {
        needsNetwork_ = true;
        std::string opts = std::string(kRuntimeNs) + "CorsOptions()";
        if (!c.arguments.empty() && c.arguments.front() &&
            c.arguments.front()->kind == NodeKind::ObjectLiteral) {
            const auto& lit = static_cast<const ObjectLiteral&>(*c.arguments.front());
            std::string inner;
            for (const auto& p : lit.properties) {
                if (!p || !p->value) continue;
                const std::string key = objectKey(*p);
                if (key == "origin") inner += ".origin = " + expr(*p->value) + "; ";
                else if (key == "methods") inner += ".methods = " + expr(*p->value) + "; ";
                else if (key == "headers") inner += ".headers = " + expr(*p->value) + "; ";
                else if (key == "credentials") inner += ".credentials = " + expr(*p->value) + "; ";
                else if (key == "maxAge") inner += ".maxAge = " + expr(*p->value) + "; ";
            }
            opts = "[&] { " + std::string(kRuntimeNs) + "CorsOptions o; " + inner + "return o; }()";
        } else if (!c.arguments.empty() && c.arguments.front()) {
            reportTypeError(*c.arguments.front(), "type mismatch: cors() options must be an object literal");
        }
        return std::string(kRuntimeNs) + "makeCorsMiddleware(" + opts + ")";
    }
    if (name == "Headers" || name == "FormData" || name == "Request" ||
        name == "Response" || name == "AbortSignal") {
        needsNetwork_ = true;
        needsEventLoop_ = name == "Request";
        return networkClassText(name, c.arguments,
            [this](const Expr& e) { return expr(e); },
            [this](const Expr& e) { return effectiveType(e).name; });
    }
    return std::string();
}

/// The fallback for a bare call to a name that is not a function declared in
/// this unit: the `std/network` named imports are lowered first, and everything
/// else is a value that is not callable (an intrinsic outside this surface,
/// console.* handled earlier, must be a program error rather than an undeclared
/// C++ name). The seam lives here so emitCall itself stays inside the
/// function-length budget.
std::string Generator::emitUnresolvedCall(const CallExpr& c, const std::string& name) {
    std::string net = emitNetworkIdentifier(c, name);
    if (!net.empty()) return net;
    // A middleware body calling `next()` (docs 04 §network): the parameter is
    // typed with the `$network:Next` marker in the temporary handler scope
    // (networkArg), so the call lowers to the runtime continuation.
    const std::optional<detail::Binding> binding = lookup(name);
    if (binding && binding->type.name == kNextMarker) {
        needsNetwork_ = true;
        if (!c.arguments.empty()) {
            // The documented continuation takes no argument (docs 04 §network:
            // `type NextFunction = () => Promise<void>`), so `next(x)` is an
            // arity error - a program error, not a missing feature.
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: the std/network next() "
                               "continuation takes no arguments, got " +
                                   std::to_string(c.arguments.size()));
            return "0.0";
        }
        return mapIdent(name) + "()";
    }
    // The name has no declaration in this unit and no lowering of its own: it
    // is a namespace object or a class, not a function. The same call on a
    // user class reports "not callable" (CP3404), so this is the house rule.
    reportCodegenError(c, diagcode::kNotCallable,
                       "value of type '" + name + "' is not callable");
    return "0.0";
}

// =============================================================================
// `new WebSocket(...)` / `new WebSocketServer(...)`
// =============================================================================

std::string Generator::emitNetworkNew(const NewExpr& n) {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::string();
    const std::string name = static_cast<const Identifier&>(*n.callee).name;
    // Ledger L (appended): `new Headers/Request/Response/FormData/AbortSignal`.
    if (name == "Headers" || name == "Request" || name == "Response" ||
        name == "FormData" || name == "AbortSignal") {
        needsNetwork_ = true;
        return networkClassText(name, n.arguments,
            [this](const Expr& e) { return expr(e); },
            [this](const Expr& e) { return effectiveType(e).name; });
    }
    if (name == "WebSocket") {
        needsNetwork_ = true;
        needsEventLoop_ = true;
        if (n.arguments.empty() || !n.arguments.front()) {
            // `new WebSocket(url)` (docs 04 §network): the URL is required.
            // Sema reports the arity error (src/std/network/sema.cpp).
            reportCodegenError(n, diagcode::kArityMismatch,
                               "new WebSocket(url) takes one argument");
            return "0.0";
        }
        return std::string(kRuntimeNs) + "WebSocket(" + expr(*n.arguments.front()) + ")";
    }
    if (name != "WebSocketServer") return std::string();
    needsNetwork_ = true;
    needsEventLoop_ = true;
    if (n.arguments.empty() || !n.arguments.front()) {
        // `new WebSocketServer({ port, path? })` (docs 04 §network): the
        // options object is required. Sema reports the arity error.
        reportCodegenError(n, diagcode::kArityMismatch,
                           "new WebSocketServer(options) takes one argument");
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
    // Ledger L (appended): `AbortSignal.timeout(ms)` (also reached via the
    // lead's `AbortSignal` dispatch line, see wiring notes).
    if (m.property == "timeout" && m.object &&
        m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "AbortSignal") {
        needsNetwork_ = true;
        std::string ms = c.arguments.empty() || !c.arguments.front() ? "0.0" : expr(*c.arguments.front());
        return std::string(kRuntimeNs) + "AbortSignal::timeout(" + ms + ")";
    }
    if (m.property == "fetch") return emitNetworkFetch(c, c.arguments);
    if (m.property == "lookup") return emitNetworkIdentifier(c, "lookup");
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
