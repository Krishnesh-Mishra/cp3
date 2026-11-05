# webserver `req` helpers — wiring (parent session applies; this dir is self-contained)

## Files (all under `src/std/network/webserver/request/**`)

| File | Role |
|---|---|
| `include/cp3/std/network/webserver/request.hpp` | Runtime, header-only: `cp3::runtime::webserver::request::{protocol,secure,hostname,subdomains,baseUrl,originalUrl,path,fresh,stale,xhr,is,accepts,acceptsEncodings,acceptsCharsets}`, `webserverRequestOptions`, `webserverRequest` view |
| `sema.cpp` | `Analyzer::webserverRequestMember(recv, prop)`, `webserverRequestMemberNames()`, `webserverRequestMemberDoc(prop)`, `iswebserverRequestMember(prop)` |
| `codegen.cpp` | `Generator::emitwebserverRequestCall(c, m)`, `Generator::emitwebserverRequestMember(m)` |
| `native.hpp` | `kNetworkwebserverRequestNativePackage` for import `"std/network/webserver/request"` (header-only, no TU) |
| `WIRING.md` | This file |

## Export surface

Receiver is the existing `$network:HttpRequest` marker — no new receiver type.
Properties: `protocol`, `secure`, `hostname`, `subdomains`, `baseUrl`,
`originalUrl`, `path`, `fresh`, `stale`, `xhr`. Methods:
`is(...types)`, `accepts(...)`, `acceptsEncodings(...)`, `acceptsCharsets(...)`
(all return `string`; `""` = webserver `false`/no match).

## Wiring lines (apply in shared files — NOT touched by this change)

1. **Sema declarations** — `src/sema/src/detail/analyzer.h`, after the
   `networkObjectMember` declaration (~line 379):
   ```cpp
   // std/network/webserver/request (src/std/network/webserver/request/sema.cpp)
   std::optional<Type> webserverRequestMember(const Type& recv,
                                            const std::string& prop);
   ```
   and in `src/sema/include/cp3/std_surface.hpp` (next to
   `networkObjectMemberNames`/`networkObjectMemberDoc`):
   ```cpp
   std::vector<std::string> webserverRequestMemberNames();
   StdMemberDoc webserverRequestMemberDoc(const std::string& prop);
   bool iswebserverRequestMember(const std::string& prop);
   ```
2. **Sema dispatch** — `src/sema/src/checks/expr/member_check.cpp`, in the
   `$network:HttpRequest` arm (where `networkObjectMember` is consulted,
   ~line 184): try `webserverRequestMember(ot, prop)` FIRST (before
   `networkObjectMember`), so the new members type and unknown members
   report CP3305 with `webserverRequestMemberNames()` as candidates.
3. **Codegen declarations** — `src/codegen/src/detail/generator/generator.h`,
   next to `emitDateCall` (~line 708):
   ```cpp
   std::string emitwebserverRequestCall(const CallExpr& c, const MemberExpr& m);
   std::string emitwebserverRequestMember(const MemberExpr& m);
   ```
4. **Codegen state/include** — `src/codegen/src/detail/generator/generator_state.h`:
   add `bool needswebserverRequest_ = false;` (next to `needsNetwork_`).
   `src/codegen/src/support/type/module_writer.cpp`: emit
   `#include "cp3/std/network/webserver/request.hpp"` when set (next to the
   `needsNetwork_` line ~150). `src/codegen/src/emit/decl/program.cpp`:
   merge `n.webserverRequest |= inner.needswebserverRequest_;` (next to the
   `needsDate_` merge ~326).
5. **Codegen dispatch** — `src/codegen/src/emit/std/common.cpp`:
   in `emitBuiltinCall` (member-call path) BEFORE `isNetworkObject(...)`
   (~line 272): `if (std::string e = emitwebserverRequestCall(c, m); !e.empty()) return e;`
   in `emitBuiltinMember` BEFORE `emitNetworkMemberRead` (~line 336):
   `if (std::string e = emitwebserverRequestMember(m); !e.empty()) return e;`
6. **Build** — add the three TUs (`sema.cpp`, `codegen.cpp`) to whatever list
   compiles `src/std/date/sema.cpp`/`codegen.cpp` (one per-module unit each),
   and `#include "src/std/network/webserver/request/native.hpp"` in the
   native-packages table unit + register
   `&kNetworkwebserverRequestNativePackage` (next to the `std/network` row).
   Include dir for generated code comes from that row
   (`src/std/network/webserver/request/include`).
7. **Tests** — add `tests/cases/stdlib/webserver_request_*` cases mirroring the
   `map_*`/`date_*` style; not written here (code-only, no build/test run).

## Limitations

- **No TLS in the beta-one server** (D70): `protocol`/`secure` default to
  `http`/`false`. `https` requires `webserverRequestOptions{tls=true}`
  (hand-written C++ / future socket layer) or `trustProxy` +
  `X-Forwarded-Proto`. Generated code uses default options.
- **`trustProxy` is off by default**: `X-Forwarded-Proto`/`X-Forwarded-Host`
  are ignored unless the options say otherwise (same default as webserver
  without `trust proxy`). Generated code cannot set it yet.
- **Router state is not carried by `HttpRequest`**: `baseUrl`/`originalUrl`
  come from `webserverRequestOptions` (default `""`/path). The router layer
  (child agent) must construct `webserverRequestOptions` per mount; until
  then both read as the request path.
- **`originalUrl` has no query string by default**: the server splits `?`
  off into `HttpRequest::query` (insertion-ordered Map, duplicate keys lost)
  before dispatch, so the raw URL cannot be reconstructed losslessly.
  Pass `options.originalUrl` to keep it exact.
- **`fresh` compares against caller-provided response headers**
  (`options.responseEtag`/`responseLastModified`), because webserver compares
  against `res` headers which are not yet sent at read time here. Unset =>
  not fresh. HTTP-date parsing covers IMF-fixdate, RFC 850 and asctime.
- **`is`/`accepts*` return `""` for no-match** instead of webserver's
  `false` (cp3 has no `string | false` widening on this surface); test with
  `=== ""` or `!x`.
- **Variadic args are `string[]`** in sema (`is(...types: string[])`); a
  single array argument is NOT flattened — pass spread or separate args.
- **Header names**: server lower-cases on fill; the lookup retries the
  caller's spelling, but a hand-built `HttpRequest` with mixed-case keys
  not in the exact spelling still misses.
