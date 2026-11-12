// =============================================================================
// src/modules/src/support/std_registry.cpp - the known `std/...` module surfaces.
//
// OWNERSHIP: modules module (src/modules/**). Implements detail/std_registry.h.
//
// Source of truth: docs/standard-library/04-imported-std-modules.md. Only
// specifiers listed here resolve; named/default imports are validated against
// the surface. `std/collections` deliberately exports Map/Set but NOT Mutex
// (concurrency primitives are language-level, not importable). A specifier
// that left the table (D74: `std/events`) is reported with its builtin
// replacement by `removedModuleMessage` instead of the generic CP3601 text.
// =============================================================================
#include "detail/std_registry.h"

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/std_modules.hpp"

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
    {"std/io", true, {}},
    {"std/date", true, {}},
    {"std/process", true, {}},
    {"std/path", true, {}},
    {"std/os", true, {}},
    {"std/crypto", true, {}},
    {"std/network", true, {"fetch", "lookup", "WebSocket", "WebSocketServer", "createServer", "tls", "Headers", "Request", "Response", "FormData", "AbortSignal", "static", "cors", "bodyLimit", "Router", "webserver", "cookieParser", "createTcpServer", "connectTcp", "TcpServer", "TcpSocket", "createUdpSocket", "UdpSocket", "createTlsServer", "connectTls", "TlsServer", "TlsSocket"}},
    {"std/env", false, {"env"}},
    {"std/collections", false, {"Map", "Set", "LRUCache", "Queue", "Stack", "PriorityQueue", "RingBuffer", "BitSet", "MultiMap", "ConcurrentMap"}},
    {"std/buffer", false, {"Buffer"}},
    {"std/url", true, {"URL", "URLSearchParams"}},
    {"std/time", true, {}},
    {"std/hash", true, {}},

    {"std/text", false, {"TextEncoder", "TextDecoder"}},
    {"std/readline", true, {}},

    // Every std module also publishes its type names at "<module>/types", so
    // annotation positions can import them as types without pulling in the
    // runtime surface (`import { Connection } from "std/network/types"`).
    // The type entries mirror their runtimes exactly. std/collections/types
    // is deliberately absent: the collections family is TS-style ambient
    // (like Map/Set in TypeScript), so the extra facade would be dead weight.
    {"std/network/types", true, {"WebSocket", "WebSocketServer", "Headers", "Request", "Response", "FormData", "AbortSignal", "Router", "TlsSocket", "TlsServer", "TcpSocket", "TcpServer", "UdpSocket"}},
    {"std/buffer/types", false, {"Buffer"}},
    {"std/url/types", true, {"URL", "URLSearchParams"}},
    {"std/date/types", true, {"Date"}},
    {"std/text/types", false, {"TextEncoder", "TextDecoder"}},
};

// Specifiers that were once std modules and are now language builtins (D74).
// The loader reports the replacement, so an old import fails with a pointer to
// the builtin instead of the generic "cannot find module".
struct RemovedModuleDef {
    const char* specifier;
    const char* message;
};

const RemovedModuleDef kRemovedModules[] = {
    {"std/events",
     "std/events is not a module: EventEmitter is a language builtin, no "
     "import is needed (see language-spec 14)"},
    {"std/log",
     "std/log was removed before v0.2: use console.log/console.info for "
     "logging output"},
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

const char* removedModuleMessage(std::string_view specifier) {
    for (const RemovedModuleDef& def : kRemovedModules) {
        if (std::string_view(def.specifier) == specifier) return def.message;
    }
    return nullptr;
}

}  // namespace cp3::modules

namespace cp3 {

// Public surface (cp3/std_modules.hpp): the specifier list the LSP completion
// offers inside `import ... from "|"`. Table order is the docs order.
std::vector<std::string> stdModuleSpecifiers() {
    std::vector<std::string> specifiers;
    for (const modules::StdModuleDef& def : modules::kStdModules) {
        specifiers.emplace_back(def.specifier);
    }
    return specifiers;
}

std::vector<std::string> stdModuleExportNames(const std::string& specifier) {
    std::vector<std::string> names;
    const modules::ExportSurface* surface = modules::stdModuleSurface(specifier);
    if (surface != nullptr) {
        names.assign(surface->named.begin(), surface->named.end());
    }
    return names;
}

}  // namespace cp3
