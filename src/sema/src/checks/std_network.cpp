// =============================================================================
// src/sema/src/checks/std_network.cpp - `std/network` surface typing.
//
// OWNERSHIP: the network wave (std/network). One per-module check unit; the
// rest of sema never needs to change for this module (the dispatch row already
// exists in std_check.cpp).
//
// STUB: returns nullopt until the network wave lands, so `network.*`
// member/property access keeps the permissive ambient shape it had before this
// file existed. The documented surface is named imports, not a namespace
// (docs/standard-library/04 §network):
//   import { fetch, createServer, WebSocket, WebSocketServer } from "std/network";
// which the wave types where those bindings are resolved (call/construct
// checking), using this table for the namespace-shaped corner if it needs one.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>

namespace cp3 {

std::optional<Type> Analyzer::networkMember(const std::string& prop) {
    (void)prop;
    return std::nullopt;
}

}  // namespace cp3
