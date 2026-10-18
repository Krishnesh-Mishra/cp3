// =============================================================================
// src/runtime/src/std/network.cpp - `std/network` implementation TU.
//
// OWNERSHIP: the network wave. Selected (compiled + linked, with uSockets +
// libuv) only when a program imports "std/network" - see
// src/cli/src/detail/native_std_network.hpp.
//
// Backend notes: libuv v1.53.0 + uWebSockets v20.80.0 static (third_party/**,
// D70). Every TU that sees the uWS headers is compiled with -DUWS_NO_ZLIB
// (D68 wires it); TLS is off, so beta-one fetch is http:// only.
// =============================================================================
#include "cp3/std/network.hpp"

// STUB: the network wave lands the definitions here.
