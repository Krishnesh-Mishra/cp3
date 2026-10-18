// =============================================================================
// src/codegen/src/emit/std/network.cpp - `std/network` lowering.
//
// OWNERSHIP: the network wave (std/network). One per-module lowering unit; the
// namespace-shaped dispatch row already exists in emit/std/common.cpp
// (emitBuiltinCall), so the rest of codegen never needs to change for the
// namespace corner of this module.
//
// STUB: returns "" until the network wave lands, which makes the caller report
// CP3002 (not implemented) exactly as before this file existed. The documented
// surface is named imports (`fetch`, `createServer`, `WebSocket`,
// `WebSocketServer`; docs/standard-library/04 §network), whose bare-call and
// construct forms the wave hooks where identifiers/constructors are lowered;
// it lowers onto the runtime surface in cp3/std/network.hpp and sets
// `needsNetwork_` so buildSource adds the include.
// =============================================================================
#include "detail/generator.h"

#include <string>

namespace cp3 {

using namespace detail;

std::string Generator::emitNetworkCall(const CallExpr& c, const MemberExpr& m) {
    (void)c;
    (void)m;
    return std::string();
}

}  // namespace cp3
