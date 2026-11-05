// =============================================================================
// src/std/network/webserver/request/codegen.cpp - webserver `req` helper lowering.
//
// OWNERSHIP: the webserver/request family (src/std/network/webserver/request/**).
// One per-module lowering unit; the dispatch arm lives in
// emit/std/common.cpp (emitBuiltinCall / emitBuiltinMember), so the rest of
// codegen never needs to change for this module.
//
// The receiver is the existing `std/network` marker `$network:HttpRequest`
// (typed in src/std/network/codegen/sema.cpp). This unit intercepts BEFORE
// the network lowering, which would emit a plain `(req).<member>` that does
// not exist on HttpRequest:
//
//   req.protocol / req.secure / req.hostname / req.subdomains / req.baseUrl /
//   req.originalUrl / req.path / req.fresh / req.stale / req.xhr
//     -> ::cp3::runtime::webserver::request::<fn>(req)
//   req.is("json", "html") / req.accepts(...) / req.acceptsEncodings(...) /
//   req.acceptsCharsets(...)
//     -> ::cp3::runtime::webserver::request::<fn>(req, std::vector<std::string>{...})
//
// `needswebserverRequest_` makes buildSource add the request.hpp include. A
// member outside the surface returns ""; sema has already rejected it as
// CP3305, so that path is defensive only.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kHttpRequest = "$network:HttpRequest";
constexpr const char* kNs = "::cp3::runtime::webserver::request::";

const char* propertyFn(const std::string& name) {
    if (name == "protocol") return "protocol";
    if (name == "secure") return "secure";
    if (name == "hostname") return "hostname";
    if (name == "subdomains") return "subdomains";
    if (name == "baseUrl") return "baseUrl";
    if (name == "originalUrl") return "originalUrl";
    if (name == "path") return "path";
    if (name == "fresh") return "fresh";
    if (name == "stale") return "stale";
    if (name == "xhr") return "xhr";
    return nullptr;
}

const char* methodFn(const std::string& name) {
    if (name == "is") return "is";
    if (name == "accepts") return "accepts";
    if (name == "acceptsEncodings") return "acceptsEncodings";
    if (name == "acceptsCharsets") return "acceptsCharsets";
    return nullptr;
}

}  // namespace

std::string Generator::emitwebserverRequestCall(const CallExpr& c,
                                              const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    if (effectiveType(*m.object).kind != TypeKind::Object ||
        effectiveType(*m.object).name != kHttpRequest) {
        return std::string();
    }
    const char* fn = methodFn(m.property);
    if (fn == nullptr) return std::string();
    needswebserverRequest_ = true;
    const std::vector<std::string> args = emitPlainArgs(c);
    return std::string(kNs) + fn + "(" + expr(*m.object) +
           ", std::vector<std::string>{" + joinStrings(args, ", ") + "})";
}

std::string Generator::emitwebserverRequestMember(const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    if (effectiveType(*m.object).kind != TypeKind::Object ||
        effectiveType(*m.object).name != kHttpRequest) {
        return std::string();
    }
    const char* fn = propertyFn(m.property);
    if (fn == nullptr) return std::string();
    needswebserverRequest_ = true;
    return std::string(kNs) + fn + "(" + expr(*m.object) + ")";
}

}  // namespace cp3
