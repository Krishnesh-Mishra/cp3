# webserver config-apps — `createServer({...})` (views + middleware + settings)

SOLO subtree `src/std/network/webserver/config-apps/**`. No shared file was
touched. Code only (no build/test/commit).

## Files

| File | Role |
|---|---|
| `include/cp3/std/network/webserver/config_apps.hpp` | Public API: `ServerConfig`, `ViewsConfig`, `AppSettingsConfig`, `MiddlewareEntry`, `ConfiguredApp`, `makeConfiguredApp`, `applyServerConfig`, `applyResponseSettings`, `onErrorFrom3Arg`, key-mapping helpers |
| `config_apps.cpp` | Runtime: `applyServerConfig`, `applyResponseSettings`, `ConfiguredApp` ctor/`setOnError`/`dispatch`/`install` |
| `sema_config_apps.cpp` | Sema fragment: `$webserver:ConfiguredApp` member table + config-object validation rules (splice comments) |
| `codegen_config_apps.cpp` | Codegen fragment: `createServer({...})` → `makeConfiguredApp(ServerConfig{...})` + `ConfiguredApp` member-call lowering (splice comments) |
| `native_config_apps.hpp` | Native row snippet (paste-only, not applied) |
| `README.md` | This file |

## Options schema

```ts
createServer({
  views: {                              // → Settings "views" / "view engine"
    dir: string,                        // template dir (default "./views")
    engine: string,                     // view-engine extension ("ejs", "html", …)
    engineFn?: (file: string, options: object) => string, // optional engine fn
  },
  middleware: [                         // array order = execution order
    (req, res, next) => { … },          // 3-arg: normal middleware
    (err, req, res, next) => { … },     // 4-arg: error middleware
  ],
  settings: {                           // → the Settings table
    etag?: boolean | string,            // "etag" (default "weak")
    xPoweredBy?: boolean,               // "x-powered-by" (default true)
    jsonSpaces?: number,                // "json spaces" (default 0)
    trustProxy?: boolean | number | string | string[], // "trust proxy"
  },
  locals: { [key: string]: string | number | boolean | null | string[] },
                                        // → app.locals (render defaults)
  onError?: (err, req, res) => { … }    // 3-arg, or
  | (err, req, res, next) => { … },      // 4-arg error handler (final layer)
})
```

Only these keys exist. `createServer()` and `createServer({ threads })`
keep the existing `$network:HttpServer` lowering (D79) untouched —
`threads` combined with any config key is a type error. The config object,
and every nested object, must be an object literal (a computed binding is
a type error at the use site, exactly like `threads`).

## Export surface (C++)

```cpp
namespace cp3::runtime::network::webserver::configapps {

const char* configSettingNameForKey(const std::string& key); // "xPoweredBy" → "x-powered-by", …
bool isConfigTopLevelKey(const std::string& key);

struct ViewsConfig { std::string dir, engine; ViewEngineFn engineFn;
                     ViewEngineCallbackFn callbackEngineFn; };
struct AppSettingsConfig { std::optional<SettingValue> etag;              // bool|string
                           std::optional<bool> xPoweredBy;
                           std::optional<double> jsonSpaces;
                           std::optional<SettingValue> trustProxy; };    // bool|number|string|string[]
struct MiddlewareEntry { std::string mount = "/";
    ConfigMiddlewareFn fn; ConfigErrorFn errorFn; bool isError = false;
    static MiddlewareEntry normal(ConfigMiddlewareFn);
    static MiddlewareEntry mounted(std::string, ConfigMiddlewareFn);
    static MiddlewareEntry error(ConfigErrorFn);
    static MiddlewareEntry mountedError(std::string, ConfigErrorFn); };
using ConfigMiddlewareFn = MiddlewareFn;   // (req, res, next)
using ConfigErrorFn      = ErrorMiddlewareFn; // (err, req, res, next)
template <class F> ConfigErrorFn onErrorFrom3Arg(F);  // (err,req,res) adapter

struct ServerConfig { ViewsConfig views; std::vector<MiddlewareEntry> middleware;
    AppSettingsConfig settings; Locals locals; ConfigErrorFn onError;
    void addMiddleware(...); /* + mounted / error forms */ };

void applyServerConfig(webserver::webserverApp&, MiddlewareEngine&,
                       const ServerConfig&);   // views+settings+locals+chain
void applyResponseSettings(const Settings&, HttpResponse&); // X-Powered-By

class ConfiguredApp {   // HttpServer + settings webserverApp + MiddlewareEngine
  ConfiguredApp(); explicit ConfiguredApp(ServerConfig);
  HttpServer& server(); settingsApp(); settings(); locals(); engine();
  setSetting/getSetting/enable/disable/enabled/setEngine/renderToString/render;
  use(mw) / use(mount, mw) / use(errMw); setOnError / onError(F);
  get/post/put/del/patch/head/options/any/all/route/param; // → server()
  Promise<void> dispatch(HttpRequest&, HttpResponse&);
  void install(); void listen(double, std::function<void()> = {}); void close() const;
};
ConfiguredApp makeConfiguredApp(ServerConfig); ConfiguredApp makeConfiguredApp();
}
```

Semantics: config is applied once at construction (views → `app.set("views",
dir)` / `app.set("view engine", engine)` + optional `app.engine(engine,
fn)`; settings → the four named settings; `locals` → `app.locals`; middleware
entries → `MiddlewareEngine` in array order; `onError` → the engine's final
4-arg error layer, bridged to `HttpServer::onError` for errors thrown
outside the chain). Per request, `dispatch()` runs `RequestScope` (current
app for `res.render`, trust-proxy → `req.ip`/`req.protocol`, fresh
`res.locals`), applies the X-Powered-By header, then the middleware chain
(404 when exhausted, 500 on unhandled error — engine defaults).

## Wiring lines (for the integrator — NOT applied, shared files untouched)

Order matters; sema/codegen fragments are not compiled until steps 1–3 land.

1. **Sema declarations** — `src/sema/src/detail/analyzer.h` (next to the
   settings family's `webserverObjectMember` decls):
   `webserverConfigType`, `webserverConfigMember`,
   `checkCreateServerConfigOptions`; `src/sema/include/cp3/std_surface.hpp`:
   `webserverConfigMemberNames`, `webserverConfigMemberDoc`.
2. **Sema TU + dispatch** — append `sema_config_apps.cpp` to
   `cp3_sema_sources` (`src/sema/CMakeLists.txt`); `member_check.cpp`
   tries `webserverConfigMember` before the network block (exact lines in
   the fragment's splice comment); `src/std/network/codegen/sema.cpp`
   returns `$webserver:ConfiguredApp` for config `createServer` calls via
   `checkCreateServerConfigOptions` (fragment header lists the rules).
3. **Codegen TU + dispatch** — append `codegen_config_apps.cpp` to
   `cp3_codegen_sources` (`src/codegen/CMakeLists.txt`); generator.h/state
   decls, `emitCreateServerConfig` claim in the `createServer` arm of
   `emitNetworkIdentifier` (+ `network.createServer` arm),
   `emitConfigMemberCall` before `isNetworkObject(...)` in `emitMemberCall`,
   and the `locals`/`settings` member reads (exact lines in the fragment's
   splice comment).
4. **Include emission** — `module_writer.cpp` / `program.cpp` next to the
   `needsNetwork_` gate: `#include "cp3/std/network/webserver/config_apps.hpp"`.
5. **Native row** — `src/std/network/codegen/native.hpp` (D68):
   `kNetworkNativePackage` include dirs add
   `config-apps/include` + `settings/include` + `middleware/include`;
   `runtimeSources` adds `config-apps/config_apps.cpp`; marker
   `makeConfiguredApp(|configapps::applyServerConfig(|configapps::ConfiguredApp(`
   — see `native_config_apps.hpp`.

## Limitations

- **Routes are not in the config.** Register them on `app.server().get(…)`
  (existing `HttpServer` surface). Ordering: the config chain installs at
  `listen()` (lazily, so `makeConfiguredApp()` may return by value) — call
  `app.install()` *before* registering routes if config middleware must
  run first; routes registered before `install()` run first.
- **Move before `install()`.** The installed dispatch captures `this`
  (same trap as the routing family's `webserverApp`); construct in place
  or move the factory result before `install()`/`listen()`.
- **`etag` / `jsonSpaces` are stored, not yet response-applied.** Auto-ETag
  needs the response body (does not exist when config runs) and
  `HttpResponse::json` serialises without consulting `"json spaces"` —
  both need shared-file changes outside this solo scope. `x-powered-by`
  and `trust proxy` *are* applied per request.
- **No bundled template language.** Without `views.engineFn`, renders use
  the settings family's trivial `{{ key }}` fallback (no escaping/control
  flow). An `engineFn` without `views.engine` (the extension to register
  under) throws at construction.
- **Trust proxy** follows the settings family's limits: no function form,
  no arbitrary CIDR/hostname lists.
- **`onError` double-fire is avoided by construction:** engine errors
  never reach the `HttpServer::onError` bridge; the bridge only serves
  errors thrown outside the chain (e.g. in `server()` routes). If
  `onError` itself throws, the engine answers 500.
- **Async current-app is best-effort:** `dispatch()` keeps the
  `RequestScope` alive until the chain settles, but interleaved async
  requests on one thread can see each other's app after an `await`
  (thread-local side channel, settings family limit).
- **Config values are literals only.** A `const cfg = {…}` binding, spread
  elements, or computed keys are type errors at the use site (same rule
  as `threads`); settings/locals values are scalars or string lists
  (`SettingValue`), so object/array values are rejected, not stringified.
