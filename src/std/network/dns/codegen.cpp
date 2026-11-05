// =============================================================================
// src/std/network/dns/codegen.cpp - `std/network` dns.lookup(hostname)
// lowering.
//
// OWNERSHIP: the std/network/dns family (src/std/network/dns/**). Lowers
// the three spellings onto cp3/std/network/dns.hpp:
//
//   lookup(hostname)              -> ::cp3::runtime::network::dns::lookup(hostname)
//   dns.lookup(hostname)          -> ::cp3::runtime::network::dns::lookup(hostname)
//   network.lookup(hostname)      -> ::cp3::runtime::network::dns::lookup(hostname)
//
// The hooks are called from the shared dispatch units (WIRING.md):
// `emitDnsIdentifier` from the unresolved-call fallthrough (where the
// previous `emitNetworkIdentifier` "lookup" arm lived) and `emitDnsCall`
// from the member-call dispatch next to the `network` namespace arm, each
// ahead of the network hook so they win. Both check the receiver is an
// ambient std/network binding, so a user value named `dns` / `network` /
// `lookup` is never mis-lowered. The previous lowering emitted
// ::cp3::runtime::network::dnsResolve (runtime/runtime_dns.cpp); the
// integrator retires that TU once this hook is wired (dns/native.hpp).
//
// Each path sets needsNetwork_ (the std/network include),
// needsDns_ (the dns.hpp include) and needsEventLoop_ (resolution runs on
// the runtime loop, so `main` must drive it even without an `await`).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kDnsNs = "::cp3::runtime::network::dns::";

}  // namespace

std::string Generator::emitDnsIdentifier(const CallExpr& c,
                                          const std::string& name) {
    // Named import: `lookup(hostname)`.
    if (name != "lookup") return std::string();
    needsNetwork_ = true;
    needsDns_ = true;
    needsEventLoop_ = true;
    if (c.arguments.empty() || !c.arguments.front()) {
        // Sema already reported the arity error (dns/sema.cpp); this is
        // the defensive path for a unit that reaches codegen without it.
        reportCodegenError(c, diagcode::kArityMismatch,
                           "dns.lookup(hostname) takes exactly one argument");
        return "0.0";
    }
    return std::string(kDnsNs) + "lookup(" + expr(*c.arguments.front()) + ")";
}

std::string Generator::emitDnsCall(const CallExpr& c, const MemberExpr& m) {
    // `dns.lookup(hostname)` / `network.lookup(hostname)`: only the ambient
    // std/network default-import bindings; a user binding of the same name
    // is left to its own lowering ("" = not ours).
    if (m.property != "lookup" || !m.object ||
        m.object->kind != NodeKind::Identifier) {
        return std::string();
    }
    const auto& id = static_cast<const Identifier&>(*m.object);
    const std::string receiver = id.name;
    if (receiver != "dns" && receiver != "network") return std::string();
    if (const auto binding = lookup(receiver); binding && !binding->isAmbient) {
        return std::string();
    }
    needsNetwork_ = true;
    needsDns_ = true;
    needsEventLoop_ = true;
    if (c.arguments.empty() || !c.arguments.front()) {
        // Sema already reported the arity error (dns/sema.cpp); this is
        // the defensive path for a unit that reaches codegen without it.
        reportCodegenError(c, diagcode::kArityMismatch,
                           "dns.lookup(hostname) takes exactly one argument");
        return "0.0";
    }
    return std::string(kDnsNs) + "lookup(" + expr(*c.arguments.front()) + ")";
}

}  // namespace cp3
