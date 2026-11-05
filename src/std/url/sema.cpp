// =============================================================================
// src/std/url/sema.cpp - `std/url` member typing and documentation.
//
// OWNERSHIP: the std/url family (src/std/url/**). The member table drives
// member typing, the CP3305 candidate list and the documentation the editor
// shows, so the three can never drift apart. URL is a CLOSED family: a member
// outside the table is CP3305 with these names as candidates, never an
// untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, URL section): RFC 3986
// URI encoding and decoding, plus the `URL` / `URLSearchParams` classes.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type stringUnary() { return Type::function({Type::string()}, Type::string()); }

const MemberRow kUrlRows[] = {
    {"encodeURIComponent", &stringUnary,
     "encodeURIComponent(value: string): string",
     "Escapes all characters except A-Z a-z 0-9 - _ . ! ~ * ' ( ) (the RFC "
     "3986 unreserved marks plus the ECMAScript extra set)."},
    {"decodeURIComponent", &stringUnary,
     "decodeURIComponent(value: string): string",
     "Decodes %HH sequences; a malformed sequence is left as-is."},
    {"encodeURI", &stringUnary,
     "encodeURI(value: string): string",
     "Escapes all characters except the URI delimiters (; / ? : @ & = + $ , "
     "#), leaving the URI structure intact."},
    {"decodeURI", &stringUnary,
     "decodeURI(value: string): string",
     "Decodes %HH sequences; a malformed sequence is left as-is."},
};

// The documented URL properties (docs 04 §url). The href/property rows keep
// the WHATWG snapshot contract: the strings are captured at construction and
// do not change when `searchParams` is mutated afterwards.
const DocRow kUrlDocRows[] = {
    {"href", "href: string",
     "The serialized URL (the `href` property)."},
    {"protocol", "protocol: string",
     "The scheme with a trailing colon (e.g. `\"https:\"`)."},
    {"host", "host: string",
     "The hostname with the port when present (e.g. `\"example.com:8080\"`)."},
    {"hostname", "hostname: string",
     "The lowercased hostname without the port."},
    {"port", "port: string",
     "The port digits without the colon, or `\"\"` when absent or default."},
    {"pathname", "pathname: string",
     "The URL path (e.g. `\"/a/b\"`)."},
    {"search", "search: string",
     "The query string with a leading `?`, or `\"\"` when absent."},
    {"hash", "hash: string",
     "The fragment with a leading `#`, or `\"\"` when absent."},
    {"origin", "origin: string",
     "The origin (`scheme://host[:port]`), or `\"\"` for opaque schemes."},
    {"username", "username: string",
     "The userinfo username (percent-encoded), or `\"\"` when absent."},
    {"password", "password: string",
     "The userinfo password (percent-encoded), or `\"\"` when absent."},
    {"toString", "toString(): string",
     "The serialized URL (same as `href`)."},
    {"searchParams", "searchParams: URLSearchParams",
     "The live `URLSearchParams` handle for this URL's query."},
};

// The documented URLSearchParams members (docs 04 §url). `get` yields
// `string | undefined`; `getAll` and `entries` return arrays.
const DocRow kUrlSearchParamsDocRows[] = {
    {"append", "append(name: string, value: string): void",
     "Appends a `name=value` pair to the end of the list."},
    {"get", "get(name: string): string | undefined",
     "Returns the first value for `name`, or `undefined` when absent."},
    {"getAll", "getAll(name: string): string[]",
     "Returns every value for `name`, in order."},
    {"has", "has(name: string): boolean",
     "True when `name` appears at least once."},
    {"set", "set(name: string, value: string): void",
     "Replaces the first value for `name` and removes later duplicates; "
     "appends when `name` is absent."},
    {"delete", "delete(name: string): boolean",
     "Removes every pair with `name`, returning whether any were removed."},
    {"toString", "toString(): string",
     "Serializes the list as `name=value&...` (application/x-www-form-"
     "urlencoded), with no leading `?`."},
    {"entries", "entries(): [string, string][]",
     "Returns the pairs as `[name, value]` tuples, in order (the optional "
     "WHATWG reading)."},
};

template <std::size_t N>
std::vector<std::string> candidateNames(const char* const (&names)[N]) {
    std::vector<std::string> out;
    out.reserve(N);
    for (const char* name : names) out.emplace_back(name);
    return out;
}

constexpr const char* kUrlMemberNames[] = {
    "href", "protocol", "host",  "hostname", "port",      "pathname",
    "search", "hash",     "origin", "username", "password", "toString",
    "searchParams"};
constexpr const char* kUrlSearchParamsMemberNames[] = {
    "append", "get", "getAll", "has", "set", "delete", "toString", "entries"};

Type urlType() { return Type(TypeKind::Class, "URL"); }

Type urlSearchParamsType() { return Type(TypeKind::Class, "URLSearchParams"); }

}  // namespace

std::optional<Type> Analyzer::urlMember(const std::string& prop) {
    return lookupMember(kUrlRows, prop);
}

std::vector<std::string> urlMemberNames() {
    return memberNames(kUrlRows);
}

StdMemberDoc urlMemberDoc(const std::string& prop) {
    return memberDoc(kUrlRows, prop);
}

// =============================================================================
// The family model (shared with scopes.h and member_check.cpp)
// =============================================================================
bool isUrlClass(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == "URL" || t.name == "URLSearchParams");
}

// =============================================================================
// `new URL(input, base?)` / `new URLSearchParams(query?)`
// =============================================================================
Type Analyzer::inferUrlConstruct(const std::string& name, ExprList& args,
                                const SourceSpan& span) {
    if (name != "URL" && name != "URLSearchParams") return Type::unknown();
    for (auto& arg : args) {
        if (arg) inferExpr(*arg);
    }
    if (name == "URL") {
        if (args.empty() || args.size() > 2) {
            report(diagcode::kArityMismatch, span,
                   "arity mismatch: URL's constructor takes 1 or 2 arguments "
                   "(input, optional base), got " +
                       std::to_string(args.size()));
        }
        return urlType();
    }
    if (args.size() > 1) {
        report(diagcode::kArityMismatch, span,
               "arity mismatch: URLSearchParams's constructor takes 0 or 1 "
               "argument (optional query string), got " +
                   std::to_string(args.size()));
    }
    return urlSearchParamsType();
}

// =============================================================================
// The member surface
// =============================================================================
Type Analyzer::urlClassMember(const Type& recv, const std::string& prop,
                             const SourceSpan& span) {
    if (recv.name == "URLSearchParams") {
        if (prop == "append" || prop == "set") {
            return Type::function({Type::string(), Type::string()},
                                  Type::voidType());
        }
        if (prop == "get") {
            return Type::function(
                {Type::string()},
                Type::unionOf({Type::string(), Type::undefined()}));
        }
        if (prop == "getAll") {
            return Type::function({Type::string()},
                                  Type::array(Type::string()));
        }
        if (prop == "has") {
            return Type::function({Type::string()}, Type::boolean());
        }
        if (prop == "delete") {
            return Type::function({Type::string()}, Type::boolean());
        }
        if (prop == "toString") {
            return Type::function({}, Type::string());
        }
        if (prop == "entries") {
            const Type pair = makeObjectType(
                {}, false, true, {Type::string(), Type::string()});
            return Type::function({}, Type::array(pair));
        }
        reportMemberMiss("URLSearchParams", prop, span,
                         candidateNames(kUrlSearchParamsMemberNames));
        return Type::unknown();
    }
    // URL: the string snapshot properties plus the live searchParams handle.
    if (prop == "href" || prop == "protocol" || prop == "host" ||
        prop == "hostname" || prop == "port" || prop == "pathname" ||
        prop == "search" || prop == "hash" || prop == "origin" ||
        prop == "username" || prop == "password") {
        return Type::string();
    }
    if (prop == "toString") {
        return Type::function({}, Type::string());
    }
    if (prop == "searchParams") {
        return urlSearchParamsType();
    }
    reportMemberMiss("URL", prop, span, candidateNames(kUrlMemberNames));
    return Type::unknown();
}

StdMemberDoc urlClassMemberDoc(const std::string& family,
                               const std::string& prop) {
    if (family == "URL") return memberDoc(kUrlDocRows, prop);
    if (family == "URLSearchParams") {
        return memberDoc(kUrlSearchParamsDocRows, prop);
    }
    return StdMemberDoc{};
}

std::vector<std::string> urlClassMemberNames(const std::string& family) {
    if (family == "URL") return docRowNames(kUrlDocRows);
    if (family == "URLSearchParams") return docRowNames(kUrlSearchParamsDocRows);
    return {};
}

}  // namespace cp3
