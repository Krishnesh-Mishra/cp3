// =============================================================================
// modules/export_surface.h - a module's export surface (PRIVATE header).
//
// OWNERSHIP: modules module (src/modules/**). Never included cross-module.
//
// The surface is what importers may legally bind: a default export flag plus
// the set of named exports, including what `export * from "./x.cp3"` chains
// re-export. Built by export_surface.cpp; the std table in std_registry.cpp
// and the loader's import validation both consume it.
// =============================================================================
#pragma once

#include <set>
#include <string>

namespace cp3::modules {

struct ExportSurface {
    bool hasDefault = false;
    std::set<std::string> named;
};

}  // namespace cp3::modules
