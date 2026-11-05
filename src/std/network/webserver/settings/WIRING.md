# webserver settings + view engine — wiring (integrator checklist)

Solo scope: `src/std/network/webserver/settings/**` only. Everything below is
a line the integrator adds to a shared file. Nothing here was run, built,
or committed.

## Files in this family

| File | Role |
|---|---|
| `include/cp3/std/network/webserver/settings.hpp` | Runtime, header-only (`cp3::runtime::webserver`): `SettingValue`, `Settings`, `Locals`, `ViewRegistry`, `webserverApp`, trust-proxy helpers, `RequestScope`, `renderResponse`, `requestProtocol`. |
| `sema.cpp` | Member tables + docs for `$webserver:webserverApp`, `$webserver:Locals`, and the appended `$network:HttpResponse` / `$network:HttpRequest` rows. |
| `codegen.cpp` | Lowering for the calls, reads, and locals writes above. |
| `native.hpp` | Notes only — no new native row (header-only, rides on `std/network`). |

## Export surface (runtime, `cp3::runtime::webserver`)

- Values: `SettingValue` (`null | bool | double | string | string[]`, JS-style
  coercions, scalar conversions so `app.get("views")` can land in a typed
  `string` binding); `Settings` (`set/get/getOr/has/remove/enable/disable/
  enabled/getString/getBool/getNumber` + webserver defaults); `Locals`
  (`set/get/getOr/has/remove/clear/mergeFrom`); `RenderOptions` = alias of
  `Locals`.
- Views: `ViewEngineFn` = `string(file, Locals)`; `ViewRegistry`
  (`setEngine/hasEngine/findEngine/render`); `renderTrivialTemplate`,
  `makeTrivialEngine`, `engineFromCallback`; extension normalization
  (dot/ case-insensitive).
- App: `webserverApp` — public `settings`, `locals`, `engines`; methods
  `enable/disable/enabled/setSetting/getSetting/setEngine/resolveViewPath/
  viewExtension/renderToString/render`. **Codegen emits exactly these member
  names**; a base webserver app class forwards to / inherits this type.
- Trust proxy: `TrustProxyConfig`, `parseTrustProxy`, `isLoopbackAddress`,
  `isLinkLocalAddress`, `isUniqueLocalAddress`, `resolveClientIp`,
  `resolveProtocol`, `applyTrustProxy(req, settings)`.
- Request plumbing: `currentApp()`, `resLocalsFor(res)`,
  `clearResLocals(res)`, `RequestScope(app, req, res)`,
  `renderResponse(res, view, options)`, `requestProtocol(req)`,
  `kProtocolHeaderSlot`.

## Wiring lines (shared files, in order)

1. **CMake (sema)** — append to `cp3_sema_sources` in `src/sema/CMakeLists.txt`:
   `../std/network/webserver/settings/sema.cpp`
2. **CMake (codegen)** — append to `cp3_codegen_sources` in
   `src/codegen/CMakeLists.txt`: `../std/network/webserver/settings/codegen.cpp`
3. **Sema declarations** — `src/sema/src/detail/analyzer.h`, next to the
   `networkObjectMember` declaration:
   ```cpp
   // std/network/webserver/settings (src/std/network/webserver/settings/sema.cpp)
   Type webserverObjectType(const std::string& name);
   std::optional<Type> webserverObjectMember(const Type& recv, const std::string& prop);
   ```
   and in `src/sema/include/cp3/std_surface.hpp` (next to
   `networkObjectMemberNames`):
   ```cpp
   std::vector<std::string> webserverObjectMemberNames(const std::string& objectTypeName);
   StdMemberDoc webserverObjectMemberDoc(const std::string& objectTypeName, const std::string& member, TypeSystem ts = TypeSystem::TypeScript);
   ```
4. **Sema dispatch** — `src/sema/src/checks/expr/member_check.cpp`, inside
   `memberOfProbe` at the top of the `TypeKind::Object` case, BEFORE the
   network block (so `res.render`/`res.locals`/`req.protocol` are not
   reported as network misses):
   ```cpp
   if (auto webserver = webserverObjectMember(ot, prop)) return *webserver;
   const std::vector<std::string> webserverNames =
       webserverObjectMemberNames(ot.name);
   if (!webserverNames.empty() &&
       ot.name.rfind("$webserver:", 0) == 0) {
       reportMemberMiss("webserver value", prop, span, webserverNames);
       return Type::unknown();
   }
   ```
   (Copy the exact `reportMemberMiss` spelling from the network block
   below it.) LSP doc/completion routing can add `webserverObjectMemberDoc`
   / `webserverObjectMemberNames` beside the network calls in
   `src/cli/src/commands/lsp/...` — optional, docs only.
5. **Codegen declarations** — `src/codegen/src/detail/generator/generator.h`,
   next to `emitNetworkMemberCall`:
   ```cpp
   std::string emitwebserverSettingsMemberCall(const CallExpr& c, const MemberExpr& m);
   std::string emitwebserverSettingsMemberRead(const MemberExpr& m);
   std::string emitwebserverLocalsIndexRead(const IndexExpr& ix);
   std::string emitwebserverLocalsAssign(const AssignmentExpr& a, const MemberExpr& m);
   std::string emitwebserverLocalsIndexWrite(const AssignmentExpr& a);
   std::string webserverSettingValueText(const Expr& value);
   std::string webserverRenderOptionsText(const CallExpr& call);
   std::string webserverSettingsArg(const CallExpr& call, std::size_t index);
   std::string webserverSettingsArgs(const CallExpr& call);
   ```
   and `src/codegen/src/detail/generator/generator_state.h` (next to
   `needsNetwork_`): `bool needswebserverSettings_ = false;`
6. **Codegen dispatch** — `src/codegen/src/emit/std/common.cpp`:
   - in `emitMemberCall` (member-call dispatch), BEFORE the
     `isNetworkObject(...)` line:
     `if (std::string view = emitwebserverSettingsMemberCall(c, m); !view.empty()) return view;`
   - in `emitBuiltinMember`, BEFORE the `emitNetworkMemberRead` line:
     `if (std::string view = emitwebserverSettingsMemberRead(m); !view.empty()) return view;`
7. **Assignment dispatch** —
   - `src/codegen/src/emit/control/assignment.cpp`, `emitAssignment`'s
     `MemberExpr` branch, BEFORE `emitModelAssign`:
     `if (std::string w = emitwebserverLocalsAssign(a, member); !w.empty()) return w;`
   - same file, `IndexExpr` branch, BEFORE `emitFamilyIndexWrite`:
     `if (std::string w = emitwebserverLocalsIndexWrite(a); !w.empty()) return w;`
   - index READ: at the top of the body of `Generator::emitIndexExpr`
     (find it with `grep "Generator::emitIndexExpr" src/codegen/src`):
     `if (std::string r = emitwebserverLocalsIndexRead(ix); !r.empty()) return r;`
8. **Include emission** — `src/codegen/src/support/type/module_writer.cpp`,
   next to the `needsNetwork_` line:
   `if (needswebserverSettings_) out += "#include \"cp3/std/network/webserver/settings.hpp\"\n";`
   (plus the same line in `src/codegen/src/emit/decl/program.cpp` next to
   the `needsNetwork_` include, mirroring the `needsPath_` pattern there).
9. **Native row** — `src/std/network/codegen/native.hpp`, `kNetworkNativePackage`
   include dirs: add `"src/std/network/webserver/settings/include"` next to
   `"src/std/network/include"`. No TU, no selector, no link change.
10. **Dispatcher glue** (server side, where the request chain runs): construct
    one scope per request — this is what makes `req.ip`/`req.protocol`,
    `res.locals`, and `res.render` work:
    ```cpp
    webserver::RequestScope scope(app, req, res);  // app: webserverApp
    ```
    It sets the current app, applies "trust proxy", and resets `res.locals`.
    A bare `createServer` chain without it leaves `req.ip` as the socket
    address, `req.protocol` as `"http"`, and `res.render` throws.
11. **Registry** — no new module: the surface rides on the existing
    `std/network` import (`src/modules/src/support/std_registry.cpp`
    unchanged). Programs `import { createServer } from "std/network"` as
    today; `app` is an `webserverApp` (or a base app forwarding to one).

## Limitations (deliberate)

- **No bundled template language.** With no engine registered for an
  extension, render falls back to trivial `{{ key }}` interpolation
  (`renderTrivialTemplate`): no escaping, no control flow — a divergence
  from webserver (which errors). Register `app.engine(ext, fn)` for real
  engines; `engineFromCallback` adapts webserver-style callback engines.
- **Setting values are scalars** (string/number/boolean/null/string[]).
  Function-valued settings are unsupported, so the webserver "trust proxy"
  function form and arbitrary CIDR/hostname trust lists are not
  implemented: strings map to `loopback`/`linklocal`/`uniquelocal`/
  `trust all`/hop-count, anything else behaves as untrusted (`false`).
- **`app.get` returns `null` for unset settings** (webserver: `undefined`).
- **`app.render` is synchronous** (`string` return; no callback form).
- **`res.locals` / current-app are thread-local side channels** (the
  shared `HttpRequest`/`HttpResponse` classes cannot gain members from
  this family). `req.ip` resolution is stored back on the request;
  `req.protocol` is carried in the documented `x-cp3-expr-protocol`
  header slot. Detached async work that outlives `RequestScope` renders
  without an app and throws.
- **Locals compound assignment** (`app.locals.n += 1`, `app.locals.n++`)
  is not lowered (plain `=` only); read-modify-write instead.
- **Settings are not thread-safe for concurrent mutation**; configure
  before `listen` (the scaled server shares one app across loop threads).
- Inside an `app.engine` handler, read options with `opts.getOr("key")`
  (the handler's options parameter is typed `Locals` at runtime; member
  syntax `opts.key` is not lowered there).
- Sema types `app.get`/`app.set` values as ambient; the scalar restriction
  is enforced at codegen (`reportTypeError` for object/array-typed
  values other than string arrays).
