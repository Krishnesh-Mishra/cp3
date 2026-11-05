// =============================================================================
// src/std/network/webserver/middleware/sema/middleware_sema.cpp
// Sema helpers for `app.use(...)` overloads + `next(...)` forms.
// Pure logic only (no Analyzer dependency), so this TU compiles standalone
// and the shared analyzer can call it with one line (README.md wiring).
// OWNERSHIP: src/std/network/webserver/middleware/** only.
// A 3-param handler is normal middleware, a 4-param handler is error
// middleware; `next` accepts (), ("route") or (err).
// =============================================================================
#include "cp3/std/network/webserver/middleware.hpp"

namespace cp3::runtime::network::webserver::sema_codegen {

bool isNextRouteToken(const std::string& literalValue) {
    return literalValue == "route";
}

std::string validateMountLiteral(const std::string& mount) {
    if (mount.empty() || mount == "*" || mount == "/") return "";
    if (mount.front() != '/') {
        return "app.use mount path must start with '/' (or be \"*\" or \"/\")";
    }
    return "";
}

}  // namespace cp3::runtime::network::webserver::sema_codegen
