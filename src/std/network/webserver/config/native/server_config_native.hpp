// =============================================================================
// src/std/network/webserver/config/native/server_config_native.hpp
// Native-package row snippet for this subtree (D68). NOT a shared-file edit:
// the integrator pastes the row below into src/std/network/codegen/native.hpp
// (see README.md §Wiring). Include root + TUs + marker are additive only.
// =============================================================================
#pragma once

// Include root to add:
//   "src/std/network/webserver/config/include"
// (serves cp3/std/network/webserver/config.hpp; the family ABI tree
//  src/std/network/include already covers cp3/std/network.hpp)
//
// Runtime TUs to add to runtimeSources (same index as the marker below):
//   "src/std/network/webserver/config/runtime/runtime_server_config.cpp"
//   "src/std/network/webserver/config/sema/server_config_sema.cpp"
//   "src/std/network/webserver/config/codegen/server_config_codegen.cpp"
//
// Marker (same index as runtimeSources) to add:
//   "webserver::config::"
//
// Reuses the std/network native libs (uSockets/libuv) already in the row -
// no new third-party dependency. Define -DCP3_UWS_SSL in the row's defines
// ONLY after uSockets is rebuilt with an SSL backend (README §Wiring step 1);
// without it the TLS path fails closed with ERR_TLS_BACKEND_UNAVAILABLE.
