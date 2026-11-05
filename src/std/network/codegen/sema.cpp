// =============================================================================
// src/std/network/sema.cpp - `std/network` surface typing.
//
// OWNERSHIP: the std/network family (src/std/network/**). Two shapes reach this file:
//   * the namespace corner (`network.fetch(...)`, the std_check.cpp row) and
//   * the documented named imports (`import { fetch, createServer, WebSocket,
//     WebSocketServer } from "std/network"`), which are ambient bindings, so
//     their call and construct sites are typed here (call_check.cpp and
//     expr_check.cpp hook `tryInferNetworkCall` / `inferNetworkConstruct`).
//
// Marker objects carry a `$network:` name and one closed field table. The
// Field rows below are the supported member set (docs 04 §network); the
// member checker and editor completion consume those same rows, so a typo is
// diagnosed and completion stays in sync. The Map-shaped rows name the landed
// `Map<string, string>`
// (std/collections) and `arrayBuffer` promises the landed `Buffer`.
// Marker names are shared with codegen (src/std/network/codegen.cpp), which
// uses them to decide which receivers it lowers.
// =============================================================================
#include "detail/analyzer.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

/// A numeric `threads` literal, including a negated one (`-1` parses as a
/// unary expression, not a NumberLiteral).
bool literalThreadCount(const Expr& e, double& out) {
    if (e.kind == NodeKind::NumberLiteral) {
        out = static_cast<const NumberLiteral&>(e).value;
        return true;
    }
    if (e.kind == NodeKind::UnaryExpr) {
        const auto& unary = static_cast<const UnaryExpr&>(e);
        if (unary.op == UnaryOp::Negate && unary.operand &&
            unary.operand->kind == NodeKind::NumberLiteral) {
            out = -static_cast<const NumberLiteral&>(*unary.operand).value;
            return true;
        }
    }
    return false;
}

const char* const kFetchResponse = "$network:FetchResponse";
const char* const kHttpRequest = "$network:HttpRequest";
const char* const kHttpResponse = "$network:HttpResponse";
const char* const kHttpServer = "$network:HttpServer";
const char* const kHttpNext = "$network:Next";
const char* const kWebSocket = "$network:WebSocket";
const char* const kWebSocketServer = "$network:WebSocketServer";

/// `Map<string, string>` (std/collections): the documented shape of the
/// request/fetch headers, route params and query string.
Type stringMap() {
    Type map(TypeKind::Class, "Map");
    map.parts.push_back(Type::string());
    map.parts.push_back(Type::string());
    return map;
}

/// The nominal std/buffer `Buffer` (src/std/buffer/sema.cpp).
Type bufferType() { return byteBufferType(); }

/// `(p0, p1, ...) => void` with permissive parameters, used for callback
/// surfaces that do not yet have a declared parameter model (such as generic
/// WebSocket event listeners). HTTP handlers use their explicit marker types.
std::vector<Type> permissiveParams(std::size_t arity) {
    return std::vector<Type>(arity, ambient());
}

Type callbackOf(std::size_t arity) {
    return Type::function(permissiveParams(arity), Type::voidType());
}

Type networkValue(const char* name) { return Type(TypeKind::Object, name); }

Type httpRouteHandler() {
    return Type::function({networkValue(kHttpRequest), networkValue(kHttpResponse)},
                          Type::voidType());
}

Type httpMiddleware() {
    Type next = Type::function({}, promiseOf(Type::voidType()));
    // Keep this callable-shaped for CP3's call checker, while codegen uses the
    // network callback context to lower the parameter as runtime::network::Next.
    next.name = kHttpNext;
    return Type::function({networkValue(kHttpRequest), networkValue(kHttpResponse),
                           next},
                          Type::voidType());
}

Type eventListener() { return callbackOf(2); }

// -----------------------------------------------------------------------------
// The documented field rows per receiver (docs 04 §network). These rows are
// the source of truth for typing, completion and diagnostics; `networkObjectMember`
// hands each accepted read its row type. SEAM comments mark the still-blocked
// swap point (the fs ReadStream body).
// -----------------------------------------------------------------------------
std::vector<Field> fetchResponseFields() {
    return {
        Field{"ok", Type::boolean(), false},
        Field{"status", Type::number(), false},
        Field{"statusText", Type::string(), false},
        // Map<string, string>; the runtime fills it in parseHead
        // (runtime_fetch.cpp) from the response header block.
        Field{"headers", stringMap(), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        // SEAM(json): non-scalar T is routed through the json reification at
        // codegen (networkTypeArgsText / the record arm); the
        // `tryInferNetworkMemberCall` hook substitutes the explicit T here.
        Field{"json", Type::function({}, promiseOf(ambient())), false},
        // The landed std/buffer `Buffer`.
        Field{"arrayBuffer", Type::function({}, promiseOf(bufferType())), false},
    };
}

std::vector<Field> httpRequestFields() {
    return {
        Field{"method", Type::string(), false},
        Field{"path", Type::string(), false},
        // The three Map<string, string> fields; the server fills them in
        // onRequest (runtime_server.cpp) from the native query string, the
        // header block and the matched `:name` route params.
        Field{"params", stringMap(), false},
        Field{"query", stringMap(), false},
        Field{"headers", stringMap(), false},
        // Parsed from the `Cookie` header.
        Field{"cookies", stringMap(), false},
        // The remote address as text.
        Field{"ip", Type::string(), false},
        // SEAM(readstream/fs): `body: ReadStream`; the runtime buffers the
        // body before the chain runs, and the landed fs ReadStream is
        // file-backed, so nothing stream-shaped exists yet.
        Field{"body", ambient(), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        Field{"json", Type::function({}, promiseOf(ambient())), false},
    };
}

std::vector<Field> httpResponseFields() {
    return {
        // `status` / `header` chain; the terminal writers return nothing.
        Field{"status",
              Type::function({Type::number()}, networkValue(kHttpResponse)), false},
        Field{"header",
              Type::function({Type::string(), Type::string()},
                             networkValue(kHttpResponse)), false},
        Field{"text",
              withOptionalTail(Type::function({Type::string(), Type::number()},
                                              Type::voidType())), false},
        // `res.json(payload)`: a scalar spells directly, an object/array
        // literal lowers to the json dynamic value tree (codegen_methods.cpp),
        // and a reified interface goes through the record tables.
        Field{"json",
              withOptionalTail(Type::function({Type::unknown(), Type::number()},
                                              Type::voidType())), false},
        Field{"send",
              withOptionalTail(Type::function({Type::number()}, Type::voidType())),
              false},
        Field{"redirect",
              withOptionalTail(
                  Type::function({Type::string(), Type::number()},
                                 Type::voidType())),
              false},
        Field{"sendFile", Type::function({Type::string()}, Type::voidType()),
              false},
        Field{"cookie",
              withOptionalTail2(Type::function(
                  {Type::string(), Type::string(), Type::unknown()},
                  networkValue(kHttpResponse))),
              false},
        Field{"write",
              Type::function({Type::unionOf({Type::string(), bufferType()})},
                             Type::voidType()), false},
        Field{"end", Type::function({}, Type::voidType()), false},
    };
}

std::vector<Field> httpServerFields() {
    std::vector<Field> fields;
    const Type route = httpRouteHandler();
    // The documented four verbs plus the Express-style extras the runtime grew
    // so this table stays truthful; codegen maps the cp3 `delete` onto the
    // runtime `del` member (`delete` is a C++ keyword).
    for (const char* verb : {"get", "post", "put", "delete", "patch", "head", "options",
                             "any"}) {
        fields.push_back(Field{verb, Type::function({Type::string(), route}, Type::voidType()),
                               false});
    }
    fields.push_back(Field{"use", Type::function({httpMiddleware()}, Type::voidType()),
                           false});
    fields.push_back(
        Field{"listen",
              withOptionalTail(
                  Type::function({Type::number(), callbackOf(0)}, Type::voidType())),
              false});
    fields.push_back(Field{"close", Type::function({}, Type::voidType()), false});
    return fields;
}

std::vector<Field> webSocketFields() {
    return {
        Field{"on", Type::function({Type::string(), eventListener()}, Type::voidType()),
              false},
        // `send(data: string | Buffer)` (docs 04 §network): the landed
        // std/buffer type joins the string arm, exactly like the crypto
        // hash inputs (src/std/crypto/sema.cpp).
        Field{"send",
              Type::function({Type::unionOf({Type::string(), bufferType()})},
                             Type::voidType()),
              false},
        Field{"close", withOptionalTail2(callbackOf(2)), false},
    };
}

std::vector<Field> webSocketServerFields() {
    return {
        Field{"on", Type::function({Type::string(), eventListener()}, Type::voidType()),
              false},
        Field{"close", Type::function({}, Type::voidType()), false},
    };
}

/// The documented field rows of one marker table, or {} when `name` is not a
/// network marker. Shared by `networkObjectType` (the marker itself) and
/// `networkObjectMember` (one documented read).
std::vector<Field> networkFieldsFor(const std::string& name) {
    if (name == kFetchResponse) return fetchResponseFields();
    if (name == kHttpRequest) return httpRequestFields();
    if (name == kHttpResponse) return httpResponseFields();
    if (name == kHttpServer) return httpServerFields();
    if (name == kWebSocket) return webSocketFields();
    if (name == kWebSocketServer) return webSocketServerFields();
    return {};
}

}  // namespace

Type Analyzer::networkObjectType(const std::string& name) {
    std::vector<Field> fields = networkFieldsFor(name);
    if (fields.empty()) return Type::unknown();
    // Keep the nominal network marker; member_check uses the same field table
    // to resolve known members and report CP3305 for unknown ones.
    return Type(TypeKind::Object, name);
}

std::optional<Type> Analyzer::networkObjectMember(const Type& recv,
                                                  const std::string& prop) {
    if (recv.kind != TypeKind::Object) return std::nullopt;
    const std::vector<Field> fields = networkFieldsFor(recv.name);
    for (const Field& field : fields) {
        if (field.name == prop) return field.type;
    }
    return std::nullopt;
}

std::vector<std::string> networkObjectMemberNames(
    const std::string& objectTypeName) {
    std::vector<std::string> names;
    for (const Field& field : networkFieldsFor(objectTypeName)) {
        names.push_back(field.name);
    }
    return names;
}

StdMemberDoc networkObjectMemberDoc(const std::string& objectTypeName,
                                   const std::string& member) {
    const std::string type = objectTypeName.rfind("$network:", 0) == 0
                                 ? objectTypeName.substr(9)
                                 : objectTypeName;
    if (type == "HttpServer") {
        if (member == "get" || member == "post" || member == "put" ||
            member == "delete" || member == "patch" || member == "head" ||
            member == "options" || member == "any") {
            return {member + "(path: string, handler: (req: HttpRequest, res: HttpResponse) => void): void",
                    "Registers a handler for matching requests that use this HTTP method. The handler receives a typed request and response. Route handlers may also be `async` and return `Promise<void>`.\n\n"
                    "**Example**\n\n```cp3\napp." + member + "(\"/items\", (req, res) => {\n  res.json({ items: [] });\n});\n```"};
        }
        if (member == "use") return {"use(handler: (req: HttpRequest, res: HttpResponse, next: () => Promise<void>) => void): void",
            "Registers middleware in order. Call `next()` (or `await next()` in an async handler) to continue to the next matching route or middleware. A response can be sent without calling `next()` to finish the request.\n\n"
            "**Example**\n\n```cp3\napp.use(async (req, res, next) => {\n  console.log(req.method, req.path);\n  await next();\n});\n```"};
        if (member == "listen") return {"listen(port: number, callback?: () => void): void",
            "Starts accepting HTTP requests on `port`. The optional callback runs once the listener is ready.\n\n**Example**\n\n```cp3\napp.listen(3000, () => console.log(\"ready\"));\n```"};
        if (member == "close") return {"close(): void", "Stops accepting new connections and closes the server."};
    }
    if (type == "HttpRequest") {
        if (member == "method") return {"method: string", "The request method, such as `GET` or `POST`."};
        if (member == "path") return {"path: string", "The URL path without its query string."};
        if (member == "params") return {"params: Map<string, string>", "Route parameters captured from a route pattern such as `/users/:id`."};
        if (member == "query") return {"query: Map<string, string>", "URL query parameters parsed from the request."};
        if (member == "headers") return {"headers: Map<string, string>", "Request headers. Header names are looked up case-insensitively by the runtime."};
        if (member == "cookies") return {"cookies: Map<string, string>", "Cookies from the request's `Cookie` header (values are raw, not decoded)."};
        if (member == "ip") return {"ip: string", "The remote address that connected, as text."};
        if (member == "body") return {"body: unknown", "The buffered request body. Prefer `text()` or `json<T>()` to decode it."};
        if (member == "text") return {"text(): Promise<string>", "Returns the buffered request body as text."};
        if (member == "json") return {"json<T>(): Promise<T>", "Parses the buffered request body as JSON and types the result as `T`. The annotation describes the expected shape; validate untrusted input before relying on it."};
    }
    if (type == "HttpResponse") {
        if (member == "status") return {"status(code: number): HttpResponse", "Sets the status for the response and returns the response so calls can be chained."};
        if (member == "header") return {"header(name: string, value: string): HttpResponse", "Sets a response header and returns the response so calls can be chained."};
        if (member == "text") return {"text(data: string, status?: number): void", "Sends a text response. This is a terminal write: once a response has been sent, later terminal writes are ignored."};
        if (member == "json") return {"json(data: unknown, status?: number): void", "Serializes a value as JSON and sends it as the response. A second argument may set the status."};
        if (member == "send") return {"send(status?: number): void", "Sends the response data previously buffered with `write`, or an empty response."};
        if (member == "redirect") return {"redirect(url: string, status?: number): void", "Answers with a redirect (302 by default) and the `Location` header. Terminal."};
        if (member == "sendFile") return {"sendFile(path: string): void", "Serves the file's bytes with a content type from its extension, or 404 when missing. Terminal."};
        if (member == "cookie") return {"cookie(name: string, value: string, options?: object): HttpResponse", "Appends a `Set-Cookie` header; options may carry `maxAge` (seconds), `path`, `httpOnly` and `secure`. Returns the response for chaining."};
        if (member == "write") return {"write(chunk: string | Buffer): void", "Appends a string or raw Buffer bytes to a chunked response. Finish the response with `end()`."};
        if (member == "end") return {"end(): void", "Finishes a chunked response started with `write()`."};
    }
    if (type == "FetchResponse") {
        if (member == "ok") return {"ok: boolean", "Whether the response status is in the successful 200–299 range."};
        if (member == "status") return {"status: number", "The HTTP status code returned by the server."};
        if (member == "statusText") return {"statusText: string", "The response's reason phrase, when provided."};
        if (member == "headers") return {"headers: Map<string, string>", "The response headers."};
        if (member == "text") return {"text(): Promise<string>", "Reads the response body as text."};
        if (member == "json") return {"json<T>(): Promise<T>", "Parses the response body as JSON and types the result as `T`."};
        if (member == "arrayBuffer") return {"arrayBuffer(): Promise<Buffer>", "Reads the response body as raw bytes in a Buffer."};
    }
    if (type == "WebSocket" && member == "send") return {"send(data: string | Buffer): void", "Sends UTF-8 text or raw Buffer bytes to the connected WebSocket peer."};
    if (type == "WebSocketServer" && member == "close") return {"close(): void", "Closes the WebSocket listener and its connections."};
    return {};
}

std::optional<Type> Analyzer::networkMember(const std::string& prop) {
    if (prop == "fetch") {
        // `network.fetch(url, init?)`: the init is ambient until RequestInit
        // has a shape; the trailing optional keeps the documented one-argument
        // call from reporting an arity mismatch.
        return withOptionalTail(Type::function(
            {Type::string(), ambient()}, promiseOf(networkObjectType(kFetchResponse))));
    }
    if (prop == "createServer") {
        // `createServer(options?)` (D79): one optional objects argument.
        return withOptionalTail(
            Type::function({ambient()}, networkObjectType(kHttpServer)));
    }
    if (prop == "WebSocket") {
        return Type::function({Type::string()}, networkObjectType(kWebSocket));
    }
    if (prop == "WebSocketServer") {
        return Type::function({ambient()}, networkObjectType(kWebSocketServer));
    }
    return std::nullopt;
}

bool Analyzer::tryInferNetworkCall(CallExpr& call, const Identifier& id, Type& out) {
    if (id.name == "fetch") {
        inferArgs(call);
        // `fetch(url, options?)` (docs 04 §network): the URL is required, the
        // options object is optional. A call without the URL is a program
        // error here, not a codegen skip: the runtime has no URL to fetch.
        if (call.arguments.size() > 2 || call.arguments.empty()) {
            report(diagcode::kArityMismatch, anchorSpan(call.span),
                   "fetch(url, options?) takes one or two arguments");
        }
        out = promiseOf(networkObjectType(kFetchResponse));
        return true;
    }
    if (id.name == "createServer") {
        if (checkCreateServerOptions(call)) out_.networkScaleMode = true;
        out = networkObjectType(kHttpServer);
        return true;
    }
    return false;
}

/// D79: `createServer(options?)`. The only accepted shape is one object
/// literal with an optional `threads` property. Literal thread counts are
/// checked here (0, negative, fractional and non-number literals become
/// CP3401); a computed value is validated by the runtime constructor, and the
/// literal 1 keeps the single-loop path.
bool Analyzer::checkCreateServerOptions(CallExpr& call) {
    inferArgs(call);
    if (call.arguments.empty()) return false;
    if (call.arguments.size() > 1 || !call.arguments.front()) {
        report(diagcode::kTypeMismatch, anchorSpan(call.span),
               "createServer() takes at most one argument (an options object: "
               "{ threads?: number })");
        return false;
    }
    const Expr& options = *call.arguments.front();
    if (options.kind != NodeKind::ObjectLiteral) {
        report(diagcode::kTypeMismatch, anchorSpan(options.span),
               "createServer() options must be an object literal: "
               "{ threads?: number }");
        return false;
    }
    bool scale = false;
    const auto& literal = static_cast<const ObjectLiteral&>(options);
    for (const auto& propPtr : literal.properties) {
        const ObjectProperty* prop = propPtr.get();
        if (prop == nullptr) continue;
        if (prop->computed || prop->key == nullptr) {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "createServer() options only support the 'threads' property");
            continue;
        }
        const std::string key = keyName(prop->key.get());
        if (key != "threads") {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "unknown createServer() option '" + key +
                       "' (only 'threads' is supported)");
            continue;
        }
        if (prop->value == nullptr) continue;
        double literalCount = 0.0;
        if (literalThreadCount(*prop->value, literalCount)) {
            if (!std::isfinite(literalCount) || literalCount < 1.0 ||
                literalCount != std::floor(literalCount)) {
                report(diagcode::kTypeMismatch, anchorSpan(prop->value->span),
                       "createServer() 'threads' must be an integer >= 1");
            } else if (literalCount != 1.0) {
                scale = true;
            }
            continue;
        }
        // A computed value may still be known to be non-numeric; otherwise it
        // is accepted and checked by the runtime constructor.
        const Type valueType = typeOfExpr(*prop->value);
        const bool numericLiteral =
            valueType.kind == TypeKind::Literal &&
            valueType.literalBaseType().kind == TypeKind::Number;
        if (valueType.kind == TypeKind::Number || numericLiteral ||
            valueType.kind == TypeKind::Unknown ||
            valueType.kind == TypeKind::Error) {
            scale = true;
        } else {
            report(diagcode::kTypeMismatch, anchorSpan(prop->value->span),
                   "createServer() 'threads' must be a number");
        }
    }
    return scale;
}

Type Analyzer::inferNetworkConstruct(const std::string& name, ExprList& args,
                                     const SourceSpan& span) {
    if (name != "WebSocket" && name != "WebSocketServer") return Type::unknown();
    for (auto& arg : args) {
        if (arg) inferExpr(*arg);
    }
    // `new WebSocket(url)` and `new WebSocketServer({ port, path? })`
    // (docs 04 §network): both take exactly one argument. A call without it
    // is a program error here (the runtime has no URL / no port to bind), not
    // a codegen skip.
    if (args.size() != 1) {
        report(diagcode::kArityMismatch, anchorSpan(span),
               name == "WebSocket" ? "new WebSocket(url) takes one argument"
                                   : "new WebSocketServer(options) takes one "
                                     "argument");
    }
    return name == "WebSocket" ? networkObjectType(kWebSocket)
                               : networkObjectType(kWebSocketServer);
}

bool Analyzer::tryInferNetworkMemberCall(CallExpr& call, MemberExpr& m, Type& out) {
    // `network.createServer(...)` (D79): the namespace spelling of the same
    // construction path, with the same options validation.
    if (m.property == "createServer" && m.object &&
        m.object->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*m.object);
        Binding binding;
        if (id.name == "network" && lookup(id.name, binding) && binding.isAmbient) {
            if (checkCreateServerOptions(call)) out_.networkScaleMode = true;
            out = networkObjectType(kHttpServer);
            return true;
        }
    }
    if (m.property != "json" || m.object == nullptr) return false;
    const Type recv = typeOfExpr(*m.object);
    if (recv.kind != TypeKind::Object) return false;
    if (recv.name != kFetchResponse && recv.name != kHttpRequest) return false;
    if (call.typeArgs.empty() || !call.typeArgs.front()) return false;
    // `res.json<T>()` / `req.json<T>()` takes no arguments (docs 04 §network);
    // the explicit T substitutes into the promise exactly like
    // `JSON.parse<T>` (call_check.cpp's tryInferJsonCall), so `await
    // res.json<User>()` is the interface shape and `u.id` lowers through the
    // json record tables.
    Type value = Type::unknown();
    for (auto& ta : call.typeArgs) {
        if (!ta) continue;
        value = resolveType(*ta);
    }
    FuncSig sig;
    sig.annotatedReturn = true;
    sig.name = "json";
    sig.ret = promiseOf(value);
    checkArgs(sig, call);
    out = withType(call, sig.ret);
    return true;
}

}  // namespace cp3
