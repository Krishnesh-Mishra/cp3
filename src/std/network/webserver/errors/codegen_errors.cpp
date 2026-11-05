// =============================================================================
// src/std/network/webserver/errors/codegen_errors.cpp - codegen emission helpers.
// STANDALONE: pure string builders (no Generator dependency). The lead calls
// these from codegen.cpp / codegen_methods.cpp (wiring in README.md). The
// emit* callbacks are the Generator::expr() spelling for one sub-expression.
// =============================================================================
#include "codegen_errors.hpp"

namespace cp3::runtime::network::webserver::errors::codegen {
namespace {

std::string ns() { return "::cp3::runtime::network::webserver::errors::"; }

}  // namespace

// --- option object literals ---------------------------------------------------
// Each builder takes (key -> emitted C++ text) pairs already lowered by the
// caller, so this file never needs the AST. Unknown keys are dropped by the
// caller (sema diagnostics) exactly like the existing cors() lowering.

std::string sendFileOptionsText(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string body;
    for (auto const& [k, v] : fields) {
        if (k == "root") body += ".root = " + v + "; ";
        else if (k == "dotfiles") body += ".dotfiles = " + v + "; ";
        else if (k == "etag") body += ".etag = " + v + "; ";
        else if (k == "lastModified") body += ".lastModified = " + v + "; ";
        else if (k == "maxAge") body += ".maxAge = " + v + "; ";
        else if (k == "headers") body += ".headers = " + v + "; ";
        else if (k == "fallthrough") body += ".fallthrough = " + v + "; ";
        else if (k == "acceptRanges") body += ".acceptRanges = " + v + "; ";
    }
    return "[&] { " + ns() + "SendFileOptions o; " + body + "return o; }()";
}

std::string staticOptionsText(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string body;
    for (auto const& [k, v] : fields) {
        if (k == "root") body += ".root = " + v + "; ";
        else if (k == "dotfiles") body += ".dotfiles = " + v + "; ";
        else if (k == "etag") body += ".etag = " + v + "; ";
        else if (k == "lastModified") body += ".lastModified = " + v + "; ";
        else if (k == "maxAge") body += ".maxAge = " + v + "; ";
        else if (k == "fallthrough") body += ".fallthrough = " + v + "; ";
        else if (k == "index") body += ".index = " + v + "; ";
        else if (k == "indexName") body += ".indexName = " + v + "; ";
        else if (k == "redirect") body += ".redirect = " + v + "; ";
        else if (k == "headers") body += ".headers = " + v + "; ";
        else if (k == "acceptRanges") body += ".acceptRanges = " + v + "; ";
    }
    return "[&] { " + ns() + "StaticOptions o; " + body + "return o; }()";
}

// --- call spellings -----------------------------------------------------------
std::string sendFileCallText(const std::string& res, const std::string& req,
                             const std::string& path, const std::string& options) {
    // res.sendFile(path, options) -> errors::sendFile(res, req, path, options)
    // The existing HttpResponse::sendFile(path) overload is unchanged.
    return ns() + "sendFile(" + res + ", " + req + ", " + path +
           (options.empty() ? std::string{} : ", " + options) + ")";
}

std::string staticCallText(const std::string& dir, const std::string& options) {
    return ns() + "makeStatic(" + dir +
           (options.empty() ? std::string{} : ", " + options) + ")";
}

std::string errorHandlerAdaptText(const std::string& handler) {
    // 4-arg handler -> existing 3-arg app.onError() slot.
    return ns() + "asErrorHandler(" + handler + ")";
}

std::string nextCallText(const std::string& next, const std::string& arg) {
    // next() / next(err): arg=="" means the no-argument continuation.
    return next + "(" + arg + ")";
}

std::string httpErrorText(int status, const std::string& message) {
    return ns() + "httpError(" + std::to_string(status) + ", " + message + ")";
}

}  // namespace cp3::runtime::network::webserver::errors::codegen
