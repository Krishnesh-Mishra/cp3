// =============================================================================
// src/std/network/webserver/config-apps/config_apps.cpp
// Runtime for `createServer({ views, middleware, settings, locals, onError })`.
// OWNERSHIP: src/std/network/webserver/config-apps/** only (SOLO subtree).
// Links against the existing settings (header-only) and middleware
// (middleware_engine.cpp) TUs; no shared file is touched.
// =============================================================================
#include "cp3/std/network/webserver/config_apps.hpp"

#include <memory>
#include <utility>

namespace cp3::runtime::network::webserver::configapps {

void applyServerConfig(settings_app::webserverApp& app, MiddlewareEngine& engine,
                       const ServerConfig& config) {
    // --- views -------------------------------------------------------------
    if (!config.views.dir.empty()) {
        app.setSetting("views", SettingValue(config.views.dir));
    }
    if (!config.views.engine.empty()) {
        app.setSetting("view engine", SettingValue(config.views.engine));
    }
    if (config.views.engineFn || config.views.callbackEngineFn) {
        if (config.views.engine.empty()) {
            throw Error(
                "createServer() views: an engine function requires "
                "'views.engine' to name the extension it renders");
        }
        // A synchronous engine wins when both forms are given.
        if (config.views.engineFn) {
            app.setEngine(config.views.engine, config.views.engineFn);
        } else {
            app.setEngine(config.views.engine,
                          settings_app::engineFromCallback(
                              config.views.callbackEngineFn));
        }
    }

    // --- settings ------------------------------------------------------------
    if (config.settings.etag) {
        app.setSetting("etag", *config.settings.etag);
    }
    if (config.settings.xPoweredBy) {
        app.setSetting("x-powered-by",
                       SettingValue(*config.settings.xPoweredBy));
    }
    if (config.settings.jsonSpaces) {
        app.setSetting("json spaces", SettingValue(*config.settings.jsonSpaces));
    }
    if (config.settings.trustProxy) {
        app.setSetting("trust proxy", *config.settings.trustProxy);
    }

    // --- locals (app.locals) ---------------------------------------------------
    app.locals.mergeFrom(config.locals);

    // --- middleware chain (array order = registration order) -------------------
    for (const MiddlewareEntry& entry : config.middleware) {
        if (entry.isError) {
            if (entry.errorFn) engine.useError(entry.mount, entry.errorFn);
        } else if (entry.fn) {
            engine.useMiddleware(entry.mount, entry.fn);
        }
    }
}

void applyResponseSettings(const Settings& settings, HttpResponse& res) {
    // webserver sends "X-Powered-By: Express" unless disabled. The body does
    // not exist yet, so etag / json-spacing cannot be applied here (stored
    // settings only; see README.md).
    if (settings.enabled("x-powered-by")) {
        res.header("X-Powered-By", "Express");
    }
}

ConfiguredApp::ConfiguredApp(ServerConfig config) {
    applyServerConfig(app_, engine_, config);
    if (config.onError) setOnError(std::move(config.onError));
}

void ConfiguredApp::setOnError(ConfigErrorFn fn) {
    if (!fn) return;
    onError_ = fn;
    // Final error layer of the config chain: throws / next(err) from any
    // earlier layer land here. Its next() clears the error and resumes
    // normal layers after it (none, unless use() is called later).
    engine_.useError(std::move(fn));
}

Promise<void> ConfiguredApp::dispatch(HttpRequest& req, HttpResponse& res) {
    // Heap-held so the current app / res.locals / trust-proxy state outlive
    // async handlers: the continuations capture the scope and release it
    // only when the chain settles (its destructor restores the previous
    // current app). Interleaved async requests on one thread can still see
    // each other's current app after an await (settings family limit).
    auto scope = std::make_shared<settings_app::RequestScope>(app_, req, res);
    applyResponseSettings(app_.settings, res);
    return engine_.dispatch(req, res).then(
        [scope]() -> Promise<void> { return Promise<void>::resolve(); },
        [scope](Thrown err) -> Promise<void> {
            (void)scope;
            throw err;
        });
}

void ConfiguredApp::install() {
    if (installed_) return;
    installed_ = true;
    server_.use([this](HttpRequest& req, HttpResponse& res, Next&) -> Promise<void> {
        return dispatch(req, res);
    });
    if (onError_) {
        // Defensive bridge only: errors inside the config chain never reach
        // the server dispatcher (the engine turns them into onError-layer
        // runs or a 500), so this fires solely for errors thrown outside
        // the chain (e.g. in a route registered directly on server()).
        ConfigErrorFn handler = onError_;
        server_.onError([handler](Thrown err, HttpRequest& req,
                                  HttpResponse& res) mutable -> Promise<void> {
            webserverNext next;
            return handler(std::move(err), req, res, next);
        });
    }
}

}  // namespace cp3::runtime::network::webserver::configapps
