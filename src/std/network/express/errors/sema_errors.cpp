// =============================================================================
// src/std/network/express/errors/sema_errors.cpp - sema spec tables.
// STANDALONE: no Analyzer dependency, so this TU compiles without touching
// src/std/network/codegen/sema.cpp. The lead copies the rows below into
// sema.cpp (exact wiring in README.md). Marker names match the codegen
// helpers in codegen_errors.cpp.
// =============================================================================
#include "sema_errors.hpp"

namespace cp3::runtime::network::express::errors::sema {

const char* const kHttpError = "$network:HttpError";
const char* const kNextError = "$network:NextError";

std::vector<std::string> httpErrorFields() {
    return {"status", "statusCode", "expose", "message", "code", "stack"};
}

std::vector<std::string> sendFileOptionNames() {
    return {"root", "dotfiles", "etag", "lastModified", "maxAge", "headers",
            "fallthrough", "acceptRanges"};
}

std::vector<std::string> staticOptionNames() {
    return {"root", "dotfiles", "etag", "lastModified", "maxAge", "fallthrough",
            "index", "indexName", "redirect", "headers", "acceptRanges"};
}

std::vector<std::string> dotfilesValues() { return {"allow", "deny", "ignore"}; }

bool isSendFileOption(const std::string& name) {
    for (auto const& n : sendFileOptionNames()) if (n == name) return true;
    return false;
}
bool isStaticOption(const std::string& name) {
    for (auto const& n : staticOptionNames()) if (n == name) return true;
    return false;
}
bool isDotfilesValue(const std::string& v) {
    for (auto const& n : dotfilesValues()) if (n == v) return true;
    return false;
}

/// Arity rules the lead should enforce in sema.cpp:
///   * error handler: exactly 4 params (err, req, res, next)
///   * next(): 0 args; next(err): 1 arg (Error | HttpError | unknown)
///   * res.sendFile(path, options?): 1 required + 1 optional object
///   * express.static(root, options?): 1 required + 1 optional object
bool checkErrorHandlerArity(std::size_t params, std::string& diag) {
    if (params != 4) { diag = "error handler takes exactly 4 arguments (err, req, res, next)"; return false; }
    return true;
}
bool checkNextArity(std::size_t args, std::string& diag) {
    if (args > 1) { diag = "next() takes at most one argument (the error)"; return false; }
    return true;
}
bool checkSendFileArity(std::size_t args, std::string& diag) {
    if (args < 1 || args > 2) { diag = "sendFile(path, options?) takes one or two arguments"; return false; }
    return true;
}

}  // namespace cp3::runtime::network::express::errors::sema
