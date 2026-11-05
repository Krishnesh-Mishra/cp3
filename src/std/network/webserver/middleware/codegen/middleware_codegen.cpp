// =============================================================================
// src/std/network/webserver/middleware/codegen/middleware_codegen.cpp
// Codegen helpers for `app.use(...)` / `next(...)` lowering.
// Pure text builders (no Generator dependency), so this TU compiles
// standalone and the shared generator can call it with one line
// (README.md wiring). Emitted C++ targets this engine's public API:
//   app.use(mw)                 -> engine.use(mw)
//   app.use("/api", mw)         -> engine.use("/api", mw)
//   app.use("/api", errMw)      -> engine.use("/api", errMw)  (4-arg lambda)
//   next() / next("route")      -> next() / next.route()
//   next(err)                   -> next.error(err)
// OWNERSHIP: src/std/network/webserver/middleware/** only.
// =============================================================================
#include "cp3/std/network/webserver/middleware.hpp"

namespace cp3::runtime::network::webserver::sema_codegen {

std::string useCallText(const std::string& mountExpr, const std::string& handlerExpr,
                        bool hasMount, bool errorHandler) {
    // errorHandler is carried for the integrator's diagnostics; the emitted
    // call is identical - arity (3 vs 4 params) selects the layer kind in
    // MiddlewareEngine::use(const std::string&, F) via is_invocable.
    (void)errorHandler;
    if (hasMount) return "engine.use(" + mountExpr + ", " + handlerExpr + ")";
    return "engine.use(" + handlerExpr + ")";
}

std::string nextCallText(const std::string& nextExpr, const std::string& argExpr,
                         bool isRouteToken, bool hasArg) {
    if (isRouteToken) return nextExpr + ".route()";
    if (hasArg) return nextExpr + ".error(" + argExpr + ")";
    return nextExpr + "()";
}

}  // namespace cp3::runtime::network::webserver::sema_codegen
