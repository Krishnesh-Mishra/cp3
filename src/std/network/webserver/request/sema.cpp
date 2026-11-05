// =============================================================================
// src/std/network/webserver/request/sema.cpp - webserver `req` helper typing.
//
// OWNERSHIP: the webserver/request family (src/std/network/webserver/request/**).
// One per-module check unit; the core dispatch (member_check.cpp's
// $network:HttpRequest arm) forwards here, so the rest of sema never needs to
// change for this module.
//
// The receiver is the existing `std/network` marker `$network:HttpRequest`:
// these helpers EXTEND that marker, they do not introduce a new receiver
// type. A member outside the tables below stays CP3305, reported by the
// caller with this module's names as candidates.
//
// Surface (webserver `req`):
//   protocol, hostname, baseUrl, originalUrl, path: string (properties)
//   subdomains: string[] (property)
//   fresh, stale, xhr, secure: boolean (properties)
//   is(...types: string[]): string            ("" = no match, webserver false)
//   accepts(...types: string[]): string       ("" = none acceptable)
//   acceptsEncodings(...encodings: string[]): string
//   acceptsCharsets(...charsets: string[]): string
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// The sema marker of the receiver these helpers extend (std/network sema.cpp).
constexpr const char* kHttpRequest = "$network:HttpRequest";

Type stringArray() { return Type::array(Type::string()); }
Type stringRestToString() {
    return Type::function({stringArray()}, Type::string());
}

}  // namespace

// Properties: (name, type, signature, documentation). Method rows carry a
// function type; both live in one lookup so member typing, the candidate
// list and the editor documentation can never drift apart.
namespace {

struct RequestRow {
    const char* name;
    Type type;
    const char* signature;
    const char* body;
};

const RequestRow kRequestRows[] = {
    {"protocol", Type::string(), "protocol: string",
     "The request protocol: \"https\" when the socket is TLS-terminated or "
     "(with trust proxy) X-Forwarded-Proto says so, otherwise \"http\"."},
    {"secure", Type::boolean(), "secure: boolean",
     "True when the request arrived over TLS (protocol == \"https\")."},
    {"hostname", Type::string(), "hostname: string",
     "The Host header without the port (X-Forwarded-Host wins under trust "
     "proxy); \"\" when no Host header is present."},
    {"subdomains", stringArray(), "subdomains: string[]",
     "Hostname labels left of the last two (configurable offset): "
     "\"a.b.example.com\" -> [\"a\", \"b\"]"},
    {"baseUrl", Type::string(), "baseUrl: string",
     "The mount point of the router handling this request (\"\" at the "
     "root). Set by the router layer."},
    {"originalUrl", Type::string(), "originalUrl: string",
     "The full original request URL. Defaults to the request path (the "
     "query string is split off by the server before dispatch)."},
    {"path", Type::string(), "path: string",
     "The URL path without its query string."},
    {"fresh", Type::boolean(), "fresh: boolean",
     "True when the response ETag / Last-Modified still matches the "
     "request's If-None-Match / If-Modified-Since (GET/HEAD only)."},
    {"stale", Type::boolean(), "stale: boolean",
     "The inverse of `fresh`."},
    {"xhr", Type::boolean(), "xhr: boolean",
     "True when X-Requested-With is \"XMLHttpRequest\"."},
    {"is", stringRestToString(), "is(...types: string[]): string",
     "The first offered type matching the request Content-Type, or \"\" "
     "when none match. Extensions (\"json\") and wildcards (\"text/*\") work."},
    {"accepts", stringRestToString(), "accepts(...types: string[]): string",
     "The best offered type for the request Accept header (q-values "
     "honoured), or \"\" when the client accepts none of them."},
    {"acceptsEncodings", stringRestToString(),
     "acceptsEncodings(...encodings: string[]): string",
     "The best offered encoding for Accept-Encoding, or \"\" when none is "
     "acceptable."},
    {"acceptsCharsets", stringRestToString(),
     "acceptsCharsets(...charsets: string[]): string",
     "The best offered charset for Accept-Charset, or \"\" when none is "
     "acceptable."},
};

bool isRequestRow(const std::string& prop) {
    for (const RequestRow& row : kRequestRows) {
        if (prop == row.name) return true;
    }
    return false;
}

}  // namespace

std::optional<Type> Analyzer::webserverRequestMember(const Type& recv,
                                                   const std::string& prop) {
    if (recv.kind != TypeKind::Object || recv.name != kHttpRequest) {
        return std::nullopt;
    }
    for (const RequestRow& row : kRequestRows) {
        if (prop == row.name) return row.type;
    }
    return std::nullopt;
}

std::vector<std::string> webserverRequestMemberNames() {
    std::vector<std::string> names;
    names.reserve(sizeof(kRequestRows) / sizeof(kRequestRows[0]));
    for (const RequestRow& row : kRequestRows) names.push_back(row.name);
    return names;
}

StdMemberDoc webserverRequestMemberDoc(const std::string& prop) {
    for (const RequestRow& row : kRequestRows) {
        if (prop == row.name) return {row.signature, row.body};
    }
    return StdMemberDoc{};
}

bool iswebserverRequestMember(const std::string& prop) {
    return isRequestRow(prop);
}

}  // namespace cp3
