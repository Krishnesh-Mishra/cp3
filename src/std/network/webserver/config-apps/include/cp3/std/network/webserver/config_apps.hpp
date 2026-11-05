// =============================================================================
// src/std/network/webserver/config-apps/include/cp3/std/network/webserver/config_apps.hpp
// `createServer({ views, middleware, settings, locals, onError })`: the
// declarative webserver app built from one options object.
//
// OWNERSHIP: src/std/network/webserver/config-apps/** only (SOLO subtree).
// This family composes the two existing families without editing them:
//   * settings     (cp3::runtime::webserver, webserver/settings.hpp):
//                  Settings / Locals / ViewRegistry / webserverApp /
//                  RequestScope (trust proxy, res.locals, res.render).
//   * middleware   (cp3::runtime::network::webserver, middleware.hpp):
//                  MiddlewareEngine (ordered chain, mounts, next("route"),
//                  4-arg error dispatch).
//   * std/network  (cp3::runtime::network::HttpServer): the socket server,
//                  routes, listen/close. Routes are NOT part of this config
//                  (see README.md): register them on server() after install().
//
// CP3 spelling (codegen fragment: codegen_config_apps.cpp):
//   const app = createServer({
//     views:    { dir: "./views", engine: "ejs" },
//     middleware: [(req, res, next) => { next(); }],
//     settings: { etag: "weak", xPoweredBy: true, jsonSpaces: 2,
//                 trustProxy: "loopback" },
//     locals:   { title: "Hello" },
//     onError:  (err, req, res, next) => { res.status(500).text("..."); },
//   });
//
// The C++ surface below is what that object lowers to. Everything except
// ConfiguredApp::install/dispatch/listen is configuration-time and runs
// before the first request; settings are not mutated while serving.
// =============================================================================
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/std/network/webserver/middleware.hpp"
#include "cp3/std/network/webserver/settings.hpp"

namespace cp3::runtime::network::webserver::configapps {

namespace settings_app = ::cp3::runtime::webserver;

using SettingValue = settings_app::SettingValue;
using Locals = settings_app::Locals;
using Settings = settings_app::Settings;
using ViewEngineFn = settings_app::ViewEngineFn;

/// webserver-style callback view engine: `(file, options, callback) => void`,
/// callback invoked once with the HTML. Adapted with
/// settings_app::engineFromCallback when a config carries one.
using ViewEngineCallbackFn = std::function<void(
    const std::string& file, const Locals& options,
    std::function<void(std::string html)>)>;

/// `middleware: [...]` entries are the middleware family's functions:
/// 3-arg normal middleware `(req, res, next)`, 4-arg error middleware
/// `(err, req, res, next)`. Use MiddlewareEntry for a mounted entry.
using ConfigMiddlewareFn = MiddlewareFn;
using ConfigErrorFn = ErrorMiddlewareFn;

/// Maps a config `settings` key onto the webserver setting name it writes.
/// "" = not a supported config key. Pure (sema/codegen share the table).
inline const char* configSettingNameForKey(const std::string& key) {
    if (key == "etag") return "etag";
    if (key == "xPoweredBy") return "x-powered-by";
    if (key == "jsonSpaces") return "json spaces";
    if (key == "trustProxy") return "trust proxy";
    return "";
}

/// True for the five top-level `createServer({...})` keys of this family.
inline bool isConfigTopLevelKey(const std::string& key) {
    return key == "views" || key == "middleware" || key == "settings" ||
           key == "locals" || key == "onError";
}

// =============================================================================
// views: { dir, engine, engineFn? }
// =============================================================================
/// `views: { dir, engine }`:
///   * `dir`    -> the "views" setting (template directory, default "./views").
///   * `engine` -> the "view engine" setting: the extension `res.render`
///                 dispatches on when the view name carries none of its own
///                 ("ejs", "html", ...; dot/case-insensitive at lookup).
///   * `engineFn` (optional, not in the minimal spelling) -> the engine
///                 function registered for `engine`. Requires `engine` to
///                 name the extension; applyServerConfig throws otherwise.
///                 A callback-form engine is `callbackEngineFn` instead.
/// With no engineFn, rendering uses the settings family's trivial
/// `{{ key }}` fallback engine (no escaping, no control flow).
struct ViewsConfig {
    std::string dir;
    std::string engine;
    ViewEngineFn engineFn;
    ViewEngineCallbackFn callbackEngineFn;
};

// =============================================================================
// settings: { etag, xPoweredBy, jsonSpaces, trustProxy }
// =============================================================================
/// The four supported config settings, mapped onto the Settings table by
/// applyServerConfig (see configSettingNameForKey). Absent (nullopt) keys
/// keep the webserver defaults already in Settings:
///   etag "weak", x-powered-by true, json spaces 0, trust proxy false.
///   * etag:       SettingValue: true/"weak"/"strong"/false. Stored; the
///                 conditional-GET helpers (request/sendFile families) read
///                 it. Auto-ETag on res.text/res.json is NOT applied here
///                 (the response body does not exist yet when this config
///                 runs; see README.md limitations).
///   * xPoweredBy: bool. Applied per response: "X-Powered-By: Express"
///                 header when enabled (webserver parity value).
///   * jsonSpaces: number of spaces for res.json pretty-printing. Stored
///                 under "json spaces"; HttpResponse::json does not read it
///                 yet (see README.md limitations).
///   * trustProxy: SettingValue: bool | number | string | string[].
///                 Applied per request through RequestScope ->
///                 applyTrustProxy (req.ip / req.protocol). The webserver
///                 function form is unsupported (settings family limit).
struct AppSettingsConfig {
    std::optional<SettingValue> etag;
    std::optional<bool> xPoweredBy;
    std::optional<double> jsonSpaces;
    std::optional<SettingValue> trustProxy;
};

// =============================================================================
// middleware: [...] (+ onError)
// =============================================================================
/// One `middleware: [...]` entry. The bare form is a normal 3-arg handler at
/// mount "/"; a mounted entry filters by path (segment-boundary prefix,
/// middleware family semantics); an error entry is a 4-arg handler run only
/// while an error is pending. Entries run in array order.
struct MiddlewareEntry {
    std::string mount = "/";
    ConfigMiddlewareFn fn;
    ConfigErrorFn errorFn;
    bool isError = false;

    static MiddlewareEntry normal(ConfigMiddlewareFn handler) {
        MiddlewareEntry entry;
        entry.fn = std::move(handler);
        return entry;
    }
    static MiddlewareEntry mounted(std::string mountPath,
                                   ConfigMiddlewareFn handler) {
        MiddlewareEntry entry;
        entry.mount = std::move(mountPath);
        entry.fn = std::move(handler);
        return entry;
    }
    static MiddlewareEntry error(ConfigErrorFn handler) {
        MiddlewareEntry entry;
        entry.isError = true;
        entry.errorFn = std::move(handler);
        return entry;
    }
    static MiddlewareEntry mountedError(std::string mountPath,
                                        ConfigErrorFn handler) {
        MiddlewareEntry entry;
        entry.mount = std::move(mountPath);
        entry.isError = true;
        entry.errorFn = std::move(handler);
        return entry;
    }
};

/// Adapts a 3-arg `onError: (err, req, res) => ...` handler (the HttpServer
/// Ledger-L shape) to the config's 4-arg ConfigErrorFn. The adapter's next
/// is unreachable from user code: a 3-arg handler ends the error chain by
/// responding, exactly as HttpServer::onError does today.
template <class F>
ConfigErrorFn onErrorFrom3Arg(F fn) {
    return [fn = std::move(fn)](Thrown err, HttpRequest& req, HttpResponse& res,
                                webserverNext&) mutable -> Promise<void> {
        return detail::callAsPromise(fn, err, req, res);
    };
}

// =============================================================================
// ServerConfig - the createServer({...}) options value
// =============================================================================
/// The whole options object. `locals` is the app.locals bag (visible to
/// every render as the lowest-precedence layer: render options > res.locals
/// > app.locals). `onError` is an optional trailing 4-arg error handler.
struct ServerConfig {
    ViewsConfig views;
    std::vector<MiddlewareEntry> middleware;
    AppSettingsConfig settings;
    Locals locals;
    ConfigErrorFn onError;

    void addMiddleware(ConfigMiddlewareFn fn) {
        middleware.push_back(MiddlewareEntry::normal(std::move(fn)));
    }
    void addMiddleware(const std::string& mount, ConfigMiddlewareFn fn) {
        middleware.push_back(
            MiddlewareEntry::mounted(mount, std::move(fn)));
    }
    void addErrorMiddleware(ConfigErrorFn fn) {
        middleware.push_back(MiddlewareEntry::error(std::move(fn)));
    }
    void addErrorMiddleware(const std::string& mount, ConfigErrorFn fn) {
        middleware.push_back(
            MiddlewareEntry::mountedError(mount, std::move(fn)));
    }
    // No templated addMiddleware: a lambda converts to ConfigMiddlewareFn
    // implicitly (as in the middleware family, where passing a std::function
    // to a templated use() can be ambiguous).
};

// =============================================================================
// Configuration application (configuration-time, before serving)
// =============================================================================
/// Writes views/settings/locals/middleware into an existing settings app +
/// middleware engine. Does NOT touch `config.onError` (the ConfiguredApp
/// registers it, so it can also bridge it to HttpServer::onError); does not
/// install anything on a server. Throws Error when a view engineFn is given
/// without an `engine` extension to register it under.
void applyServerConfig(settings_app::webserverApp& app, MiddlewareEngine& engine,
                       const ServerConfig& config);

/// Per-response settings that can be applied before the body exists:
/// the X-Powered-By header. (etag/jsonSpaces/trustProxy are stored/consumed
/// elsewhere; see AppSettingsConfig.)
void applyResponseSettings(const Settings& settings, HttpResponse& res);

// =============================================================================
// ConfiguredApp - HttpServer + settings app + middleware engine, from config
// =============================================================================
/// The value behind `createServer({...})` for this family. Owns one
/// HttpServer (routes/listen), one settings webserverApp (views/locals/
/// settings/res.render state), and one MiddlewareEngine (the config chain).
///
/// Ordering contract: the config chain is installed on the server by
/// install() (called by listen()). Call install() explicitly BEFORE
/// registering routes on server() if config middleware must run before
/// them; routes registered first run first, as on HttpServer. Like the
/// routing family's webserverApp, moving a ConfiguredApp is safe only
/// before install() (the installed dispatch captures `this`): construct in
/// place, or move the makeConfiguredApp() result before install/listen.
class ConfiguredApp {
public:
    explicit ConfiguredApp(ServerConfig config);
    ConfiguredApp() = default;  // empty config (all defaults)

    // --- the three owned pieces -------------------------------------------
    network::HttpServer& server() { return server_; }
    const network::HttpServer& server() const { return server_; }
    settings_app::webserverApp& settingsApp() { return app_; }
    const settings_app::webserverApp& settingsApp() const { return app_; }
    Settings& settings() { return app_.settings; }
    const Settings& settings() const { return app_.settings; }
    Locals& locals() { return app_.locals; }
    const Locals& locals() const { return app_.locals; }
    MiddlewareEngine& engine() { return engine_; }
    const MiddlewareEngine& engine() const { return engine_; }
    bool installed() const { return installed_; }

    // --- settings/views forwarding (the app.set/get/engine/locals surface)
    ConfiguredApp& setSetting(const std::string& name, SettingValue value) {
        app_.setSetting(name, std::move(value));
        return *this;
    }
    SettingValue getSetting(const std::string& name) const {
        return app_.getSetting(name);
    }
    ConfiguredApp& enable(const std::string& name) { app_.enable(name); return *this; }
    ConfiguredApp& disable(const std::string& name) { app_.disable(name); return *this; }
    bool enabled(const std::string& name) const { return app_.enabled(name); }
    ConfiguredApp& setEngine(const std::string& ext, ViewEngineFn fn) {
        app_.setEngine(ext, std::move(fn));
        return *this;
    }
    std::string renderToString(const std::string& view,
                               const Locals& options = Locals()) const {
        return app_.renderToString(view, options);
    }
    void render(HttpResponse& res, const std::string& view,
                const Locals& options = Locals()) const {
        app_.render(res, view, options);
    }

    // --- middleware forwarding (config chain, not server middleware) -------
    void use(ConfigMiddlewareFn fn) { engine_.useMiddleware(std::move(fn)); }
    void use(const std::string& mount, ConfigMiddlewareFn fn) {
        engine_.useMiddleware(mount, std::move(fn));
    }
    void use(ConfigErrorFn fn) { engine_.useError(std::move(fn)); }
    void use(const std::string& mount, ConfigErrorFn fn) {
        engine_.useError(mount, std::move(fn));
    }
    template <class F>
    void use(F fn) { engine_.use(std::move(fn)); }
    template <class F>
    void use(const std::string& mount, F fn) { engine_.use(mount, std::move(fn)); }
    template <class F>
    void useError(F fn) { engine_.useError(std::move(fn)); }

    /// Sets the config error handler: appended to the engine as the final
    /// error layer, and bridged to HttpServer::onError for errors thrown
    /// outside the engine (defensive; the engine normally catches first,
    /// so the bridge does not double-fire). 3-arg handlers are adapted.
    void setOnError(ConfigErrorFn fn);
    template <class F>
    void onError(F fn) {
        if constexpr (std::is_invocable_v<F&, Thrown, HttpRequest&, HttpResponse&,
                                          webserverNext&>) {
            setOnError(ConfigErrorFn(std::move(fn)));
        } else {
            setOnError(onErrorFrom3Arg(std::move(fn)));
        }
    }

    // --- route forwarding (HttpServer surface, out of config scope) --------
    template <class F>
    void get(const std::string& path, F handler) const { server_.get(path, std::move(handler)); }
    template <class F>
    void post(const std::string& path, F handler) const { server_.post(path, std::move(handler)); }
    template <class F>
    void put(const std::string& path, F handler) const { server_.put(path, std::move(handler)); }
    template <class F>
    void del(const std::string& path, F handler) const { server_.del(path, std::move(handler)); }
    template <class F>
    void patch(const std::string& path, F handler) const { server_.patch(path, std::move(handler)); }
    template <class F>
    void head(const std::string& path, F handler) const { server_.head(path, std::move(handler)); }
    template <class F>
    void options(const std::string& path, F handler) const { server_.options(path, std::move(handler)); }
    template <class F>
    void any(const std::string& path, F handler) const { server_.any(path, std::move(handler)); }
    template <class F>
    void all(const std::string& path, F handler) const { server_.all(path, std::move(handler)); }
    network::HttpRouteChain route(const std::string& path) const { return server_.route(path); }
    template <class F>
    void param(const std::string& name, F handler) const { server_.param(name, std::move(handler)); }

    // --- serving ------------------------------------------------------------
    /// One request through the config: RequestScope (current app for
    /// res.render, trust proxy -> req.ip/req.protocol, fresh res.locals),
    /// per-response settings, then the middleware chain. The scope is kept
    /// alive until the chain settles (heap-held, captured by the returned
    /// promise's continuations).
    Promise<void> dispatch(HttpRequest& req, HttpResponse& res);

    /// Installs dispatch() as the server's middleware (once; listen()
    /// calls it too). Call before registering routes on server() when
    /// config middleware must precede them.
    void install();

    void listen(double port, std::function<void()> callback = {}) {
        install();
        server_.listen(port, std::move(callback));
    }
    void close() const { server_.close(); }

private:
    settings_app::webserverApp app_;
    MiddlewareEngine engine_;
    network::HttpServer server_;
    ConfigErrorFn onError_;
    bool installed_ = false;
};

/// Factories spelling the CP3 surface (`createServer({...})` at codegen).
inline ConfiguredApp makeConfiguredApp(ServerConfig config) {
    return ConfiguredApp(std::move(config));
}
inline ConfiguredApp makeConfiguredApp() { return ConfiguredApp(); }

}  // namespace cp3::runtime::network::webserver::configapps
