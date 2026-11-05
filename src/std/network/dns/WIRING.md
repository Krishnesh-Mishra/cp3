# std/dns lookup — wiring (integrator checklist)

Solo scope: `src/std/network/dns/**` only. Everything below is a line the
integrator adds to (or deletes from) a shared file. Nothing here was run,
built, or committed.

`dns.lookup(hostname)` was previously scattered across
`src/std/network/codegen/{sema,codegen}.cpp`, `src/std/network/codegen/native.hpp`,
`src/std/network/include/cp3/std/network.hpp` (`dnsResolve`) and
`src/std/network/runtime/runtime_dns.cpp`. This family moves it into its
own `dns/` directory with a new runtime namespace
(`cp3::runtime::network::dns`), new sema hooks and new codegen hooks, so
the old `lookup` arms and the old TU can be deleted once this is wired
(step 10). Nothing is renamed on the user surface.

## Files in this family

| File | Role |
|---|---|
| `include/cp3/std/network/dns.hpp` | Runtime header: `cp3::runtime::network::dns::lookup(host) -> Promise<std::string>`. |
| `runtime/runtime_dns_lookup.cpp` | Runtime TU: one-shot libuv `uv_getaddrinfo` on the shared loop, first address as text, `Error` rejections. |
| `sema.cpp` | `Analyzer::dnsMember`, `Analyzer::tryInferDnsCall`, `Analyzer::tryInferDnsMemberCall`, `dnsMemberNames`, `dnsMemberDoc`. |
| `codegen.cpp` | `Generator::emitDnsIdentifier`, `Generator::emitDnsCall`. |
| `native.hpp` | Paste fragment for `kNetworkNativePackage` (not included anywhere). |
| `WIRING.md` | This checklist. |

## Export surface

User surface (all three spellings already exported from `std/network`;
no registry change needed — `src/modules/src/support/std_registry.cpp`
already lists `lookup` under `std/network`, and
`src/sema/src/checks/type/std_check.cpp` already hints
`import { lookup } from "std/network"`):

- `lookup(hostname: string): Promise<string>` — named import
  (`import { lookup } from "std/network"`).
- `dns.lookup(hostname: string): Promise<string>` — default import
  (`import dns from "std/network"`).
- `network.lookup(hostname: string): Promise<string>` — default import
  (`import network from "std/network"`).

Runtime surface (`cp3/std/network/dns.hpp`, `cp3::runtime::network::dns`):

- `Promise<std::string> lookup(const std::string& host)` — first address
  (text form) of the libuv resolution; rejects with an `Error` object.

## Wiring lines (shared files, in order)

1. **CMake (sema)** — append to `cp3_sema_sources` in `src/sema/CMakeLists.txt`,
   next to `../std/network/codegen/sema.cpp`:
   `../std/network/dns/sema.cpp`
2. **CMake (codegen)** — append to `cp3_codegen_sources` in
   `src/codegen/CMakeLists.txt`, next to
   `../std/network/codegen/codegen.cpp`:
   `../std/network/dns/codegen.cpp`
3. **Sema declarations** — `src/sema/src/detail/analyzer.h`, next to the
   `tryInferNetworkCall` declaration (line ~395):
   ```cpp
   // --- std/network dns.lookup (src/std/network/dns/sema.cpp) --------------
   std::optional<Type> dnsMember(const std::string& prop);
   bool tryInferDnsCall(CallExpr& call, const Identifier& id, Type& out);
   bool tryInferDnsMemberCall(CallExpr& call, MemberExpr& m, Type& out);
   ```
   and in `src/sema/include/cp3/std_surface.hpp`, next to
   `networkObjectMemberNames` (docs/completion only, optional):
   ```cpp
   // std/network dns (src/std/network/dns/sema.cpp)
   std::vector<std::string> dnsMemberNames();
   StdMemberDoc dnsMemberDoc(const std::string& member, TypeSystem ts = TypeSystem::TypeScript);
   ```
4. **Sema dispatch — named import** — `src/sema/src/checks/call/call_check.cpp`,
   in the ambient-bBuiltin chain, immediately BEFORE
   `if (tryInferNetworkCall(call, id, builtin))` (line ~65):
   ```cpp
   if (tryInferDnsCall(call, id, builtin)) {
       return withType(call, builtin);
   }
   ```
5. **Sema dispatch — member call** — same file,
   `Analyzer::tryInferMemberCall`, immediately BEFORE the
   `tryInferNetworkMemberCall` line (line ~142):
   ```cpp
   // std/network dns.lookup: `dns.lookup(host)` / `network.lookup(host)`.
   if (tryInferDnsMemberCall(call, m, out)) return true;
   ```
6. **Sema dispatch — namespace member read** —
   `src/sema/src/checks/type/std_check.cpp`, `stdNamespaceMember`, in the
   `if (ns == "network")` row (line ~48):
   ```cpp
   if (ns == "network") {
       if (auto dns = dnsMember(prop)) return dns;
       return networkMember(prop);
   }
   ```
   (Optional LSP docs: add a `network` row to `stdNamespaceMemberDoc` /
   `stdNamespaceMemberNames` that falls back to `dnsMemberDoc` /
   `dnsMemberNames` for `lookup`.)
7. **Codegen declarations** —
   `src/codegen/src/detail/generator/generator.h`, next to
   `emitNetworkCall` (line ~699):
   ```cpp
   // std/network dns.lookup (src/std/network/dns/codegen.cpp)
   std::string emitDnsCall(const CallExpr& c, const MemberExpr& m);
   std::string emitDnsIdentifier(const CallExpr& c, const std::string& name);
   ```
   and `src/codegen/src/detail/generator/generator_state.h`, next to
   `needsNetwork_` (line ~173):
   ```cpp
   bool needsDns_ = false;  // std/network dns -> cp3/std/network/dns.hpp include
   ```
8. **Codegen dispatch** — `src/codegen/src/emit/std/common.cpp`:
   - in `emitMemberCall`, immediately BEFORE the
     `if (isNamespaceObject(m.object.get(), "network"))` line (line ~249):
     ```cpp
     if (std::string dns = emitDnsCall(c, m); !dns.empty()) return dns;
     ```
     (This hook is what lowers the `dns.` / `network.` member spellings.
     Previously only the `network.` spelling reached `emitNetworkCall`
     through that `isNamespaceObject("network")` arm; `dns.lookup` was
     typed by sema but had no codegen arm — no test covers it, both
     existing cases use the named import.)
   - in `emitUnresolvedCall` (src/std/network/codegen/codegen.cpp,
     line ~510), immediately BEFORE `std::string net = emitNetworkIdentifier(c, name);`:
     ```cpp
     if (std::string dns = emitDnsIdentifier(c, name); !dns.empty()) return dns;
     ```
     (This is the named-import fallthrough; if the old `lookup` arm in
     `emitNetworkIdentifier` is deleted in step 10, this line is the only
     `lookup` lowering left.)
9. **Include emission** —
   `src/codegen/src/support/type/module_writer.cpp`, next to the
   `needsNetwork_` line (line ~157):
   ```cpp
   if (needsDns_) out += "#include \"cp3/std/network/dns.hpp\"\n";
   ```
   plus the same line in `src/codegen/src/emit/decl/program.cpp` next to
   the `needsNetwork_` include (line ~211), mirroring the `needsNetwork_`
   propagation at line ~339 (`n.dns |= inner.needsDns_;`, with a
   `bool dns = false;` on the assembly struct next to `n.network`).
10. **Native row** — `src/std/network/codegen/native.hpp`,
    `kNetworkNativePackage` (see `dns/native.hpp` for the paste fragment):
    - include dirs: add `"src/std/network/dns/include"` next to
      `"src/std/network/include"`.
    - `runtimeSources`: add
      `"src/std/network/dns/runtime/runtime_dns_lookup.cpp"`.
    - selectors: add `"dns::lookup("` parallel to the new TU.
11. **Retire the old dns** (same commit as steps 1–10, or the next one):
    delete the three `lookup` arms in
    `src/std/network/codegen/sema.cpp` (`networkMember` "lookup" row,
    `tryInferNetworkCall` "lookup" arm, `tryInferNetworkMemberCall`
    `dns.lookup` arm), the `lookup` arm in
    `emitNetworkIdentifier` (src/std/network/codegen/codegen.cpp),
    `"src/std/network/runtime/runtime_dns.cpp"` + `"dnsResolve("` from
    `kNetworkNativePackage`, the `dnsResolve` declaration in
    `src/std/network/include/cp3/std/network.hpp`, and
    `src/std/network/runtime/runtime_dns.cpp` itself. Verified by grep:
    nothing outside those files references `dnsResolve`. Until step 11
    lands, both runtimes coexist (distinct namespaces/symbols), so the
    wiring order above does not matter.

## Limitations (deliberate)

- **First address only.** `dns.lookup` returns the first `addrinfo` of
  the resolution (AF_UNSPEC + SOCK_STREAM hints, like fetch). Callers
  that need every address (e.g. `connectTcp` trying each family in
  turn) must call the libuv `uv_getaddrinfo` themselves or a future
  `dns.lookupAll` must be added; this family does not guess.
- **One function, one hostname.** No port, no options object, no
  callback form (`dns.lookup(host, cb)` is not offered); the promise is
  the only surface, matching the ROADMAP row.
- **Rejection text is a summary, not the resolver's.** Messages carry
  the hostname plus `uv_strerror` detail where libuv provides it; there
  is no `code`/`errno` field on the `Error` (that would be a new error
  shape, not the v0.2 `Error` contract).
- **No cancellation.** A started `uv_getaddrinfo` cannot be cancelled
  (ROADMAP §I-known-limits); abandoning the promise still settles it
  on the loop and releases the retain.
- **IPv6 text is the libuv rendering** (`uv_ip6_name`, compressed form);
  there is no zone-id (`%eth0`) handling.
- The `network.lookup` member-read type comes from `dnsMember` via
  step 6; without step 6 the call site still types through
  `tryInferDnsMemberCall` (step 5) but a bare `network.lookup` read is
  ambient.

## Verification (not run — code only, per solo scope)

- `import { lookup } from "std/network"; const ip = await lookup("localhost");`
  compiles, emits `::cp3::runtime::network::dns::lookup(...)`, selects
  only `runtime_dns_lookup.cpp`, and fulfils with `127.0.0.1` or `::1`.
- `lookup()` and `lookup("a", "b")` are `kArityMismatch` in sema.
- `await lookup("nonexistent.invalid")` rejects; `catch (e)` binds an
  `Error` whose `message` names the hostname.
- A program that never calls `lookup` does not compile/link
  `runtime_dns_lookup.cpp` (selector) and does not include dns.hpp
  (`needsDns_`).
