// =============================================================================
// src/std/network/webserver/config-apps/native_config_apps.hpp
// Native-package row snippet for this subtree (D68). NOT a shared-file edit:
// the integrator pastes the row below into src/std/network/codegen/native.hpp
// (see README.md). Include roots + TU + marker are additive only; the family
// reuses the std/network native libs (uSockets/libuv) already in the row.
// =============================================================================
#pragma once

// Include roots to add (next to the settings + middleware roots):
//   "src/std/network/webserver/config-apps/include"
//   "src/std/network/webserver/settings/include"          (settings family)
//   "src/std/network/webserver/middleware/include"        (middleware family)
//
// Runtime TU to add to runtimeSources:
//   "src/std/network/webserver/config-apps/config_apps.cpp"
//
// Marker (same index as runtimeSources) to add:
//   "makeConfiguredApp(|configapps::applyServerConfig(|configapps::ConfiguredApp("
//
// Sema/codegen TUs: the sema/codegen fragments (sema_config_apps.cpp,
// codegen_config_apps.cpp) are NOT native TUs: like the routing family's
// fragments they join src/sema/CMakeLists.txt (cp3_sema_sources) and
// src/codegen/CMakeLists.txt (cp3_codegen_sources) when their splice lines
// are applied (README.md wiring steps 1-2).
