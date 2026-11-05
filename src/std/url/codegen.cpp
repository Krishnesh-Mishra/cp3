// =============================================================================
// src/std/url/codegen.cpp - lowering for the `std/url` surface.
//
// `encodeURIComponent(s)` -> `::cp3::runtime::url::encodeURIComponent(s)`
// `new URL(input[, base])` -> `::cp3::runtime::url::makeURL(input[, base])`
// `new URLSearchParams(q?)` -> `::cp3::runtime::url::makeURLSearchParams(q?)`
// `u.href` / `u.searchParams` -> `u->href` / `u->searchParams`
// `u.toString()` -> `u->toString()` (returns `href`)
// `sp.append/get/...` -> the member calls on `sp` (`delete` -> `remove`)
//
// Like `std/date` (src/std/date/codegen.cpp), the URL types use Shape::Unknown
// with a direct `::cp3::runtime::url::URL` spelling: no dedicated value-shape
// paths exist for them yet, and the property surface is lowered here rather
// than through the generic object model. A member outside the sema table
// never reaches this unit; the closed CP3305 diagnostic is sema's.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/type_map.h"
#include "detail/int/builtin_tables.h"

namespace cp3 {

using namespace detail;

namespace detail {

// The URL family's C++ spelling (src/std/url/sema.cpp types them as the
// nominal classes "URL" / "URLSearchParams"). `classify` calls this from its
// TypeKind::Class arm (the `classifyDate` precedent).
bool classifyUrl(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class) return false;
    if (t.name != "URL" && t.name != "URLSearchParams") return false;
    info.shape = Shape::Unknown;  // no URL-specific shape paths exist yet
    info.representable = true;
    info.cpp = t.name == "URL" ? "::cp3::runtime::url::URL"
                               : "::cp3::runtime::url::URLSearchParams";
    return true;
}

}  // namespace detail

namespace {

const char* const kUrlMethods[] = {"encodeURIComponent",
                                   "decodeURIComponent", "encodeURI",
                                   "decodeURI"};

bool isUrlType(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == "URL" || t.name == "URLSearchParams");
}

bool isUrlParamsType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "URLSearchParams";
}

const char* const kUrlParamsMethods[] = {"append", "get",    "getAll", "has",
                                         "set",    "delete", "toString", "entries"};

bool isUrlParamsMethod(const std::string& name) {
    return CP3_IN_TABLE(name, kUrlParamsMethods);
}

}  // namespace

std::string Generator::emitUrlCall(const CallExpr& c, const MemberExpr& m) {
    if (!CP3_IN_TABLE(m.property, kUrlMethods)) return std::string();
    if (c.arguments.size() != 1 || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: " + m.property +
                               "() takes exactly one argument");
        return "0.0";
    }
    needsUrl_ = true;
    return "::cp3::runtime::url::" + m.property + "(" +
           expr(*c.arguments.front()) + ")";
}

// `new URL(input[, base])` / `new URLSearchParams(query?)`.
std::string Generator::emitUrlNew(const NewExpr& n) {
    const Type t = effectiveType(n);
    if (!isUrlType(t)) return std::string();
    needsUrl_ = true;
    if (t.name == "URL") {
        if (n.arguments.empty() || n.arguments.size() > 2) {
            reportCodegenError(
                n, diagcode::kArityMismatch,
                "arity mismatch: URL's constructor takes 1 or 2 arguments "
                "(input, optional base)");
            return "0.0";
        }
        std::string args = expr(*n.arguments.front());
        if (n.arguments.size() == 2 && n.arguments[1]) {
            args += ", " + expr(*n.arguments[1]);
        }
        return "::cp3::runtime::url::makeURL(" + args + ")";
    }
    if (n.arguments.size() > 1) {
        reportCodegenError(
            n, diagcode::kArityMismatch,
            "arity mismatch: URLSearchParams's constructor takes 0 or 1 "
            "argument (optional query string)");
        return "0.0";
    }
    if (n.arguments.empty() || !n.arguments.front()) {
        return "::cp3::runtime::url::makeURLSearchParams()";
    }
    return "::cp3::runtime::url::makeURLSearchParams(" +
           expr(*n.arguments.front()) + ")";
}

// URL / URLSearchParams method calls (`sp.append(...)`, `sp.get(...)`, ...,
// `u.toString()`).
std::string Generator::emitUrlMemberCall(const CallExpr& c,
                                        const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    const bool isUrl =
        recv.kind == TypeKind::Class && recv.name == "URL";
    if (isUrl) {
        // `URL.toString()` returns `href` (the runtime `toString()`).
        if (m.property == "toString") {
            needsUrl_ = true;
            return "(" + expr(*m.object) + ")->toString()";
        }
        return std::string();
    }
    if (!isUrlParamsType(recv)) return std::string();
    if (!isUrlParamsMethod(m.property)) return std::string();
    needsUrl_ = true;
    const std::string obj = "(" + expr(*m.object) + ")";
    const std::string plain = [&]() {
        std::string out;
        for (const std::string& a : emitPlainArgs(c)) {
            if (!out.empty()) out += ", ";
            out += a;
        }
        return out;
    }();
    // `delete` is a C++ keyword; the runtime member is `remove`.
    const std::string cpp = m.property == "delete" ? "remove" : m.property;
    if (m.property == "toString" && plain.empty()) {
        return obj + "->toString()";
    }
    return obj + "->" + cpp + "(" + plain + ")";
}

// Property reads: the ten URL string components and `searchParams`.
std::string Generator::emitUrlMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (!isUrlType(recv)) return std::string();
    const std::string& prop = m.property;
    if (prop == "href" || prop == "protocol" || prop == "host" ||
        prop == "hostname" || prop == "port" || prop == "pathname" ||
        prop == "search" || prop == "hash" || prop == "origin" ||
        prop == "username" || prop == "password" || prop == "searchParams") {
        needsUrl_ = true;
        return "(" + expr(*m.object) + ")->" + prop;
    }
    return std::string();
}

}  // namespace cp3
