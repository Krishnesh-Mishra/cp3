// =============================================================================
// src/std/network/dns/sema.cpp - `std/network` dns.lookup(hostname) typing.
//
// OWNERSHIP: the std/network/dns family (src/std/network/dns/**). This unit
// owns the whole dns.lookup surface, previously scattered across
// src/std/network/codegen/sema.cpp (the `networkMember` "lookup" row, the
// `tryInferNetworkCall` "lookup" arm and the `tryInferNetworkMemberCall`
// `dns.lookup` arm). The three spellings are preserved, now dispatched to
// the hooks below (WIRING.md):
//
//   * named import   lookup(hostname)        -> tryInferDnsCall
//   * default import dns.lookup(hostname)    -> tryInferDnsMemberCall
//   * namespace      network.lookup(host)    -> dnsMember (via
//                      stdNamespaceMember's "network" row) +
//                      tryInferDnsMemberCall for the call site
//
// All three type as (hostname: string) => Promise<string>: the runtime
// (dns/runtime/runtime_dns_lookup.cpp) resolves the first address as text
// over libuv getaddrinfo, the same resolution fetch drives
// (runtime_fetch_io.cpp). A call with anything but exactly one argument is
// a program error here (kArityMismatch), not a codegen skip: the runtime
// has no hostname to resolve.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

/// `(hostname: string) => Promise<string>`: the one documented shape.
Type dnsLookupType() {
    return Type::function({Type::string()}, promiseOf(Type::string()));
}

}  // namespace

std::optional<Type> Analyzer::dnsMember(const std::string& prop) {
    // `network.lookup(hostname)` / `dns.lookup(hostname)` member reads.
    if (prop == "lookup") return dnsLookupType();
    return std::nullopt;
}

bool Analyzer::tryInferDnsCall(CallExpr& call, const Identifier& id,
                               Type& out) {
    // Named import: `lookup(hostname)`.
    if (id.name != "lookup") return false;
    inferArgs(call);
    if (call.arguments.size() != 1 || !call.arguments.front()) {
        report(diagcode::kArityMismatch, anchorSpan(call.span),
               "dns.lookup(hostname) takes exactly one argument");
    }
    out = promiseOf(Type::string());
    return true;
}

bool Analyzer::tryInferDnsMemberCall(CallExpr& call, MemberExpr& m,
                                     Type& out) {
    // `dns.lookup(hostname)` / `network.lookup(hostname)`: the ambient
    // std/network default-import bindings. A user value named `dns` /
    // `network` shadows the std surface and is never claimed here.
    if (m.property != "lookup" || m.object == nullptr ||
        m.object->kind != NodeKind::Identifier) {
        return false;
    }
    const auto& id = static_cast<const Identifier&>(*m.object);
    if (id.name != "dns" && id.name != "network") return false;
    Binding binding;
    if (!lookup(id.name, binding) || !binding.isAmbient) return false;
    inferArgs(call);
    if (call.arguments.size() != 1 || !call.arguments.front()) {
        report(diagcode::kArityMismatch, anchorSpan(call.span),
               "dns.lookup(hostname) takes exactly one argument");
    }
    out = promiseOf(Type::string());
    return true;
}

std::vector<std::string> dnsMemberNames() { return {"lookup"}; }

StdMemberDoc dnsMemberDoc(const std::string& member, TypeSystem) {
    if (member == "lookup") {
        return {"lookup(hostname: string): Promise<string>",
                "Resolves `hostname` over libuv getaddrinfo (the same "
                "resolution `fetch` drives) and fulfils with the first "
                "address as text. Rejects with an `Error` when the "
                "hostname cannot be resolved. This is the resolution a "
                "`connectTcp` host goes through."};
    }
    return {};
}

}  // namespace cp3
