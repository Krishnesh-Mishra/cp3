// =============================================================================
// src/std/network/webserver/config/sema/server_config_sema.cpp
// Sema helpers for createServer({ tls, cors, threads, ... }) option keys +
// literal checks. Pure logic only (no Analyzer dependency), so this TU
// compiles standalone; the shared analyzer calls it with one line each
// (README.md wiring). Diagnostic strings are returned, not reported.
// OWNERSHIP: src/std/network/webserver/config/** only.
// =============================================================================
#include "cp3/std/network/webserver/config.hpp"

namespace cp3::runtime::network::webserver::config::sema_codegen {

bool isKnownCreateServerOption(const std::string& key) {
    return key == "threads" || key == "tls" || key == "cors" ||
           key == "maxConnections" || key == "idleTimeout" ||
           key == "maxWriteBuffer" || key == "timeout";
}

bool isKnownTlsOption(const std::string& key) {
    return key == "cert" || key == "key" || key == "chain" || key == "ca" ||
           key == "minProtocol";
}

bool isKnownCorsOption(const std::string& key) {
    return key == "origin" || key == "methods" || key == "headers" ||
           key == "credentials" || key == "maxAge";
}

std::string checkMinProtocolLiteral(const std::string& value) {
    if (value.empty() || value == "TLSv1.2" || value == "1.2" ||
        value == "TLSv1.3" || value == "1.3") {
        return "";
    }
    return "tls.minProtocol must be \"TLSv1.2\" or \"TLSv1.3\" (TLSv1.0/1.1 and "
           "SSLv3 are refused)";
}

}  // namespace cp3::runtime::network::webserver::config::sema_codegen
