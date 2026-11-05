// =============================================================================
// src/std/network/express/settings/native.hpp - `std/network` (Express
// settings) native backend notes.
//
// OWNERSHIP: the std/network/express/settings family
// (src/std/network/express/settings/**). This family is HEADER-ONLY and
// rides on the existing `std/network` native row: there is no new
// NativePackage, no runtime TU and no vendored library. The single native
// change the integrator makes is ONE extra include root on
// `kNetworkNativePackage` (src/std/network/codegen/native.hpp), so the
// generated `#include "cp3/std/network/express/settings.hpp"` resolves:
//
//   "src/std/network/express/settings/include",
//
// added to the include-dirs list next to "src/std/network/include". The
// header itself includes only the standard library plus the cp3 core /
// network headers the network row already serves; ... the selector regexes
// of the network row need no change (no new TU is ever selected).
// =============================================================================
#pragma once

// Intentionally empty: see the header comment. The integrator's one-line
// native change is the include root documented above; nothing in this file
// is included by the native-package table.
