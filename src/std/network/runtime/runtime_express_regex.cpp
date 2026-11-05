// =============================================================================
// src/std/network/runtime/runtime_webserver_regex.cpp - regex-routing bridge.
//
// OWNERSHIP: the std/network family (src/std/network/runtime/**).
// Declared in cp3/std/network.hpp (regex_routing forward declarations +
// addRegexRoute free functions). Implements PathSpec/RoutingOptions route
// registration on HttpServer: registers a "*" route whose wrapper matches
// the PathSpec (regex_routing::matchPath) and only then runs the handler,
// so string paths keep the stock dispatcher and RegExp/array paths get the
// extended matcher. App settings are read at registration time (webserver
// snapshot semantics).
// =============================================================================
#include "cp3/std/network.hpp"

#include <memory>
#include <string>

#include "../server/runtime_server_internal.hpp"
#include "regex_routing.hpp"

namespace cp3::runtime::network {

void addRegexRoute(const HttpServer& server, const char* method,
                   const regex_routing::PathSpec& spec,
                   const regex_routing::RoutingOptions& options,
                   detail::RouteFn handler) {
    // Snapshot the app settings at registration (webserver compiles the path
    // at registration). Settings live on ServerImpl via setRoutingSetting;
    // read them through a const_cast-free path: the server's impl is private,
    // so resolve from the well-known keys with defaults (false) unless the
    // generated code set them before registering - in that case the snapshot
    // helpers below consult the same registry the dispatcher uses.
    regex_routing::RoutingSettings app;
    app.caseSensitive = server.getRoutingSetting("case sensitive routing");
    app.strict = server.getRoutingSetting("strict routing");
    const regex_routing::EffectiveOptions eff = regex_routing::resolveEffective(app, options);
    auto specShared = std::make_shared<regex_routing::PathSpec>(spec);
    server.addRouteImpl(method, "*", [specShared, eff, handler](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
        const regex_routing::MatchResult result = regex_routing::matchPath(*specShared, req.path, eff);
        if (!result.matched) {
            next();
            return Promise<void>::resolve();
        }
        StringMap params = collections::makeMap<std::string, std::string>();
        for (auto& [name, value] : result.params) params->set(name, value);
        req.setParams(std::move(params));
        return handler(req, res, next);
    });
}

void addRegexRouteWithOptions(const HttpServer& server, const char* method,
                              const regex_routing::PathSpec& spec,
                              const regex_routing::RoutingOptions& options,
                              detail::RouteFn handler) {
    addRegexRoute(server, method, spec, options, handler);
}

}  // namespace cp3::runtime::network
