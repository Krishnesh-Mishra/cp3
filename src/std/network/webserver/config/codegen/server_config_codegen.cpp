// =============================================================================
// src/std/network/webserver/config/codegen/server_config_codegen.cpp
// Codegen text builders for createServer({...}). Pure string builders (no
// Generator dependency); the shared generator walks the options object
// literal, collects one expression per field (README §Wiring step 5), and
// calls these. Stringly-typed fields accept a string literal OR a string[]
// array literal, which the integrator lowers with joinStrings({...}).
// OWNERSHIP: src/std/network/webserver/config/** only.
// =============================================================================
#include "cp3/std/network/webserver/config.hpp"

namespace cp3::runtime::network::webserver::config::sema_codegen {
namespace {

std::string orDefault(const std::string& expr, const std::string& fallback) {
    return expr.empty() ? fallback : expr;
}

}  // namespace

std::string serverConfigCallText(const std::string& threadsExpr,
                                 const std::string& maxConnectionsExpr,
                                 const std::string& idleTimeoutExpr,
                                 const std::string& maxWriteBufferExpr,
                                 const std::string& timeoutExpr,
                                 const std::string& tlsExpr,
                                 const std::string& corsExpr) {
    return "webserver::config::makeServerConfig(" +
           orDefault(threadsExpr, "1.0") + ", " +
           orDefault(maxConnectionsExpr, "0.0") + ", " +
           orDefault(idleTimeoutExpr, "0.0") + ", " +
           orDefault(maxWriteBufferExpr, "0.0") + ", " +
           orDefault(timeoutExpr, "0.0") + ", " +
           orDefault(tlsExpr, "webserver::config::TlsOptions()") + ", " +
           orDefault(corsExpr, "webserver::config::CorsOptions()") + ")";
}

std::string tlsOptionsText(const std::string& certExpr, const std::string& keyExpr,
                           const std::string& chainExpr, const std::string& caExpr,
                           const std::string& minProtocolExpr) {
    return "webserver::config::TlsOptions{true, " +
           orDefault(certExpr, "std::string()") + ", " +
           orDefault(keyExpr, "std::string()") + ", " +
           orDefault(chainExpr, "std::string()") + ", " +
           orDefault(caExpr, "std::string()") + ", " +
           orDefault(minProtocolExpr, "std::string(\"TLSv1.2\")") + "}";
}

std::string corsOptionsText(const std::string& originExpr, const std::string& methodsExpr,
                            const std::string& headersExpr, const std::string& credentialsExpr,
                            const std::string& maxAgeExpr) {
    return "webserver::config::CorsOptions{true, " +
           orDefault(originExpr, "std::string(\"*\")") + ", " +
           orDefault(methodsExpr, "std::string(\"GET,POST,PUT,PATCH,DELETE,OPTIONS\")") + ", " +
           orDefault(headersExpr, "std::string()") + ", " +
           orDefault(credentialsExpr, "false") + ", " +
           orDefault(maxAgeExpr, "0.0") + "}";
}

std::string createServerCallText(const std::string& configExpr) {
    return "webserver::config::createServerFromConfig(" +
           orDefault(configExpr, "webserver::config::ServerConfig()") + ")";
}

}  // namespace cp3::runtime::network::webserver::config::sema_codegen
