// =============================================================================
// src/modules/src/detail/std_registry.h - the known `std/...` module surfaces (PRIVATE).
//
// OWNERSHIP: modules module (src/modules/**). Never included cross-module.
//
// Definitions live in src/support/std_registry.cpp. The table is derived from
// docs/standard-library/04-imported-std-modules.md; a specifier outside the
// table is CP3601. Named imports are validated against this surface (e.g.
// `Mutex` is deliberately NOT part of `std/collections`).
// =============================================================================
#pragma once

#include <string_view>

#include "detail/export_surface.h"

namespace cp3::modules {

// The export surface of `specifier`, or nullptr when it names no std module.
// The returned pointer is stable for the lifetime of the process.
const ExportSurface* stdModuleSurface(std::string_view specifier);

}  // namespace cp3::modules
