// =============================================================================
// modules/std_registry.cpp - the known `std/...` module surfaces.
//
// OWNERSHIP: modules module (src/modules/**). Implements std_registry.h.
//
// Source of truth: docs/standard-library/04-imported-std-modules.md. Only
// specifiers listed here resolve; named/default imports are validated against
// the surface. `std/collections` deliberately exports Map/Set but NOT Mutex
// (concurrency primitives are language-level, not importable).
// =============================================================================
#include "std_registry.h"

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>

namespace cp3::modules {

namespace {

struct StdModuleDef {
    const char* specifier;
    bool hasDefault;
    std::initializer_list<const char*> named;
};

const StdModuleDef kStdModules[] = {
    {"std/console", true, {}},
    {"std/json", true, {}},
    {"std/math", true, {}},
    {"std/fs", true, {}},
    {"std/date", true, {}},
    {"std/process", true, {}},
    {"std/path", true, {}},
    {"std/os", true, {}},
    {"std/crypto", true, {}},
    {"std/network", false, {"fetch", "WebSocket", "WebSocketServer", "createServer"}},
    {"std/events", false, {"EventEmitter"}},
    {"std/env", false, {"env"}},
    {"std/collections", false, {"Map", "Set"}},
    {"std/buffer", false, {"Buffer"}},
};

const std::map<std::string, ExportSurface>& stdTable() {
    static const std::map<std::string, ExportSurface> table = [] {
        std::map<std::string, ExportSurface> result;
        for (const StdModuleDef& def : kStdModules) {
            ExportSurface surface;
            surface.hasDefault = def.hasDefault;
            for (const char* name : def.named) surface.named.insert(name);
            result.emplace(def.specifier, std::move(surface));
        }
        return result;
    }();
    return table;
}

}  // namespace

const ExportSurface* stdModuleSurface(std::string_view specifier) {
    const auto& table = stdTable();
    const auto it = table.find(std::string(specifier));
    return it == table.end() ? nullptr : &it->second;
}

}  // namespace cp3::modules
