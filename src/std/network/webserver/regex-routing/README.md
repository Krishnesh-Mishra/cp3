# webserver regex / array routing (`std/network`)

Isolated delivery for the webserver routing surface: RegExp paths, arrays of
paths, `*` wildcards, `caseSensitive` + `strict` (per-route and app
settings), and percent-decoded `req.params`.

**Scope rule honoured:** everything in this directory lives under
`src/std/network/webserver/regex-routing/**`. No shared file was touched.
This README is the contract for the integrator: apply the wiring lines
below (each is a small, additive diff), then build.

---

## Files

| File | Role |
| --- | --- |
| `regex_routing.hpp` | Runtime header: `RoutingOptions`, `RoutingSettings`, `EffectiveOptions`, `PathSpec`, `MatchResult`, matcher + app-settings-registry API. Only needs `cp3/core/regex.hpp` and `cp3/std/network.hpp`. |
| `runtime_regex_routing.cpp` | Implementation: PCRE2-through-core-engine matcher, string-pattern compiler, named-group parser, percent decoder, settings registry (mutex-protected, keyed by `HttpServer*`). Own TU; links with the existing network native package. |
| `sema_fragment.cpp` | Sema paste fragment for `src/std/network/codegen/sema.cpp` (field rows + `tryInferNetworkMemberCall` hook for the 3-argument options form). Not a TU. |
| `codegen_fragment.cpp` | Codegen paste fragment for `src/std/network/codegen/codegen_methods.cpp` (`Generator::regexRoutingPathText` / `regexRoutingOptionsCtor` + dispatch hunks in `emitNetworkMemberCall`). Not a TU. |
| `README.md` | This file: export surface, wiring lines, limitations. |

---

## Export surface (CP3 source)

```cp3
import { createServer } from "std/network";

const app = createServer();

// String paths (unchanged) + new path kinds — all five verbs + any
app.get("/users/:id", (req, res) => { ... });          // existing :name
app.get(/\/commits\/(\d+)/, (req, res) => { ... });   // RegExp path
app.get(["/a", "/b"], (req, res) => { ... });         // array of paths
app.get(["/a", /\/b\/(\d+)/], (req, res) => { ... }); // mixed array

// Wildcards (existing rules, now case/strict aware)
app.get("/files/*", (req, res) => { ... });            // rest of path -> "*"
app.get("/*", (req, res) => { ... });                  // any path

// Per-route options (override the app settings for this route)
app.get("/Admin", { caseSensitive: true, strict: true }, (req, res) => { ... });

// App settings (webserver names; set BEFORE registering routes — settings
// snapshot at registration, exactly like webserver compiles the path)
app.set("case sensitive routing", true);
app.enable("strict routing");       // == set("strict routing", true)
app.disable("strict routing");      // == set("strict routing", false)
app.get("strict routing");          // -> boolean
app.enabled("strict routing");      // -> boolean
app.disabled("strict routing");     // -> boolean
```

`req.params` gains: `"0"`, `"1"`, … for RegExp unnamed groups (group 1 is
`"0"`), group names for `(?<name>…)` groups, and the existing `":name"` /
`"*"`. Every value is percent-decoded once (`%20` → space, `%2F` → `/`,
malformed `%` stays literal, `+` stays `+`).

### Runtime (C++) surface

Namespace `cp3::runtime::network::regex_routing`:

- Types: `RoutingOptions{optional<bool> caseSensitive, strict}`,
  `RoutingSettings{caseSensitive, strict}`, `EffectiveOptions`,
  `PathSpec{Kind::{String,RegExp,Array}, …}` (+ `makeString/makeRegex/makeArray`),
  `MatchResult{matched, params, index}`.
- Functions: `resolveEffective`, `decodeParam` / `decodeURIComponent`,
  `matchStringPattern`, `matchRegexPath`, `matchArrayPath`, `matchPath`,
  `flattenPathArray`, `compileStringPattern`, `regexGroupNames`,
  `isRoutingSettingKey`, `setAppSetting`, `enableAppSetting`,
  `disableAppSetting`, `getAppSetting`, `appSettingsFor`.

---

## WIRING (apply in order; every hunk is additive)

### 1. Runtime bridge — `src/std/network/include/cp3/std/network.hpp`

Add a forward declaration and, inside `HttpServer` (next to `any`/`use`),
the members below. Defined out-of-line so the header needs no include of
the regex-routing header (no cycle). The `get`/`post`/… names MUST be
spelled per-verb so codegen can emit the member name.

```cpp
namespace cp3::runtime::network::regex_routing {
struct PathSpec; struct RoutingOptions; struct RoutingSettings;
}  // namespace cp3::runtime::network::regex_routing
```

```cpp
// In class HttpServer, after `any(...)`:
template <class F>
void get(const regex_routing::PathSpec& path, F handler) const {
    addRouteSpecImpl("GET", path, regex_routing::RoutingOptions{},
                     detail::makeRoute(std::move(handler)));
}
template <class F>
void get(const regex_routing::PathSpec& path,
         const regex_routing::RoutingOptions& options, F handler) const {
    addRouteSpecImpl("GET", path, options,
                     detail::makeRoute(std::move(handler)));
}
// ... repeat for post/put/del/patch/head/options/any (method strings
// "POST"/"PUT"/"DELETE"/"PATCH"/"HEAD"/"OPTIONS"/"*") and the
// (path, options, handler) form of each. Existing string overloads stay.

// App routing settings (forward to the registry in regex_routing.hpp):
void setRoutingSetting(const std::string& key, bool value) const;
bool getRoutingSetting(const std::string& key) const;
void enableRoutingSetting(const std::string& key) const;
void disableRoutingSetting(const std::string& key) const;
// private:
void addRouteSpecImpl(const char* method,
                      const regex_routing::PathSpec& path,
                      const regex_routing::RoutingOptions& options,
                      detail::RouteFn handler) const;
```

### 2. Route storage + registration — `runtime_server_internal.hpp` / `runtime_server.cpp`

In `detail::ServerImpl::Route` append (keep `pattern` for the existing
string overloads):

```cpp
regex_routing::PathSpec spec;              // hmm: needs the full type
regex_routing::EffectiveOptions eff;       // snapshot at registration
bool useSpec = false;                      // true for PathSpec overloads
```

Because the headers must not include the webserver header either, instead
store the two effective booleans and the pre-compiled string regex:

```cpp
bool useSpec = false;
bool specIsRegex = false;
std::string specString;          // string pattern (when !specIsRegex)
std::string specRegexSource;     // regex source (when specIsRegex)
std::string specRegexFlags;
std::vector<std::string> specArrayStrings;   // array-of-strings
std::vector<std::string> specArrayRegexSources; // parallel, "" = string
bool effCaseSensitive = false, effStrict = false;
```

Then in `runtime_server.cpp` implement `addRouteSpecImpl`: snapshot
`regex_routing::appSettingsFor(impl_.get())`, call
`regex_routing::resolveEffective(settings, options)`, fill the fields
above (flatten arrays, split string/regex leaves), push the route. The
four `setRoutingSetting`/`getRoutingSetting` members forward to
`regex_routing::setAppSetting(impl_.get(), key, value)` /
`regex_routing::getAppSetting(impl_.get(), key)`.

### 3. Dispatcher — `runtime_server_dispatch.cpp`

Replace the `pathMatches(...)` call in `runChain` with:

```cpp
std::vector<std::pair<std::string,std::string>> captures;
bool ok = false;
if (route.useSpec) {
    EffectiveOptions eff{route.effCaseSensitive, route.effStrict};
    // rebuild a runtime PathSpec from the stored fields, or match
    // directly:
    //   specIsRegex  -> regex_routing::matchRegexPath(
    //       Regex::compile(route.specRegexSource, route.specRegexFlags),
    //       ctx.request.path, false);
    //   array        -> regex_routing::matchArrayPath(...);
    //   else         -> regex_routing::matchStringPattern(
    //       route.specString, ctx.request.path, eff);
    // captures = result.params; ok = result.matched;
} else {
    ok = pathMatches(route.pattern, ctx.request.path, &captures);
}
if (!ok) continue;
```

(Keep `pathMatches` for the original string overloads; the new matcher
handles `useSpec` routes, including the decoder. Include
`regex_routing.hpp` from `runtime_server_dispatch.cpp`.)

### 4. Native package — `src/std/network/codegen/native.hpp`

Add to `runtimeSources`: `"src/std/network/webserver/regex-routing/runtime_regex_routing.cpp"`,
and to the include dirs: `"src/std/network/webserver/regex-routing"` (the
cpp includes `regex_routing.hpp` by bare name). Marker: it already
follows the `"HttpServer("` row. Regex lowering sets `needsRegex_`
already (D48), so no new link row is needed.

### 5. Sema — `src/std/network/codegen/sema.cpp`

Apply `sema_fragment.cpp`: (a) helper types `regexRoutingPathType()` /
`regexRoutingOptionsType()`; (b) widen the verb loop's first parameter to
`regexRoutingPathType()` and add the six settings fields; (c) uncomment
and paste the `(path, options, handler)` hook at the top of
`tryInferNetworkMemberCall`; (d) doc swaps (optional).

### 6. Codegen — `src/std/network/codegen/codegen_methods.cpp` + `generator.h`

Apply `codegen_fragment.cpp`: (a) include `cp3/core/regex.hpp`; (b)
`Generator::regexRoutingPathText` / `regexRoutingOptionsCtor` members +
`generator.h` declarations; (c) the dispatch hunk at the top of
`emitNetworkMemberCall`. **One correction on paste:** in the PathSpec
branches emit `networkMemberName(receiver, m.property)` instead of a
hardcoded `.get(` (each verb has its own C++ overload from step 1).
Also map the settings verbs in the hunk: `set`/`enable`/`disable` → emit
`.setRoutingSetting(key, true/false)`; `get`/`enabled`/`disabled` →
`.getRoutingSetting(key)` (with a `!` for `disabled`).

---

## Limitations (deliberate, with reasons)

1. **RegExp paths use `test` (search) semantics**, matching webserver
   (`route.test(path)`). Use `matchRegexPath(..., anchored=true)` for
   whole-path matching; the codegen emits the search form. Anchor
   explicitly in the pattern (`/^\/commits\/(\d+)$/`) for exact routes.
2. **`g`/`y` flags are stripped** for RegExp paths: the core engine's
   `lastIndex` lives in the shared regex value, so a stateful regex would
   walk state across requests (webserver has the same footgun and resets
   per request; CP3 strips rather than mutating the user's value).
3. **`caseSensitive` / `strict` do not apply to RegExp paths** (webserver
   parity): case comes from the `i` flag; trailing-slash strictness is
   webservered by anchoring (`/?$`).
4. **App settings snapshot at registration**: changing
   `case sensitive routing` after `app.get(...)` does not affect routes
   already registered (webserver compiles the path with the settings at
   registration time). Set settings first.
5. **Encoded literals**: matching runs on the raw path; only captured
   values are decoded. A route spelled `"/a%20b"` matches the literal
   bytes `%20` in the request path, and `req.params` for `/a%20b c`
   decodes to `"b c"`-style values once.
6. **Array nesting is one level by default** at codegen (nested array
   literals are traversed recursively by `flattenPathArray`, but a spread
   element inside a path array is a compile-time error, see the fragment).
7. **Unknown setting keys are ignored** (`setAppSetting` returns false;
   `getAppSetting` returns false). Other app settings (`view engine`,
   `etag`, …) remain unimplemented, as before.
8. **No `app.route()` chaining, `app.use(path, …)` path mounting, or
   mergeParams semantics** — out of scope for this delivery; `use` stays
   the existing `(handler)` middleware form.
