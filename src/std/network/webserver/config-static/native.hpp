// =============================================================================
// src/std/network/webserver/config-static/native.hpp - `std/network`
// (createServer config: static + body limits) native backend notes.
//
// OWNERSHIP: the std/network/webserver/config-static family
// (src/std/network/webserver/config-static/**). This family is HEADER-ONLY
// and rides on the existing `std/network` native row: there is no new
// NativePackage, no runtime TU and no vendored library. The runtime pieces
// it composes already belong to other rows of that package:
//
//   * makeStaticMiddleware / makeBodyLimitMiddleware / HttpServer
//       -> src/std/network/runtime/runtime_web.cpp (network row, existing)
//   * body_parse parsers (makeJsonMiddleware / makeUrlEncodedMiddleware /
//     parseLimitText) -> src/std/network/webserver/body-parse/
//     runtime_body_parse.cpp (body-parse family's own row, WIRING.md there)
//   * webserver::SettingValue (trustProxy carrier) -> settings.hpp,
//     header-only (settings family's include root, WIRING.md there)
//
// The integrator's native change is therefore ONLY include roots on
// kNetworkNativePackage (src/std/network/codegen/native.hpp): add
// "src/std/network/webserver/config-static/include" next to
// "src/std/network/include", plus the body-parse and settings include roots
// their own WIRING.md files already require. No source entry, no marker:
// config_static.hpp is included by generated code only (WIRING.md step 7),
// never by a native TU, so no selector regex changes.
// =============================================================================
#pragma once

// Intentionally empty: see the header comment. Nothing in this file is
// included by the native-package table.
