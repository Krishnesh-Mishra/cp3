// =============================================================================
// src/std/network/webserver/routing/sema_routing.cpp - webserver complex routing
// typing (sema fragment).
//
// OWNERSHIP: the std/network family, the `webserver/routing` sub-family
// (src/std/network/webserver/routing/** ONLY). This file is a wirable fragment,
// NOT a standalone TU: the Analyzer/Field/Type vocabulary lives in
// detail/analyzer.h and the existing network tables in
// src/std/network/codegen/sema.cpp. The lead's splice lines (WIRING.md) paste
// the marker constants + field rows below into src/std/network/codegen/sema.cpp
// next to the existing HttpServer table, and extend Analyzer::networkObjectType
// / member dispatch. The functions here are written against that vocabulary so
// the splice is mechanical.
//
// webserver shapes typed here:
//   * HttpServer/Router/webserverApp : route, all, param + the existing verbs
//   * RouteChain                   : every verb returns RouteChain (chaining)
//   * Router() / webserver()         : fresh $network:Router / $network:webserverApp
//   * handler arrays               : (string, handler | handler[]) and the
//                                    variadic tail (string, ...handler[])
// =============================================================================
#include "detail/analyzer.h"

#include <string>
#include <vector>

namespace cp3 {

namespace network_webserver_routing {

// Marker names shared with codegen_routing.cpp (same spelling convention as
// src/std/network/codegen/sema.cpp: a `$network:` prefix + class name).
constexpr const char* kRouter = "$network:Router";
constexpr const char* kRouteChain = "$network:RouteChain";
constexpr const char* kwebserverApp = "$network:webserverApp";

}  // namespace network_webserver_routing

}  // namespace cp3

// -----------------------------------------------------------------------------
// SPLICE LINES (applied in src/std/network/codegen/sema.cpp, see WIRING.md):
//
// 1. At the top of the anonymous namespace, next to kHttpServer:
//      constexpr const char* kRouter = "$network:Router";
//      constexpr const char* kRouteChain = "$network:RouteChain";
//      constexpr const char* kwebserverApp = "$network:webserverApp";
//
// 2. httpServerFields(): append after the "any" verb row:
//      fields.push_back(Field{"all", Type::function({Type::string(), route}, Type::voidType()), false});
//      fields.push_back(Field{"route", Type::function({Type::string()}, networkValue(kRouteChain)), false});
//      fields.push_back(Field{"param", Type::function({Type::string(), httpParamHandler()}, Type::voidType()), false});
//    where httpParamHandler() is:
//      Type httpParamHandler() {
//          Type next = Type::function({}, promiseOf(Type::voidType()));
//          next.name = "$network:Next";
//          return Type::function({networkValue(kHttpRequest), networkValue(kHttpResponse),
//                                 next, Type::string()}, Type::voidType());
//      }
//
// 3. New field tables + networkFieldsFor() rows for kRouter / kRouteChain /
//    kwebserverApp (see routerFields()/routeChainFields() below; the webserverApp
//    table is the Router table plus listen/close/onError).
//
// 4. networkMember(): treat `Router`/`webserver` like `createServer`:
//      if (prop == "Router")  return Type::function({ambient()}, networkObjectType(kRouter));
//      if (prop == "webserver") return Type::function({ambient()}, networkObjectType(kwebserverApp));
//    (options object { mergeParams?, strict? } is ambient until it grows a
//    shape; the flags are read at codegen from the object literal.)
//
// 5. tryInferNetworkCall(): infer `Router()` / `webserver()` calls and
//    `router.route(...)` / `app.route(...)` results. route() returns
//    networkObjectType(kRouteChain); Router()/webserver() return the markers
//    above.
// -----------------------------------------------------------------------------

namespace cp3 {

namespace network_webserver_routing {

/// `(req, res, next, value: string) => void`: the app.param callback type.
/// The string param carries the captured value; the callable marker name is
/// irrelevant (codegen re-types the arrow params from the routing context).
Type paramHandler() {
    Type next = Type::function({}, promiseOf(Type::voidType()));
    next.name = "$network:Next";
    Type req = Type(TypeKind::Object, "$network:HttpRequest");
    Type res = Type(TypeKind::Object, "$network:HttpResponse");
    return Type::function({req, res, next, Type::string()}, Type::voidType());
}

/// `(string, handler | handler[])` plus the variadic tail: an array literal
/// of handlers is one handler-list argument, extra handler arguments extend
/// the list (codegen collects them; see codegen_routing.cpp routingHandlers).
Type routeHandlerOrList() {
    Type route = Type::function(
        {Type(TypeKind::Object, "$network:HttpRequest"),
         Type(TypeKind::Object, "$network:HttpResponse")},
        Type::voidType());
    Type array = Type(TypeKind::Array, "Array");
    array.parts.push_back(route);
    return Type::unionOf({route, array});
}

/// The verb rows of the Router table: same verbs as the HttpServer table,
/// with the array/variadic handler type and the new members.
std::vector<Field> routerFields() {
    std::vector<Field> fields;
    const Type handlerList = routeHandlerOrList();
    for (const char* verb :
         {"get", "post", "put", "delete", "patch", "head", "options", "all"}) {
        fields.push_back(Field{verb, Type::function({Type::string(), handlerList}, Type::voidType()),
                               false});
    }
    Type next = Type::function({}, promiseOf(Type::voidType()));
    next.name = "$network:Next";
    Type req = Type(TypeKind::Object, "$network:HttpRequest");
    Type res = Type(TypeKind::Object, "$network:HttpResponse");
    Type middleware = Type::function({req, res, next}, Type::voidType());
    // `use(mw)` and `use(mountPath, router)` share one member name.
    fields.push_back(Field{"use", Type::function({middleware}, Type::voidType()), false});
    Type router = Type(TypeKind::Object, kRouter);
    fields.push_back(
        Field{"use", Type::function({Type::string(), router}, Type::voidType()), false});
    fields.push_back(
        Field{"param", Type::function({Type::string(), paramHandler()}, Type::voidType()), false});
    fields.push_back(
        Field{"route", Type::function({Type::string()}, Type(TypeKind::Object, kRouteChain)), false});
    return fields;
}

/// The RouteChain table: every verb returns RouteChain so
/// `app.route('/x').get(h).post(h)` type-checks as a chain of RouteChain.
std::vector<Field> routeChainFields() {
    std::vector<Field> fields;
    const Type handlerList = routeHandlerOrList();
    const Type chain = Type(TypeKind::Object, kRouteChain);
    for (const char* verb :
         {"get", "post", "put", "delete", "patch", "head", "options", "all"}) {
        fields.push_back(
            Field{verb, Type::function({handlerList}, chain), false});
    }
    return fields;
}

}  // namespace network_webserver_routing

}  // namespace cp3
