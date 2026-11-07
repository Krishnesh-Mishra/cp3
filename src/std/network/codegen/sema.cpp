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
#include "cp3/host/std_integer.hpp"

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
const char* const kTlsOptions = "$network:TlsOptions";
// Ledger L (appended): Headers/Request/Response/FormData/AbortSignal markers.
const char* const kHeaders = "$network:Headers";
const char* const kRequest = "$network:Request";
const char* const kResponse = "$network:Response";
const char* const kFormData = "$network:FormData";
const char* const kAbortSignal = "$network:AbortSignal";
// webserver agents (pasted): router / routing / errors.
const char* const kRouter = "$network:Router";
const char* const kRoute = "$network:Route";
const char* const kRouteChain = "$network:RouteChain";
const char* const kwebserverApp = "$network:webserverApp";
const char* const kHttpError = "$network:HttpError";

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

// Ledger L (appended): `app.onError((err, req, res) => ...)` handler type.
// `err` is ambient (the runtime passes a Thrown); req/res are typed so member
// calls inside the body resolve.
Type httpErrorHandler() {
    return Type::function({ambient(), networkValue(kHttpRequest), networkValue(kHttpResponse)},
                          Type::voidType());
}

// -----------------------------------------------------------------------------
// The documented field rows per receiver (docs 04 §network). These rows are
// the source of truth for typing, completion and diagnostics; `networkObjectMember`
// hands each accepted read its row type. SEAM comments mark the still-blocked
// swap point (the fs ReadStream body).
// -----------------------------------------------------------------------------
std::vector<Field> fetchResponseFields(TypeSystem ts) {
    return {
        Field{"ok", Type::boolean(), false},
        Field{"status", stdIntegerType(StdIntegerKind::Status, ts), false},
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
        // Cookie agent: verified signed cookies.
        Field{"signedCookies", stringMap(), false},
        // Router agent: mount-path reads.
        Field{"baseUrl", Type::string(), false},
        Field{"originalUrl", Type::string(), false},
        // The remote address as text.
        Field{"ip", Type::string(), false},
        // Settings family: trust-proxy protocol ("http" unless a trusted
        // X-Forwarded-Proto was applied by the config trustProxy glue).
        Field{"protocol", Type::string(), false},
        // SEAM(readstream/fs): `body: ReadStream`; the runtime buffers the
        // body before the chain runs, and the landed fs ReadStream is
        // file-backed, so nothing stream-shaped exists yet.
        Field{"body", ambient(), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        Field{"json", Type::function({}, promiseOf(ambient())), false},
    };
}

std::vector<Field> httpResponseFields(TypeSystem ts) {
    const Type status = stdIntegerType(StdIntegerKind::Status, ts);
    return {
        // `status` / `header` chain; the terminal writers return nothing.
        Field{"status",
              Type::function({status}, networkValue(kHttpResponse)), false},
        Field{"header",
              Type::function({Type::string(), Type::string()},
                             networkValue(kHttpResponse)), false},
        Field{"text",
              withOptionalTail(Type::function({Type::string(), status},
                                              Type::voidType())), false},
        // `res.json(payload)`: a scalar spells directly, an object/array
        // literal lowers to the json dynamic value tree (codegen_methods.cpp),
        // and a reified interface goes through the record tables.
        // `res.json()` / `res.json(payload)` / `res.json(payload, status)`:
        // the generic writer form takes no value argument at all, so the
        // payload is optional together with the status.
        Field{"json",
              withOptionalTail2(Type::function({Type::unknown(), status},
                                               Type::voidType())), false},
        Field{"send",
              withOptionalTail(Type::function({status}, Type::voidType())),
              false},
        Field{"redirect",
              withOptionalTail(
                  Type::function({Type::string(), status}, Type::voidType())),
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
        // Cookie agent.
        Field{"clearCookie",
              withOptionalTail(Type::function(
                  {Type::string(), Type::unknown()},
                  networkValue(kHttpResponse))),
              false},
        // Response agent (§3a): chainable header helpers + terminals.
        Field{"set",
              withOptionalTail(Type::function(
                  {Type::unknown(), Type::unknown()},
                  networkValue(kHttpResponse))),
              false},
        Field{"append",
              Type::function({Type::string(), Type::string()},
                             networkValue(kHttpResponse)),
              false},
        Field{"get",
              Type::function({Type::string()}, Type::string()),
              false},
        Field{"type",
              Type::function({Type::string()}, networkValue(kHttpResponse)),
              false},
        Field{"format",
              Type::function({Type::unknown()}, Type::voidType()),
              false},
        Field{"render",
              withOptionalTail(Type::function(
                  {Type::string(), Type::unknown()}, Type::voidType())),
              false},
        Field{"download",
              withOptionalTail(Type::function(
                  {Type::string(), Type::string()}, Type::voidType())),
              false},
        Field{"attach",
              Type::function({Type::string()}, networkValue(kHttpResponse)),
              false},
        Field{"links",
              Type::function({Type::unknown()}, networkValue(kHttpResponse)),
              false},
        Field{"location",
              Type::function({Type::string()}, networkValue(kHttpResponse)),
              false},
        Field{"vary",
              Type::function({Type::unknown()}, networkValue(kHttpResponse)),
              false},
        Field{"headersSent", Type::boolean(), false},
    };
}

std::vector<Field> httpServerFields(TypeSystem ts) {
    std::vector<Field> fields;
    const Type route = httpRouteHandler();
    // The documented four verbs plus the webserver-style extras the runtime grew
    // so this table stays truthful; codegen maps the cp3 `delete` onto the
    // runtime `del` member (`delete` is a C++ keyword).
    for (const char* verb : {"get", "post", "put", "delete", "patch", "head", "options",
                             "any"}) {
        fields.push_back(Field{verb, Type::function({Type::string(), route}, Type::voidType()),
                               false});
    }
    fields.push_back(Field{"use", Type::function({httpMiddleware()}, Type::voidType()),
                           false});
    // Router agent [S7]: app-mount spellings.
    fields.push_back(Field{"use",
                           Type::function({networkValue(kRouter)},
                                          Type::voidType()), false});
    fields.push_back(Field{"use",
                           Type::function({Type::string(),
                                           networkValue(kRouter)},
                                          Type::voidType()), false});
    // Middleware agent: app.use("/api", mw) mount form.
    fields.push_back(Field{"use",
                           Type::function({Type::string(), httpMiddleware()},
                                          Type::voidType()), false});
    // Cookie agent: app.set(name, value) chainable ("secret" lowered).
    fields.push_back(Field{"set",
                           Type::function({Type::string(), Type::unknown()},
                                          networkValue(kHttpServer)),
                           false});
    // Regex-routing agent: app settings.
    fields.push_back(Field{"enable",
                           Type::function({Type::string()}, networkValue(kHttpServer)),
                           false});
    fields.push_back(Field{"disable",
                           Type::function({Type::string()}, networkValue(kHttpServer)),
                           false});
    fields.push_back(Field{"enabled", Type::function({Type::string()}, Type::boolean()), false});
    fields.push_back(Field{"disabled", Type::function({Type::string()}, Type::boolean()), false});
    // Routing agent: all / route / param.
    fields.push_back(Field{"all", Type::function({Type::string(), route}, Type::voidType()), false});
    fields.push_back(Field{"route", Type::function({Type::string()}, networkValue(kRouteChain)), false});
    fields.push_back(Field{"param",
                           Type::function({Type::string(),
                                           Type::function({networkValue(kHttpRequest),
                                                           networkValue(kHttpResponse),
                                                           Type(TypeKind::Object, kHttpNext),
                                                           Type::string()},
                                                          Type::voidType())},
                                          Type::voidType()), false});
    // Ledger L (appended): `app.onError((err, req, res) => ...)`.
    fields.push_back(Field{"onError", Type::function({httpErrorHandler()}, Type::voidType()),
                           false});
    fields.push_back(
        Field{"listen",
              withOptionalTail(Type::function(
                  {stdIntegerType(StdIntegerKind::Port, ts), callbackOf(0)},
                  Type::voidType())),
              false});
    fields.push_back(Field{"close", Type::function({}, Type::voidType()), false});
    return fields;
}

// Router agent [S2] (pasted from sema_router.cpp).
std::vector<Field> routerFields(TypeSystem ts) {
    std::vector<Field> fields;
    const Type route = httpRouteHandler();
    for (const char* verb : {"get", "post", "put", "delete", "patch", "head",
                             "options", "all"}) {
        fields.push_back(Field{verb, Type::function({Type::string(), route}, Type::voidType()),
                               false});
    }
    const Type router = networkValue(kRouter);
    fields.push_back(Field{"use",
                           Type::function({httpMiddleware()}, Type::voidType()), false});
    fields.push_back(Field{"use",
                           Type::function({Type::string(), httpMiddleware()},
                                          Type::voidType()), false});
    fields.push_back(Field{"use",
                           Type::function({Type::string(), router},
                                          Type::voidType()), false});
    fields.push_back(Field{"use",
                           Type::function({router}, Type::voidType()), false});
    fields.push_back(Field{"param",
                           Type::function(
                               {Type::string(),
                                Type::function({networkValue(kHttpRequest),
                                                networkValue(kHttpResponse),
                                                Type(TypeKind::Object, kHttpNext),
                                                Type::string()},
                                               Type::voidType())},
                               Type::voidType()), false});
    fields.push_back(Field{"route",
                           Type::function({Type::string()}, networkValue(kRoute)),
                           false});
    fields.push_back(Field{"handle",
                           Type::function(
                               {networkValue(kHttpRequest),
                                networkValue(kHttpResponse),
                                Type(TypeKind::Object, kHttpNext)},
                               promiseOf(Type::voidType())), false});
    fields.push_back(Field{"handler",
                           Type::function({}, httpMiddleware()), false});
    fields.push_back(Field{"mountMiddleware",
                           Type::function({Type::string()}, httpMiddleware()),
                           false});
    (void)ts;
    return fields;
}

std::vector<Field> routeFields() {
    std::vector<Field> fields;
    const Type route = httpRouteHandler();
    for (const char* verb : {"get", "post", "put", "delete", "patch", "head",
                             "options", "all"}) {
        fields.push_back(
            Field{verb, Type::function({route}, networkValue(kRoute)), false});
    }
    return fields;
}

// Routing agent (pasted from sema_routing.cpp): RouteChain verbs return the chain.
std::vector<Field> routeChainFields() {
    std::vector<Field> fields;
    Type handlerArray = Type(TypeKind::Array, "Array");
    handlerArray.parts.push_back(httpRouteHandler());
    const Type handlerList = Type::unionOf({httpRouteHandler(), handlerArray});
    const Type chain = networkValue(kRouteChain);
    for (const char* verb : {"get", "post", "put", "delete", "patch", "head",
                             "options", "all"}) {
        fields.push_back(Field{verb, Type::function({handlerList}, chain), false});
    }
    return fields;
}

// Errors agent (pasted from sema_errors.cpp): HttpError fields.
std::vector<Field> httpErrorFields() {
    return {
        Field{"status", Type::number(), false},
        Field{"statusCode", Type::number(), false},
        Field{"expose", Type::boolean(), false},
        Field{"message", Type::string(), false},
        Field{"code", Type::string(), false},
        Field{"stack", Type::string(), false},
    };
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

/// `$network:TlsOptions` fields (`tls({...})` produces this type).
std::vector<Field> tlsOptionsFields() {
    return {
        Field{"cert", Type::string(), false},
        Field{"key", Type::string(), false},
        Field{"ca", Type::string(), false},
        Field{"passphrase", Type::string(), false},
        Field{"minProtocol", Type::string(), false},
        Field{"servername", Type::string(), false},
        Field{"rejectUnauthorized", Type::boolean(), false},
        Field{"checkServerIdentity", Type::boolean(), false},
    };
}

std::vector<Field> webSocketServerFields() {
    return {
        Field{"on", Type::function({Type::string(), eventListener()}, Type::voidType()),
              false},
        Field{"close", Type::function({}, Type::voidType()), false},
    };
}

// Ledger L (appended) field rows.
std::vector<Field> headersFields() {
    const Type fn2 = Type::function({Type::string(), Type::string()}, Type::voidType());
    return {
        Field{"get", Type::function({Type::string()}, Type::string()), false},
        Field{"has", Type::function({Type::string()}, Type::boolean()), false},
        Field{"set", fn2, false},
        Field{"append", fn2, false},
        Field{"delete", Type::function({Type::string()}, Type::voidType()), false},
    };
}
std::vector<Field> formDataFields() {
    const Type fn2 = Type::function({Type::string(), Type::string()}, Type::voidType());
    return {
        Field{"append", fn2, false},
        Field{"get", Type::function({Type::string()}, Type::string()), false},
        Field{"has", Type::function({Type::string()}, Type::boolean()), false},
        Field{"set", fn2, false},
        Field{"delete", Type::function({Type::string()}, Type::voidType()), false},
    };
}
std::vector<Field> abortSignalFields() {
    return {
        Field{"aborted", Type::boolean(), false},
        Field{"timeout", Type::function({Type::number()}, networkValue(kAbortSignal)), false},
    };
}
std::vector<Field> requestFields() {
    return {
        Field{"url", Type::string(), false},
        Field{"method", Type::string(), false},
        Field{"headers", networkValue(kHeaders), false},
        Field{"signal", networkValue(kAbortSignal), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        Field{"json", Type::function({}, promiseOf(ambient())), false},
        Field{"arrayBuffer", Type::function({}, promiseOf(bufferType())), false},
    };
}
std::vector<Field> responseFields(TypeSystem ts) {
    return {
        Field{"ok", Type::boolean(), false},
        Field{"status", stdIntegerType(StdIntegerKind::Status, ts), false},
        Field{"statusText", Type::string(), false},
        Field{"headers", networkValue(kHeaders), false},
        Field{"url", Type::string(), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        Field{"json", Type::function({}, promiseOf(ambient())), false},
        Field{"arrayBuffer", Type::function({}, promiseOf(bufferType())), false},
    };
}

/// The documented field rows of one marker table, or {} when `name` is not a
/// network marker. Shared by `networkObjectType` (the marker itself) and
/// `networkObjectMember` (one documented read).
std::vector<Field> networkFieldsFor(const std::string& name, TypeSystem ts) {
    if (name == kFetchResponse) return fetchResponseFields(ts);
    if (name == kHttpRequest) return httpRequestFields();
    if (name == kHttpResponse) return httpResponseFields(ts);
    if (name == kHttpServer) return httpServerFields(ts);
    if (name == kWebSocket) return webSocketFields();
    if (name == kWebSocketServer) return webSocketServerFields();
    if (name == kHeaders) return headersFields();
    if (name == kFormData) return formDataFields();
    if (name == kAbortSignal) return abortSignalFields();
    if (name == kRequest) return requestFields();
    if (name == kResponse) return responseFields(ts);
    if (name == kRouter) return routerFields(ts);
    if (name == kRoute) return routeFields();
    if (name == kRouteChain) return routeChainFields();
    if (name == kwebserverApp) return routerFields(ts);
    if (name == kHttpError) return httpErrorFields();
    if (name == kTlsOptions) return tlsOptionsFields();
    return {};
}

}  // namespace

Type Analyzer::networkObjectType(const std::string& name) {
    std::vector<Field> fields = networkFieldsFor(name, options_.typeSystem);
    if (fields.empty()) return Type::unknown();
    // Keep the nominal network marker; member_check uses the same field table
    // to resolve known members and report CP3305 for unknown ones.
    return Type(TypeKind::Object, name);
}

std::optional<Type> Analyzer::networkObjectMember(const Type& recv,
                                                  const std::string& prop) {
    if (recv.kind != TypeKind::Object) return std::nullopt;
    const std::vector<Field> fields =
        networkFieldsFor(recv.name, options_.typeSystem);
    for (const Field& field : fields) {
        if (field.name == prop) return field.type;
    }
    return std::nullopt;
}

std::vector<std::string> networkObjectMemberNames(
    const std::string& objectTypeName) {
    std::vector<std::string> names;
    for (const Field& field :
         networkFieldsFor(objectTypeName, TypeSystem::TypeScript)) {
        names.push_back(field.name);
    }
    return names;
}

StdMemberDoc networkObjectMemberDoc(const std::string& objectTypeName,
                                   const std::string& member,
                                   TypeSystem ts) {
    const std::string status = stdIntegerSpelling(StdIntegerKind::Status, ts);
    const std::string port = stdIntegerSpelling(StdIntegerKind::Port, ts);
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
        if (member == "onError") return {"onError(handler: (err: unknown, req: HttpRequest, res: HttpResponse) => void): void",
            "Registers the error handler invoked when a route/middleware handler throws or rejects and no response was sent yet. Without it the dispatcher answers 500.\n\n**Example**\n\n```cp3\napp.onError((err, req, res) => {\n  res.status(500).json({ error: \"failed\" });\n});\n```"};
        if (member == "listen") return {"listen(port: " + port + ", callback?: () => void): void",
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
        if (member == "status") return {"status(code: " + status + "): HttpResponse", "Sets the status for the response and returns the response so calls can be chained."};
        if (member == "header") return {"header(name: string, value: string): HttpResponse", "Sets a response header and returns the response so calls can be chained."};
        if (member == "text") return {"text(data: string, status?: " + status + "): void", "Sends a text response. This is a terminal write: once a response has been sent, later terminal writes are ignored."};
        if (member == "json") return {"json(data: unknown, status?: " + status + "): void", "Serializes a value as JSON and sends it as the response. A second argument may set the status."};
        if (member == "send") return {"send(status?: " + status + "): void", "Sends the response data previously buffered with `write`, or an empty response."};
        if (member == "redirect") return {"redirect(url: string, status?: " + status + "): void", "Answers with a redirect (302 by default) and the `Location` header. Terminal."};
        if (member == "sendFile") return {"sendFile(path: string): void", "Serves the file's bytes with a content type from its extension, or 404 when missing. Terminal."};
        if (member == "cookie") return {"cookie(name: string, value: string, options?: object): HttpResponse", "Appends a `Set-Cookie` header; options may carry `maxAge` (seconds), `path`, `httpOnly` and `secure`. Returns the response for chaining."};
        if (member == "write") return {"write(chunk: string | Buffer): void", "Appends a string or raw Buffer bytes to a chunked response. Finish the response with `end()`."};
        if (member == "end") return {"end(): void", "Finishes a chunked response started with `write()`."};
    }
    if (type == "FetchResponse") {
        if (member == "ok") return {"ok: boolean", "Whether the response status is in the successful 200–299 range."};
        if (member == "status") return {"status: " + status, "The HTTP status code returned by the server."};
        if (member == "statusText") return {"statusText: string", "The response's reason phrase, when provided."};
        if (member == "headers") return {"headers: Map<string, string>", "The response headers."};
        if (member == "text") return {"text(): Promise<string>", "Reads the response body as text."};
        if (member == "json") return {"json<T>(): Promise<T>", "Parses the response body as JSON and types the result as `T`."};
        if (member == "arrayBuffer") return {"arrayBuffer(): Promise<Buffer>", "Reads the response body as raw bytes in a Buffer."};
    }
    if (type == "WebSocket" && member == "send") return {"send(data: string | Buffer): void", "Sends UTF-8 text or raw Buffer bytes to the connected WebSocket peer."};
    if (type == "WebSocketServer" && member == "close") return {"close(): void", "Closes the WebSocket listener and its connections."};
    // Ledger L (appended) docs.
    if (type == "Headers") {
        if (member == "get") return {"get(name: string): string", "Returns the header value, or \"\" when missing."};
        if (member == "has") return {"has(name: string): boolean", "Whether the header is present (case-insensitive)."};
        if (member == "set") return {"set(name: string, value: string): void", "Sets a header, replacing any existing value."};
        if (member == "append") return {"append(name: string, value: string): void", "Appends a header value."};
        if (member == "delete") return {"delete(name: string): void", "Removes a header."};
    }
    if (type == "FormData") {
        if (member == "append") return {"append(name: string, value: string): void", "Appends a form field; usable as a fetch `body`."};
        if (member == "get") return {"get(name: string): string", "Returns the field value, or \"\" when missing."};
        if (member == "has") return {"has(name: string): boolean", "Whether the field is present."};
        if (member == "set") return {"set(name: string, value: string): void", "Sets a form field."};
        if (member == "delete") return {"delete(name: string): void", "Removes a form field."};
    }
    if (type == "AbortSignal") {
        if (member == "aborted") return {"aborted: boolean", "Whether the signal has aborted."};
        if (member == "timeout") return {"timeout(ms: number): AbortSignal", "Creates a signal that times out after `ms`; `fetch(url, { signal })` applies it as the request timeout."};
    }
    if (type == "Request") {
        if (member == "url") return {"url: string", "The request URL."};
        if (member == "method") return {"method: string", "The request method."};
        if (member == "headers") return {"headers: Headers", "The request headers."};
        if (member == "signal") return {"signal: AbortSignal", "The request abort signal."};
        if (member == "text") return {"text(): Promise<string>", "Reads the request body as text."};
        if (member == "json") return {"json<T>(): Promise<T>", "Parses the request body as JSON typed as `T`."};
        if (member == "arrayBuffer") return {"arrayBuffer(): Promise<Buffer>", "Reads the request body as raw bytes."};
    }
    if (type == "Response") {
        if (member == "ok") return {"ok: boolean", "Whether the status is 200-299."};
        if (member == "status") return {"status: " + status, "The response status code."};
        if (member == "statusText") return {"statusText: string", "The reason phrase."};
        if (member == "headers") return {"headers: Headers", "The response headers."};
        if (member == "url") return {"url: string", "The response URL."};
        if (member == "text") return {"text(): Promise<string>", "Reads the response body as text."};
        if (member == "json") return {"json<T>(): Promise<T>", "Parses the response body as JSON typed as `T`."};
        if (member == "arrayBuffer") return {"arrayBuffer(): Promise<Buffer>", "Reads the response body as raw bytes."};
    }
    // webserver agents (pasted docs).
    if (type == "HttpRequest") {
        if (member == "signedCookies") return {"signedCookies: Map<string, string>", "Verified signed cookies (`s:` values that pass HMAC verification against the secret from `app.set(\"secret\", ...)` / `cookieParser(secret)`). Tampered cookies are omitted. Empty when no secret is configured."};
        if (member == "baseUrl") return {"baseUrl: string", "The mount prefix chain (e.g. \"/api/v1\"); \"\" when no router has matched yet."};
        if (member == "originalUrl") return {"originalUrl: string", "The full request path as the server received it (no query string)."};
    }
    if (type == "HttpResponse") {
        if (member == "clearCookie") return {"clearCookie(name: string, options?: CookieOptions): HttpResponse", "Appends an expired `Set-Cookie` for `name` (Expires in the past, Max-Age=0, Path=/ by default). Returns the response for chaining."};
        if (member == "set") return {"set(name: string, value: string): HttpResponse | set(headers: object | Map<string, string>): HttpResponse", "Sets one or more response headers, replacing any existing values. Returns the response for chaining."};
        if (member == "append") return {"append(name: string, value: string): HttpResponse", "Appends a header value (comma-joins when the header already exists). Returns the response for chaining."};
        if (member == "get") return {"get(name: string): string", "Returns the response header value, or \"\" when missing."};
        if (member == "type") return {"type(type: string): HttpResponse", "Sets the Content-Type header (a mime string or a short extension such as \"html\"/\"json\"). Returns the response for chaining."};
        if (member == "format") return {"format(formats: { [mediaType: string]: () => void }): void", "Invokes the handler for the negotiated media type (the first entry, or a `default` entry) and sets its Content-Type when unset. Terminal in the sense the chosen handler answers."};
        if (member == "render") return {"render(view: string, locals?: object): void", "Renders a template view with optional locals and sends it as text/html. Terminal."};
        if (member == "download") return {"download(path: string, filename?: string): void", "Sends the file at `path` as an attachment (Content-Disposition). Terminal."};
        if (member == "attach") return {"attach(filename: string): HttpResponse", "Sets Content-Disposition attachment for `filename` without sending the body. Returns the response for chaining."};
        if (member == "links") return {"links(links: { [rel: string]: string }): HttpResponse", "Appends Link header entries from `rel -> href` pairs. Returns the response for chaining."};
        if (member == "location") return {"location(url: string): HttpResponse", "Sets the Location header. Returns the response for chaining."};
        if (member == "vary") return {"vary(field: string): HttpResponse", "Appends a field to the Vary header (duplicates ignored; \"*\" pins it to \"*\"). Returns the response for chaining."};
        if (member == "headersSent") return {"headersSent: boolean", "Whether the response head has been written (a terminal write happened)."};
    }
    if (type == "HttpServer") {
        if (member == "set") return {"set(name: string, value: unknown): HttpServer", "webserver-style settings setter, chainable. `set(\"secret\", ...)` is lowered (the cookie-signing secret); `set(\"case sensitive routing\", bool)` / `set(\"strict routing\", bool)` set routing settings; other keys are accepted and ignored."};
        if (member == "enable" || member == "disable") return {member + "(key: string): HttpServer", "Alias for set(key, true/false), e.g. app.enable(\"strict routing\")."};
        if (member == "enabled" || member == "disabled") return {member + "(key: string): boolean", "Reads an app setting (the routing defaults are false)."};
        if (member == "all") return {"all(path: string, handler: (req: HttpRequest, res: HttpResponse) => void): void", "Registers a handler for every method."};
        if (member == "route") return {"route(path: string): RouteChain", "Returns the chainable route object: `app.route(\"/x\").get(h).post(h)`."};
        if (member == "param") return {"param(name: string, handler: (req: HttpRequest, res: HttpResponse, next: () => Promise<void>, value: string) => void): void", "Runs `handler` before every route whose pattern contains `:name`, with that segment's value."};
    }
    if (type == "Router" || type == "webserverApp") {
        if (member == "get" || member == "post" || member == "put" ||
            member == "delete" || member == "patch" || member == "head" ||
            member == "options" || member == "all") {
            return {member + "(path: string, handler: (req: HttpRequest, res: HttpResponse) => void): void",
                    "Registers a handler on this router for matching requests. Routers are mounted on the server with `app.use(router.handler())` (or at a prefix with `router.mountMiddleware(path)`) and nested with `router.use(path, subRouter)`. `all` matches every method."};
        }
        if (member == "use") return {"use(middleware | Router [, ...]): void",
            "Registers middleware on this router, or mounts a sub-router: `router.use(\"/v1\", subRouter)`."};
        if (member == "param") return {"param(name: string, handler: (req: HttpRequest, res: HttpResponse, next: () => Promise<void>, value: string) => void): void",
            "Runs `handler` before every route whose pattern contains `:name`, with that segment's value."};
        if (member == "route") return {"route(path: string): Route",
            "Returns the chainable Route object for one path."};
        if (member == "handle") return {"handle(req: HttpRequest, res: HttpResponse, next: () => Promise<void>): Promise<void>",
            "Runs this router's stack against one request; calls `next()` when the stack is exhausted without a response."};
        if (member == "handler") return {"handler(): (req: HttpRequest, res: HttpResponse, next: () => Promise<void>) => void",
            "The `app.use(...)` form: mounts this router at \"/\"."};
        if (member == "mountMiddleware") return {"mountMiddleware(path: string): (req: HttpRequest, res: HttpResponse, next: () => Promise<void>) => void",
            "The `app.use(\"/api\", router)` form: matches the mount prefix, strips it, extends `req.baseUrl`, then runs this router."};
        if (member == "listen") return {"listen(port: " + port + ", callback?: () => void): void", "Starts accepting HTTP requests on `port` (webserverApp only)."};
        if (member == "close") return {"close(): void", "Stops accepting new connections (webserverApp only)."};
    }
    if (type == "Route" || type == "RouteChain") {
        return {member + "(handler: (req: HttpRequest, res: HttpResponse) => void): Route",
                "Registers one verb on the owning router for the route's path and returns the same Route for chaining."};
    }
    if (type == "HttpError") {
        if (member == "status" || member == "statusCode") return {member + ": number", "The HTTP status code."};
        if (member == "expose") return {"expose: boolean", "Whether the message is safe to show to the client."};
        if (member == "message") return {"message: string", "The error message."};
        if (member == "code") return {"code: string", "The machine code, e.g. \"ENOENT\"."};
        if (member == "stack") return {"stack: string", "The captured stack (development only)."};
    }
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
    if (prop == "tls") {
        return Type::function({ambient()}, networkObjectType(kTlsOptions));
    }
    if (prop == "lookup") {
        // `dns.lookup(hostname)`: resolves with the first address.
        return Type::function({Type::string()}, promiseOf(Type::string()));
    }
    // Ledger L (appended): namespace-corner spellings.
    if (prop == "static" || prop == "cors" || prop == "bodyLimit") {
        return Type::function({ambient()}, httpMiddleware());
    }
    // webserver agents (pasted).
    if (prop == "cookieParser") {
        return withOptionalTail(Type::function({ambient()}, httpMiddleware()));
    }
    if (prop == "json" || prop == "urlencoded" || prop == "text" || prop == "raw") {
        return Type::function({ambient()}, httpMiddleware());
    }
    if (prop == "Router") {
        return Type::function({ambient()}, networkObjectType(kRouter));
    }
    if (prop == "webserver") {
        return Type::function({ambient()}, networkObjectType(kwebserverApp));
    }
    if (prop == "httpError") {
        return Type::function({Type::number(), Type::string()}, networkObjectType(kHttpError));
    }
    if (prop == "Headers") return Type::function({ambient()}, networkObjectType(kHeaders));
    if (prop == "Request") return Type::function({Type::string(), ambient()}, networkObjectType(kRequest));
    if (prop == "Response") return Type::function({ambient(), ambient()}, networkObjectType(kResponse));
    if (prop == "FormData") return Type::function({}, networkObjectType(kFormData));
    if (prop == "AbortSignal") return networkObjectType(kAbortSignal);
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
    if (id.name == "lookup") {
        // `dns.lookup(hostname)`: resolves with the first address.
        inferArgs(call);
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, anchorSpan(call.span),
                   "dns.lookup(hostname) takes exactly one argument");
        }
        out = promiseOf(Type::string());
        return true;
    }
    // `tls({...})`: the server-TLS options factory. One object literal
    // argument with the documented keys; validated by validateTlsOptionsLiteral.
    if (id.name == "tls") {
        inferArgs(call);
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, anchorSpan(call.span),
                   "tls(options) takes exactly one argument (an object literal: "
                   "{ cert, key, ca?, passphrase?, minProtocol? })");
        } else if (call.arguments.front()->kind == NodeKind::ObjectLiteral) {
            validateTlsOptionsLiteral(
                static_cast<const ObjectLiteral&>(*call.arguments.front()),
                call.span);
        } else {
            report(diagcode::kTypeMismatch, anchorSpan(call.arguments.front()->span),
                   "tls(options) takes an object literal argument: "
                   "{ cert, key, ca?, passphrase?, minProtocol? }");
        }
        out = networkObjectType(kTlsOptions);
        return true;
    }
    // Ledger L (appended): middleware factories + bare class constructions.
    // `fetch(Request)` is already typed by the `fetch` arm above.
    if (id.name == "static" || id.name == "cors" || id.name == "bodyLimit") {
        inferArgs(call);
        out = httpMiddleware();
        return true;
    }
    // webserver agents (pasted).
    if (id.name == "cookieParser") {
        inferArgs(call);
        if (call.arguments.size() > 1) {
            report(diagcode::kArityMismatch, anchorSpan(call.span),
                   "cookieParser(secret?) takes at most one argument");
        }
        out = httpMiddleware();
        return true;
    }
    if (id.name == "json" || id.name == "urlencoded" || id.name == "text" || id.name == "raw") {
        inferArgs(call);
        out = httpMiddleware();
        return true;
    }
    if (id.name == "Router" || id.name == "webserver") {
        inferArgs(call);
        out = networkObjectType(id.name == "Router" ? kRouter : kwebserverApp);
        return true;
    }
    if (id.name == "httpError") {
        inferArgs(call);
        if (call.arguments.size() != 2) {
            report(diagcode::kArityMismatch, anchorSpan(call.span),
                   "httpError(status, message) takes two arguments");
        }
        out = networkObjectType(kHttpError);
        return true;
    }
    if (id.name == "Headers" || id.name == "FormData") {
        inferArgs(call);
        out = networkObjectType(id.name == "Headers" ? kHeaders : kFormData);
        return true;
    }
    if (id.name == "Request" || id.name == "Response") {
        inferArgs(call);
        out = networkObjectType(id.name == "Request" ? kRequest : kResponse);
        return true;
    }
    if (id.name == "AbortSignal") {
        inferArgs(call);
        out = networkObjectType(kAbortSignal);
        return true;
    }
    return false;
}

/// Validates the keys + value shapes of a `tls({...})` options literal:
/// `cert`/`key`/`ca`/`passphrase`/`minProtocol` are strings; anything else is
/// an unknown key (with the supported set named). Values typed `unknown` are
/// passed through; garbage literals are caught here, computed values at the
/// runtime boundary.
void Analyzer::validateTlsOptionsLiteral(const ObjectLiteral& options, const SourceSpan& span) {
    static const char* const kTlsKeys[] = {"cert", "key", "ca", "passphrase",
                                           "minProtocol"};
    for (const auto& propPtr : options.properties) {
        const ObjectProperty* prop = propPtr.get();
        if (prop == nullptr || prop->key == nullptr) {
            report(diagcode::kTypeMismatch, anchorSpan(span),
                   "tls() options only support literal keys "
                   "(cert, key, ca, passphrase, minProtocol)");
            continue;
        }
        const std::string key = keyName(prop->key.get());
        bool known = false;
        for (const char* k : kTlsKeys) {
            if (key == k) { known = true; break; }
        }
        if (!known) {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "unknown tls() option '" + key +
                       "' (supported: cert, key, ca, passphrase, minProtocol)");
            continue;
        }
        const Expr* value = prop->value.get();
        if (value == nullptr) continue;
        const Type valueType = typeOfExpr(const_cast<Expr&>(*value));
        if (valueType.kind == TypeKind::Number || valueType.kind == TypeKind::Boolean ||
            valueType.kind == TypeKind::Class || valueType.kind == TypeKind::Function) {
            report(diagcode::kTypeMismatch, anchorSpan(value->span),
                   "tls() '" + key + "' must be a string");
        }
        if (key == "minProtocol" && value->kind == NodeKind::StringLiteral) {
            const std::string spelling =
                static_cast<const StringLiteral&>(*value).value;
            if (spelling != "TLSv1.2" && spelling != "1.2" &&
                spelling != "TLSv1.3" && spelling != "1.3") {
                report(diagcode::kTypeMismatch, anchorSpan(value->span),
                       "tls() minProtocol must be \"TLSv1.2\" or \"TLSv1.3\" (got \"" +
                           spelling + "\")");
            }
        }
    }
}

/// D79: `createServer(options?)` — merged config wiring (config/config-static/
/// config-apps union). One object literal with optional keys:
/// threads/tls/cors/static/bodyLimit/trustProxy/views/onError (the case
/// spelling) plus config/ maxConnections/idleTimeout/maxWriteBuffer/timeout
/// and config-static json/urlencoded/timeouts for forward compat. Literal
/// thread counts are checked here (0, negative, fractional and non-number
/// literals become CP3401); a computed value is validated by the runtime
/// constructor, and the literal 1 keeps the single-loop path.
bool Analyzer::checkCreateServerOptions(CallExpr& call) {
    // Type the `onError` arrow with its request/response handler context
    // before the options object is inferred (its params need the typed
    // req/res for the member calls in the body, exactly like app.onError).
    if (!call.arguments.empty() && call.arguments.front() &&
        call.arguments.front()->kind == NodeKind::ObjectLiteral) {
        const auto& optLiteral =
            static_cast<const ObjectLiteral&>(*call.arguments.front());
        for (const auto& propPtr : optLiteral.properties) {
            const ObjectProperty* prop = propPtr.get();
            if (prop == nullptr || prop->value == nullptr) continue;
            const Expr& value = *prop->value;
            if (value.kind != NodeKind::ArrowFunction) continue;
            if (prop->key != nullptr &&
                keyName(prop->key.get()) == "onError" && value.nodeId != 0) {
                contextualArrowTypes_[value.nodeId] = httpErrorHandler();
            }
        }
    }
    inferArgs(call);
    if (call.arguments.empty()) return false;
    if (call.arguments.size() > 1 || !call.arguments.front()) {
        report(diagcode::kTypeMismatch, anchorSpan(call.span),
               "createServer() takes at most one argument (an options object: "
               "{ threads?, tls?, cors?, static?, bodyLimit?, trustProxy?, "
               "views?, onError? })");
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
    // The merged v0.2 config surface (config/ + config-static/ +
    // config-apps, one entry point). Keys are validated by shape here;
    // codegen lowers them into `CreateServerConfig` (tls/cors/static/
    // bodyLimit/trustProxy/views/onError) — the reserved limit keys
    // (maxConnections/idleTimeout/maxWriteBuffer/timeout/json/urlencoded/
    // timeouts) are accepted and currently inert.
    static const char* const kConfigKeys[] = {
        "threads", "tls", "cors", "static", "bodyLimit", "trustProxy", "views",
        "onError", "maxConnections", "idleTimeout", "maxWriteBuffer", "timeout",
        "json", "urlencoded", "timeouts"};
    auto isKnownKey = [&](const std::string& k) {
        for (const char* known : kConfigKeys) {
            if (k == known) return true;
        }
        return false;
    };
    for (const auto& propPtr : literal.properties) {
        const ObjectProperty* prop = propPtr.get();
        if (prop == nullptr) continue;
        if (prop->computed || prop->key == nullptr) {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "createServer() options only support literal keys");
            continue;
        }
        const std::string key = keyName(prop->key.get());
        if (!isKnownKey(key)) {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "unknown createServer() option '" + key +
                       "' (supported: threads, tls, cors, static, bodyLimit, "
                       "trustProxy, views, onError)");
            continue;
        }
        if (prop->value == nullptr) continue;
        if (key != "threads") {
            // Type-shape checks for the non-threads keys (loose on purpose:
            // computed values are runtime-checked by the factory).
            const Expr& value = *prop->value;
            const Type valueType = typeOfExpr(const_cast<Expr&>(value));
            auto badShape = [&](const char* what) {
                report(diagcode::kTypeMismatch, anchorSpan(value.span),
                       std::string("createServer() '") + key + "' must be " + what);
            };
            if (key == "tls") {
                const bool isFactoryValue =
                    valueType.kind == TypeKind::Object && valueType.name == kTlsOptions;
                const bool isLiteral = value.kind == NodeKind::ObjectLiteral;
                if (!isLiteral && !isFactoryValue &&
                    valueType.kind != TypeKind::Boolean &&
                    valueType.kind != TypeKind::Unknown &&
                    valueType.kind != TypeKind::Error) {
                    badShape("a boolean or a tls({...}) object");
                }
                if (isLiteral) {
                    validateTlsOptionsLiteral(
                        static_cast<const ObjectLiteral&>(value), value.span);
                }
            } else if (key == "trustProxy") {
                if (valueType.kind == TypeKind::String ||
                    valueType.kind == TypeKind::Class || valueType.kind == TypeKind::Object) {
                    badShape("a boolean");
                }
            } else if (key == "static" || key == "views") {
                if (valueType.kind == TypeKind::Number ||
                    valueType.kind == TypeKind::Boolean) {
                    badShape("a string or an object");
                }
            } else if (key == "bodyLimit" || key == "maxConnections" ||
                       key == "idleTimeout" || key == "maxWriteBuffer" ||
                       key == "timeout") {
                if (valueType.kind == TypeKind::Boolean ||
                    valueType.kind == TypeKind::Class) {
                    badShape("a number of bytes");
                }
            } else if (key == "cors" || key == "json" || key == "urlencoded" ||
                       key == "timeouts") {
                if (value.kind != NodeKind::ObjectLiteral &&
                    valueType.kind != TypeKind::Unknown &&
                    valueType.kind != TypeKind::Error &&
                    valueType.kind != TypeKind::Object &&
                    valueType.kind != TypeKind::Class) {
                    badShape("an object");
                }
            } else if (key == "onError") {
                if (valueType.kind != TypeKind::Function &&
                    valueType.kind != TypeKind::Unknown &&
                    valueType.kind != TypeKind::Error &&
                    valueType.kind != TypeKind::Class) {
                    badShape("a handler function");
                }
            }
            continue;
        }
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
    // Router agent [S6]: `new Router()` / `new Router(options?)`.
    if (name == "Router") {
        for (auto& arg : args) { if (arg) inferExpr(*arg); }
        return networkObjectType(kRouter);
    }
    if (name == "webserver") {
        for (auto& arg : args) { if (arg) inferExpr(*arg); }
        return networkObjectType(kwebserverApp);
    }
    // Ledger L (appended): `new Headers()/Request()/Response()/FormData()/AbortSignal()`.
    if (name == "Headers" || name == "Request" || name == "Response" ||
        name == "FormData" || name == "AbortSignal") {
        for (auto& arg : args) { if (arg) inferExpr(*arg); }
        if (name == "Request" && args.empty()) {
            report(diagcode::kArityMismatch, anchorSpan(span), "new Request(url, init?) needs a URL");
        }
        if (name == "Headers") return networkObjectType(kHeaders);
        if (name == "Request") return networkObjectType(kRequest);
        if (name == "Response") return networkObjectType(kResponse);
        if (name == "FormData") return networkObjectType(kFormData);
        return networkObjectType(kAbortSignal);
    }
    if (name != "WebSocket" && name != "WebSocketServer") return Type::unknown();
    for (auto& arg : args) {
        if (arg) inferExpr(*arg);
    }
    // `new WebSocket(url)` and `new WebSocketServer({ port, path?, tls? })`
    // (docs 04 §network): both take exactly one argument. A call without it
    // is a program error here (the runtime has no URL / no port to bind), not
    // a codegen skip.
    if (args.size() != 1) {
        report(diagcode::kArityMismatch, anchorSpan(span),
               name == "WebSocket" ? "new WebSocket(url) takes one argument"
                                   : "new WebSocketServer(options) takes one "
                                     "argument");
    } else if (name == "WebSocketServer" && args.front() &&
               args.front()->kind == NodeKind::ObjectLiteral) {
        const auto& lit = static_cast<const ObjectLiteral&>(*args.front());
        for (const auto& propPtr : lit.properties) {
            const ObjectProperty* prop = propPtr.get();
            if (prop == nullptr || prop->key == nullptr) continue;
            const std::string key = keyName(prop->key.get());
            if (key == "port") continue;  // runtime-validated number
            if (key == "path") continue;  // path check happens at listen time
            if (key == "tls" && prop->value) {
                const Expr& value = *prop->value;
                const Type valueType = typeOfExpr(const_cast<Expr&>(value));
                const bool isFactoryValue =
                    valueType.kind == TypeKind::Object && valueType.name == kTlsOptions;
                if (value.kind == NodeKind::ObjectLiteral) {
                    validateTlsOptionsLiteral(
                        static_cast<const ObjectLiteral&>(value), value.span);
                } else if (value.kind == NodeKind::BoolLiteral ||
                           valueType.kind == TypeKind::Boolean || isFactoryValue ||
                           valueType.kind == TypeKind::Unknown ||
                           valueType.kind == TypeKind::Error) {
                    // boolean or a tls({...}) value: OK.
                } else {
                    report(diagcode::kTypeMismatch, anchorSpan(value.span),
                           "new WebSocketServer({ tls }) must be a boolean or a "
                           "tls({...}) object");
                }
                continue;
            }
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "unknown new WebSocketServer() option '" + key +
                       "' (supported: port, path, tls)");
        }
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
    // `dns.lookup(hostname)`: resolves with the first address.
    if (m.property == "lookup" && m.object &&
        m.object->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*m.object);
        Binding binding;
        if (id.name == "dns" && lookup(id.name, binding) && binding.isAmbient) {
            inferArgs(call);
            if (call.arguments.size() != 1 || !call.arguments.front()) {
                report(diagcode::kArityMismatch, anchorSpan(call.span),
                       "dns.lookup(hostname) takes exactly one argument");
            }
            out = promiseOf(Type::string());
            return true;
        }
    }
    // Ledger L (appended): `AbortSignal.timeout(ms)`.
    if (m.property == "timeout" && m.object &&
        m.object->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*m.object);
        Binding binding;
        if (id.name == "AbortSignal" && lookup(id.name, binding) && binding.isAmbient) {
            inferArgs(call);
            if (call.arguments.size() != 1 || !call.arguments.front()) {
                report(diagcode::kArityMismatch, anchorSpan(call.span),
                       "AbortSignal.timeout(ms) takes exactly one argument");
            }
            out = networkObjectType(kAbortSignal);
            return true;
        }
    }
    // Body-parse agent: `webserver.json()` etc namespace spelling.
    if ((m.property == "json" || m.property == "urlencoded" ||
         m.property == "text" || m.property == "raw") &&
        m.object && m.object->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*m.object);
        Binding binding;
        if (id.name == "webserver" && lookup(id.name, binding) && binding.isAmbient) {
            inferArgs(call);
            out = httpMiddleware();
            return true;
        }
    }
    // Regex-routing agent: (path, options, handler) 3-arg verb form + the
    // 1-arg settings reads (app.get("strict routing")).
    {
        const bool isVerb = m.property == "get" || m.property == "post" ||
                            m.property == "put" || m.property == "delete" ||
                            m.property == "patch" || m.property == "head" ||
                            m.property == "options" || m.property == "any" ||
                            m.property == "all";
        const Type recv = m.object ? typeOfExpr(*m.object) : Type::unknown();
        const bool isServer = recv.kind == TypeKind::Object && recv.name == kHttpServer;
        if (isVerb && isServer && call.arguments.size() == 3) {
            inferArgs(call);
            FuncSig sig;
            sig.name = m.property;
            sig.params = {Type::unknown(), Type::unknown(), httpRouteHandler()};
            sig.ret = Type::voidType();
            checkArgs(sig, call);
            if (auto* opts = call.arguments[1].get();
                opts && opts->kind == NodeKind::ObjectLiteral) {
                for (auto& prop : static_cast<const ObjectLiteral&>(*opts).properties) {
                    if (!prop || !prop->value) continue;
                    const std::string key = keyName(prop->key.get());
                    if (key != "caseSensitive" && key != "strict") {
                        report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                               "unknown route option '" + key +
                                   "' (only 'caseSensitive' and 'strict' are supported)");
                    }
                }
            }
            out = Type::voidType();
            return true;
        }
        if (isServer && m.property == "get" && call.arguments.size() == 1) {
            inferArgs(call);
            out = Type::boolean();
            return true;
        }
    }
    if (m.property != "json" || m.object == nullptr) return false;
    const Type recv = typeOfExpr(*m.object);
    if (recv.kind != TypeKind::Object) return false;
    if (recv.name != kFetchResponse && recv.name != kHttpRequest &&
        recv.name != kRequest && recv.name != kResponse) return false;
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
