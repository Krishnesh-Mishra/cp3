// =============================================================================
// src/std/network/webserver/router/sema_router.cpp - `Router` surface typing.
//
// OWNERSHIP: the std/network family
// (src/std/network/webserver/router/**). SPLICE FRAGMENT: this file is code
// to merge into src/std/network/codegen/sema.cpp (the `std/network` sema TU),
// at the marked points below. It is NOT compiled on its own (the sema TU
// is listed explicitly in src/sema/CMakeLists.txt, so this file is inert
// until the lead splices it). The fragment assumes the destination's
// helpers (`networkValue`, `httpRouteHandler`, `httpMiddleware`,
// `stringMap`, `ambient`, `withOptionalTail*`, `stdIntegerType`, the
// `Analyzer` hooks) exactly as they exist there.
//
// WIRING (lead applies in src/std/network/codegen/sema.cpp):
//   [S1] marker consts, after kAbortSignal.
//   [S2] routerFields / routeFields definitions, beside httpServerFields
//        (they reuse httpRouteHandler/httpMiddleware).
//   [S3] httpRequestFields += baseUrl/originalUrl rows.
//   [S4] networkFieldsFor += $network:Router / $network:Route arms.
//   [S5] networkObjectMemberDoc += Router/Route/HttpRequest arms.
//   [S6] inferNetworkConstruct += the `Router` arm (new Router()).
// Plus the import-surface rows (shared files, lead applies):
//   src/modules/src/support/std_registry.cpp: "Router" in the
//     {"std/network", true, {...}} named list.
//   src/sema/src/checks/type/std_check.cpp: {"Router", "std/network"} in
//     kNamedHints (so a bare `Router` without the import errors with the
//     repair line, like the other named imports).
// =============================================================================

namespace cp3 {
namespace {

// [S1] Marker consts (after `const char* const kAbortSignal = ...`):
// Ledger R (Router, appended): webserver `Router` + its `route()` object.
const char* const kRouter = "$network:Router";
const char* const kRoute = "$network:Route";

// [S2] Field tables (after httpServerFields):
// Ledger R (Router, appended): the documented `Router` member set. `use`
// is overloaded in webserver (middleware / sub-router); the permissive
// `unknown` arm types the Router argument, and codegen
// (codegen_router.cpp) distinguishes the two by the argument's type.
// `del` is the cp3 `router.delete(...)` spelling (codegen maps the cp3
// `delete` onto it, exactly like HttpServer).
// `param`'s handler is `(req, res, next, id) => void`.
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
    // The app-mount surface (also reachable through codegen's lowering of
    // `app.use("/api", router)`): the returned middleware is the server's
    // own 3-argument shape.
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

// [S3] In httpRequestFields(), append (before the "text" row):
// Ledger R (Router, appended): the mount-path reads; the runtime side
// table behind them is runtime_router.cpp's requestBaseUrl/requestOriginalUrl.
        Field{"baseUrl", Type::string(), false},
        Field{"originalUrl", Type::string(), false},

// [S4] In networkFieldsFor(), before the final `return {};`:
    if (name == kRouter) return routerFields(ts);
    if (name == kRoute) return routeFields();

// [S6] In Analyzer::inferNetworkConstruct(), at the top (before the
// WebSocket arm). `new Router()` and `new Router(options?)`: the options
// object is permissive (the runtime reads its caseSensitive/strict/
// mergeParams flags; see the RouterOptions struct in router.hpp).
    if (name == "Router") {
        for (auto& arg : args) { if (arg) inferExpr(*arg); }
        return networkObjectType(kRouter);
    }

// [S7] In httpServerFields(): the app-mount spelling `app.use(router)`
// and `app.use("/api", router)`. codegen (codegen_router.cpp [C9]) keys
// off the *argument*'s Router type, so the checker's only job is arity:
// add beside the existing `use` row:
    fields.push_back(Field{"use",
                           Type::function({networkValue(kRouter)},
                                          Type::voidType()), false});
    fields.push_back(Field{"use",
                           Type::function({Type::string(),
                                           networkValue(kRouter)},
                                          Type::voidType()), false});

}  // namespace

// [S5] In networkObjectMemberDoc(), add (before the final `return {};`):
// Ledger R (Router, appended) docs.
    if (type == "Router") {
        if (member == "get" || member == "post" || member == "put" ||
            member == "delete" || member == "patch" || member == "head" ||
            member == "options" || member == "all") {
            return {member + "(path: string, handler: (req: HttpRequest, res: HttpResponse) => void): void",
                    "Registers a handler on this router for matching requests. Routers are mounted on the server with `app.use(router.handler())` (or at a prefix with `router.mountMiddleware(path)`) and nested with `router.use(path, subRouter)`. `all` matches every method.\n\n**Example**\n\n```cp3\nconst users = new Router();\nusers.get(\"/:id\", (req, res) => {\n  res.json({ id: req.params.get(\"id\") });\n});\n```"};
        }
        if (member == "use") return {"use(middleware | Router [, ...]): void",
            "Registers middleware on this router, or mounts a sub-router: `router.use(\"/v1\", subRouter)`. Prefix middleware sees `req.path` stripped of the prefix; `req.baseUrl` carries the mount chain.\n\n**Example**\n\n```cp3\nconst api = new Router();\napi.use(\"/v1\", users);\n```"};
        if (member == "param") return {"param(name: string, handler: (req: HttpRequest, res: HttpResponse, next: () => Promise<void>, value: string) => void): void",
            "Runs `handler` before every route whose pattern contains `:name`, with that segment's value. Call `next()` to continue to the route handler.\n\n**Example**\n\n```cp3\nrouter.param(\"id\", (req, res, next, id) => {\n  next();\n});\n```"};
        if (member == "route") return {"route(path: string): Route",
            "Returns the chainable Route object for one path: `router.route(\"/x\").get(h).post(h)` registers each verb on the owning router.\n\n**Example**\n\n```cp3\nrouter.route(\"/x\").get((req, res) => { res.text(\"ok\"); });\n```"};
        if (member == "handle") return {"handle(req: HttpRequest, res: HttpResponse, next: () => Promise<void>): Promise<void>",
            "Runs this router's stack against one request; calls `next()` when the stack is exhausted without a response (the `router.handle` surface webserver exposes)."};
        if (member == "handler") return {"handler(): (req: HttpRequest, res: HttpResponse, next: () => Promise<void>) => void",
            "The `app.use(...)` form: mounts this router at \"/\" (`app.use(router.handler())`)."};
        if (member == "mountMiddleware") return {"mountMiddleware(path: string): (req: HttpRequest, res: HttpResponse, next: () => Promise<void>) => void",
            "The `app.use(\"/api\", router)` form: matches the mount prefix, strips it, extends `req.baseUrl`, then runs this router (`app.use(router.mountMiddleware(\"/api\"))`)."};
    }
    if (type == "Route") {
        return {member + "(handler: (req: HttpRequest, res: HttpResponse) => void): Route",
                "Registers one verb on the owning router for the route's path and returns the same Route for chaining."};
    }
    if (type == "HttpRequest") {
        if (member == "baseUrl") return {"baseUrl: string", "The mount prefix chain (e.g. \"/api/v1\"); \"\" when no router has matched yet."};
        if (member == "originalUrl") return {"originalUrl: string", "The full request path as the server received it (no query string)."};
    }
    return {};

}  // namespace cp3
