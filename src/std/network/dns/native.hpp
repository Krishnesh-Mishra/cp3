// =============================================================================
// src/std/network/dns/native.hpp - `std/network` dns.lookup native backend
// notes.
//
// OWNERSHIP: the std/network/dns family (src/std/network/dns/**). This file
// is a paste fragment, not a TU: it is NOT included by the native-package
// table. The integrator applies the three changes below to
// `kNetworkNativePackage` in src/std/network/codegen/native.hpp (full
// checklist in dns/WIRING.md, steps 8-9):
//
//   1. include dirs - add ONE root so the generated
//      `#include "cp3/std/network/dns.hpp"` resolves, next to
//      "src/std/network/include":
//
//        "src/std/network/dns/include",
//
//   2. runtimeSources - add the dns TU, next to the other runtime TUs:
//
//        "src/std/network/dns/runtime/runtime_dns_lookup.cpp",
//
//   3. selectors - add the marker parallel to the new TU (the lists are
//      position-parallel), so only programs whose generated code contains
//      `dns::lookup(` compile and link it:
//
//        "dns::lookup("
//
// Backend: libuv, already linked by the std/network row (vendor/third_party/libuv,
// -luv on Windows with ws2_32/userenv/iphlpapi/ole32/dbghelp/synchronization,
// -pthread/-ldl/-lrt on Linux). No new vendored library, no new link flag.
//
// REPLACING THE OLD DNS TU. The previous dns runtime
// (src/std/network/runtime/runtime_dns.cpp), its "dnsResolve(" selector and
// its codegen arm in src/std/network/codegen/codegen.cpp become dead once
// dns/codegen.cpp is wired (nothing else references dnsResolve - verified
// by grep over src/). The integrator should delete all three together with
// the old "lookup" arms in src/std/network/codegen/sema.cpp, so no program
// can emit `dnsResolve(` and select the old TU. Until then both TUs
// compile cleanly side by side (distinct namespaces and symbols), so the
// wiring order does not matter.
// =============================================================================
#pragma once

// Intentionally empty: see the header comment. Nothing in this file is
// included by the native-package table.
