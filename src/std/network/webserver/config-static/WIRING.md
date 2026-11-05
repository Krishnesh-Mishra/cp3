# createServer() config — static + body limits — wiring (integrator checklist)

Solo scope: `src/std/network/webserver/config-static/**` only. Everything
below is a line the integrator adds to a shared file. Nothing here was
run, built, or committed.

## Files in this family

| File | Role |
|---|---|
| `include/cp3/std/network/webserver/config_static.hpp` | Runtime, header-only (`cp3::runtime::network::webserver::config_static`): `StaticConfig`, `JsonConfig`, `UrlEncodedConfig`, `ServerTimeouts`, `CreateServerConfig`, `validateConfig`/`ensureValid`, `makeConfigStaticMiddleware`, `middlewaresFor`, `installCreateServerConfig`, `applyToSettings`, `createServerWithConfig`, `lastCreateServerConfig`, `sema_codegen` tables. |
| `sema.cpp` | `Analyzer::checkCreateServerConfigOptions` — the full options schema (threads preserved + new keys). |
| `codegen.cpp` | `Generator::emitConfigStaticCreateServer` — lowering for the object form. |
| `native.hpp` | Notes only — no new native row (see below). |
| `README.md` | CP3 examples, options schema, limitations. |

## Options schema (the contract sema + codegen + runtime all enforce)

```
createServer({
  threads?: number,                                  // existing, preserved
  static?:   { dir: string,                           // required if present
               index?: string | boolean,              // default "index.html"; false disables
               etag?: boolean,                        // default true (weak)
               maxAge?: number,                       // seconds, default 0 = no Cache-Control
               fallthrough?: boolean },               // default true (miss -> next())
  bodyLimit?: number | string,                        // bytes / "100kb"; global 413 gate
  json?:     { limit?: number | string, strict?: boolean },   // default strict true
  urlencoded?:{ extended?: boolean, limit?: number | string },// default extended true
  trustProxy?: boolean | number | string | string[],  // -> "trust proxy" setting
  timeouts?: { request?: number, headers?: number,    // ms, 0 = disabled (default)
               idle?: number, keepAlive?: number },
})
```

Presence enables: `static` installs static, `json`/`urlencoded` install
their parsers, `bodyLimit` alone still installs the global gate. Order:
bodyLimit → json → urlencoded → static. Parser limit precedence: own
`limit`, else global `bodyLimit`, else body_parse default ("100kb").

## Wiring lines (shared files, in order)

1. **CMake (sema)** — append to `cp3_sema_sources` in `src/sema/CMakeLists.txt`:
   `../std/network/webserver/config-static/sema.cpp`
2. **CMake (codegen)** — append to `cp3_codegen_sources` in
   `src/codegen/CMakeLists.txt`: `../std/network/webserver/config-static/codegen.cpp`
3. **Sema declaration** — `src/sema/src/detail/analyzer.h`, next to
   `checkCreateServerOptions`:
   ```cpp
   // std/network/webserver/config-static (src/std/network/webserver/config-static/sema.cpp)
   bool checkCreateServerConfigOptions(CallExpr& call);
   ```
4. **Sema dispatch** — `src/std/network/codegen/sema.cpp`, replace BOTH
   `checkCreateServerOptions(call)` call sites (in `tryInferNetworkCall`
   and `tryInferNetworkMemberCall`) with
   `checkCreateServerConfigOptions(call)`. The new checker returns the
   same scale-mode bool and reports the same threads diagnostics, so no
   other sema change is needed.
5. **Codegen declarations** — `src/codegen/src/detail/generator/generator.h`,
   next to `emitNetworkIdentifier`:
   ```cpp
   std::string emitConfigStaticCreateServer(const CallExpr& c);
   ```
   and `src/codegen/src/detail/generator/generator_state.h` (next to
   `needsNetwork_`): `bool needsConfigStatic_ = false;`
6. **Codegen dispatch** — `src/std/network/codegen/codegen.cpp`, in
   `emitNetworkIdentifier`, inside the `name == "createServer"` arm BEFORE
   the threads-only lowering:
   ```cpp
   if (std::string cfg = emitConfigStaticCreateServer(c); !cfg.empty()) return cfg;
   ```
   (The `network.createServer(...)` namespace spelling routes through the
   same arm, so it is covered too.)
7. **Include emission** — `src/codegen/src/support/type/module_writer.cpp`,
   next to the `needsNetwork_` line:
   `if (needsConfigStatic_) out += "#include \"cp3/std/network/webserver/config_static.hpp\"\n";`
   (plus the same line in `src/codegen/src/emit/decl/program.cpp` next to
   the `needsNetwork_` include, mirroring the `needsPath_` pattern there).
   `config_static.hpp` includes `cp3/std/network.hpp`,
   `cp3/std/network/webserver/body_parse.hpp`, and
   `cp3/std/network/webserver/settings.hpp`, so `network.hpp` must NOT
   include it back (circular). The body_parse/settings headers are
   declared in their own families' WIRING.md steps; this family adds no
   include of its own beyond step 7.
8. **Native row** — `src/std/network/codegen/native.hpp`,
   `kNetworkNativePackage` include dirs: add
   `"src/std/network/webserver/config-static/include"` next to
   `"src/std/network/include"`. No TU, no selector, no link change (the
   composed factories live in `runtime_web.cpp` / body-parse's TU, both
   already on the network row).
9. **Server glue (trust proxy + timeouts)** — after `createServer({...})`
   returns, the dispatcher already runs each request through
   `webserver::RequestScope` (settings family, WIRING step 10) — that is
   what makes `trustProxy` effective. `trustProxy` reaches the settings
   table only if the app is a `webserverApp` (or forwards to one):
   ```cpp
   // generated code cannot name webserverApp; this line lives in the
   // server glue / base app that owns one:
   config_static::applyToSettings(config, app.webserverState.settings);
   ```
   `timeouts` is readable via
   `config_static::lastCreateServerConfig().timeouts` for a future
   socket-level mapping; nothing in this family enforces it (see
   Limitations).
10. **Registry** — no new module: the surface rides on the existing
    `std/network` import (`src/modules/src/support/std_registry.cpp`
    unchanged). Programs `import { createServer } from "std/network"` as
    today.

## Limitations (deliberate)

- **`timeouts` is stored, not enforced.** `HttpServer` exposes no socket
  timeout knob, so no middleware here can abort a slow request. The values
  are validated (`>= 0`, finite) and snapshotted for the integrator's glue
  (e.g. a uWS idle-timeout mapping); until that glue exists they are a
  documented no-op. `request` is the intended per-request budget,
  `headers`/`idle`/`keepAlive` the connection-level ones.
- **`trustProxy` needs `RequestScope` + a settings-owning app.** A bare
  `createServer` chain never calls `applyTrustProxy`, so `req.ip` stays
  the socket address. The function form of webserver's "trust proxy"
  (custom predicate) is unsupported — same divergence as the settings
  family; strings map to `loopback`/`linklocal`/`uniquelocal`/`trust all`/
  hop-count.
- **Zero means different things per layer** (inherited, documented in the
  header): global `bodyLimit: 0` rejects any non-empty body
  (`makeBodyLimitMiddleware` rule); a parser `limit: 0` means unlimited
  (body_parse rule). Negative limits are rejected at construction.
- **Static divergences from serve-static**: dotfiles are served (no
  `dotfiles: "ignore"` default), directory requests do NOT get a 301
  redirect to the trailing-slash form (the index file is served
  directly), there is no `setHeaders`/`extensions`/`redirect` option, and
  ETag is weak-only. `HEAD` goes through `res.sendFile` like the stock
  `makeStaticMiddleware` (the server owns body stripping, if any).
- **A non-literal options object** (`createServer(opts)`) is untouched by
  this family: sema reports it and codegen falls back to the legacy
  threads lowering, which does not see the config-static keys. Nested
  non-literal sub-objects (`json: someVar`) pass sema and are reported by
  codegen. Computed values inside a literal object are runtime-validated
  by `ensureValid`.
- **Config is applied at construction only.** Changing `static`/
  parsers after `createServer` returns means explicit `app.use(...)`
  calls (the stock factories), not a re-`installCreateServerConfig`
  (which would double-install the chain).
- **`lastCreateServerConfig()` is thread-local** (one snapshot per loop
  thread, like the D79 module state). In a scaled server each loop
  thread's snapshot reflects its own `createServerWithConfig` call.
