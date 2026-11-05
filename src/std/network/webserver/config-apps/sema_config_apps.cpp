// =============================================================================
// src/std/network/webserver/config-apps/sema_config_apps.cpp - createServer
// config typing (sema fragment).
//
// OWNERSHIP: src/std/network/webserver/config-apps/** only. Like the routing
// family's sema fragment, this is a wirable fragment, NOT a standalone TU:
// the Analyzer/Field/Type vocabulary lives in detail/analyzer.h and the
// network tables in src/std/network/codegen/sema.cpp. Splice lines are at
// the bottom (see README.md for the full wiring list).
//
// What is typed here:
//   * `createServer({...})` with a config object -> a NEW marker type
//     `$webserver:ConfiguredApp` (not `$network:HttpServer`): the config
//     form owns settings/views/locals members the HttpServer table lacks.
//     `createServer()` / `createServer({ threads })` keep the existing
//     `$network:HttpServer` result (D79 path untouched).
//   * `$webserver:ConfiguredApp` fields: the HttpServer verbs + listen /
//     close / onError / route / param, PLUS the settings family's
//     enable/disable/enabled/set/get/engine/locals/settings/render rows
//     (same types as settings/sema.cpp's webserverApp table) and the
//     middleware family's use rows (3-arg normal, 4-arg error, mount forms).
//   * The config object itself (validation rules for the integrator's
//     checkCreateServerConfigOptions, modelled on checkCreateServerOptions):
//       top level : views? middleware? settings? locals? onError?
//       views     : dir?: string, engine?: string, engineFn?: view engine fn
//       settings  : etag?: bool|string, xPoweredBy?: bool,
//                   jsonSpaces?: number,
//                   trustProxy?: bool|number|string|string[]
//       locals    : open object of scalars / string lists (SettingValue)
//       middleware: array of 3-arg (normal) or 4-arg (error) handlers
//       onError   : 3-arg (err,req,res) or 4-arg (err,req,res,next) handler
//     Unknown keys are type errors (CP3401-style diagnostics at the
//     property span); wrong value shapes are type errors at the value span.
//     Literal etag/xPoweredBy/jsonSpaces/trustProxy values are checked
//     literally where cheap; computed values are checked by the runtime
//     (applyServerConfig) exactly like `threads` today.
// =============================================================================
#include "detail/analyzer.h"

#include <string>
#include <vector>

namespace cp3 {

namespace network_webserver_configapps {

/// Marker for the createServer-config result. Shared with
/// codegen_config_apps.cpp (same spelling convention as the network tables:
/// a `$webserver:` prefix + class name).
constexpr const char* kConfiguredApp = "$webserver:ConfiguredApp";
constexpr const char* kwebserverLocals = "$webserver:Locals";

/// The config `settings` key -> webserver setting name table (mirrors
/// configSettingNameForKey in the runtime header; "" = unsupported key).
const char* configSettingNameForKey(const std::string& key) {
    if (key == "etag") return "etag";
    if (key == "xPoweredBy") return "x-powered-by";
    if (key == "jsonSpaces") return "json spaces";
    if (key == "trustProxy") return "trust proxy";
    return "";
}

bool isConfigTopLevelKey(const std::string& key) {
    return key == "views" || key == "middleware" || key == "settings" ||
           key == "locals" || key == "onError";
}

bool isViewsKey(const std::string& key) {
    return key == "dir" || key == "engine" || key == "engineFn";
}

/// `(req, res, next) => void`: normal config middleware (middleware family).
Type configMiddlewareHandler() {
    Type next = Type::function({}, promiseOf(Type::voidType()));
    next.name = "$network:Next";
    Type req = Type(TypeKind::Object, "$network:HttpRequest");
    Type res = Type(TypeKind::Object, "$network:HttpResponse");
    return Type::function({req, res, next}, Type::voidType());
}

/// `(err, req, res, next) => void`: config error middleware / onError.
Type configErrorHandler() {
    Type next = Type::function({}, promiseOf(Type::voidType()));
    next.name = "$network:Next";
    Type req = Type(TypeKind::Object, "$network:HttpRequest");
    Type res = Type(TypeKind::Object, "$network:HttpResponse");
    return Type::function(
        {ambient(), req, res, next}, Type::voidType());
}

/// `(file, options) => string`: the views.engineFn shape (settings family).
Type viewEngineHandler() {
    return Type::function({Type::string(), ambient()}, Type::string());
}

/// `(string, (req, res) => void)`: one HttpServer verb row. Handler params
/// are re-typed by codegen from the routing context (as on HttpServer).
Type configRouteHandler() {
    Type req = Type(TypeKind::Object, "$network:HttpRequest");
    Type res = Type(TypeKind::Object, "$network:HttpResponse");
    return Type::function({req, res}, Type::voidType());
}

/// The `$webserver:ConfiguredApp` member table: HttpServer verbs + config
/// (settings/views) members + middleware `use` rows + serving members.
std::vector<Field> configuredAppFields() {
    const Type app = Type(TypeKind::Object, kConfiguredApp);
    std::vector<Field> fields;
    const Type route = configRouteHandler();
    for (const char* verb : {"get", "post", "put", "delete", "patch", "head",
                             "options", "any", "all"}) {
        fields.push_back(Field{verb,
                               Type::function({Type::string(), route},
                                              Type::voidType()),
                               false});
    }
    // Middleware family rows: use(mw) | use(mount, mw) for both arities.
    fields.push_back(Field{"use",
                           Type::function({configMiddlewareHandler()},
                                          Type::voidType()),
                           false});
    fields.push_back(Field{"use",
                           Type::function({Type::string(),
                                           configMiddlewareHandler()},
                                          Type::voidType()),
                           false});
    fields.push_back(Field{"use",
                           Type::function({configErrorHandler()},
                                          Type::voidType()),
                           false});
    fields.push_back(Field{"use",
                           Type::function({Type::string(),
                                           configErrorHandler()},
                                          Type::voidType()),
                           false});
    // Ledger-L error hook (3-arg; the 4-arg form arrives via use()).
    fields.push_back(Field{"onError",
                           Type::function({configErrorHandler()},
                                          Type::voidType()),
                           false});
    // Routing rows.
    fields.push_back(Field{"route",
                           Type::function({Type::string()},
                                          Type(TypeKind::Object,
                                               "$network:RouteChain")),
                           false});
    fields.push_back(Field{"param",
                           Type::function({Type::string(), ambient()},
                                          Type::voidType()),
                           false});
    // Settings/views rows (settings family table, copied verbatim types).
    fields.push_back(Field{"enable",
                           Type::function({Type::string()}, app), false});
    fields.push_back(Field{"disable",
                           Type::function({Type::string()}, app), false});
    fields.push_back(Field{"enabled",
                           Type::function({Type::string()}, Type::boolean()),
                           false});
    fields.push_back(Field{"get",
                           Type::function({Type::string()}, ambient()), false});
    fields.push_back(Field{"set",
                           Type::function({Type::string(), ambient()}, app),
                           false});
    fields.push_back(Field{"engine",
                           Type::function({Type::string(), viewEngineHandler()},
                                          app),
                           false});
    fields.push_back(Field{"locals", Type(TypeKind::Object, kwebserverLocals),
                           false});
    fields.push_back(Field{"settings", ambient(), false});
    fields.push_back(Field{"render",
                           withOptionalTail(Type::function(
                               {Type::string(), ambient()}, Type::string())),
                           false});
    // Serving rows.
    fields.push_back(Field{"listen",
                           withOptionalTail(Type::function(
                               {Type::number(), ambient()}, Type::voidType())),
                           false});
    fields.push_back(Field{"close", Type::function({}, Type::voidType()),
                           false});
    return fields;
}

}  // namespace network_webserver_configapps

}  // namespace cp3

// -----------------------------------------------------------------------------
// SPLICE LINES (integrator; see README.md):
//
// 1. Analyzer declarations (src/sema/src/detail/analyzer.h, next to the
//    webserver declarations from settings/WIRING.md):
//      Type webserverConfigType(const std::string& name);
//      std::optional<Type> webserverConfigMember(const Type& recv,
//                                                const std::string& prop);
//      bool checkCreateServerConfigOptions(CallExpr& call);  // see (4)
//    and in src/sema/include/cp3/std_surface.hpp:
//      std::vector<std::string> webserverConfigMemberNames(
//          const std::string& objectTypeName);
//      StdMemberDoc webserverConfigMemberDoc(
//          const std::string& objectTypeName, const std::string& member,
//          TypeSystem ts = TypeSystem::TypeScript);
//
// 2. Sema dispatch (member_check.cpp, inside memberOfProbe, BEFORE the
//    network block, next to the webserverObjectMember try):
//      if (auto cfg = webserverConfigMember(ot, prop)) return *cfg;
//      const std::vector<std::string> cfgNames =
//          webserverConfigMemberNames(ot.name);
//      if (!cfgNames.empty() && ot.name == kConfiguredApp) {
//          reportMemberMiss("configured app", prop, span, cfgNames);
//          return Type::unknown();
//      }
//
// 3. createServer inference (src/std/network/codegen/sema.cpp): in
//    checkCreateServerOptions, BEFORE the "unknown option" report for a
//    non-`threads` key (or as a new first branch in tryInferNetworkCall /
//    the `network.createServer` member arm): when the options object has
//    ANY of views/middleware/settings/locals/onError, validate it with
//    checkCreateServerConfigOptions (rules in the file header) and return
//    networkObjectType(kConfiguredApp) instead of kHttpServer. Pure
//    `{ threads }` (and no-arg) keep the existing path verbatim.
//    webserverConfigType(name) returns the kConfiguredApp object type for
//    that name, Type::unknown() otherwise; webserverConfigMember answers
//    from configuredAppFields() (and ambient() for the open locals bag).
// -----------------------------------------------------------------------------
