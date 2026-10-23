// =============================================================================
// src/std/date/codegen.cpp - `Date.*` / Date-value call lowering.
//
// OWNERSHIP: the std/date family (src/std/date/**). One per-module lowering
// unit; the dispatch arm lives in emit/std/common.cpp (emitBuiltinCall), so the
// rest of codegen never needs to change for this module.
//
// `Date.now()` / `Date.fromISOString(s)` and the documented instance methods
// (docs/standard-library/04 section Date) lower 1:1 onto cp3/std/date.hpp
// (namespace cp3::runtime::date): an instance member passes the receiver as the
// first argument, so `d.addDays(n)` becomes `addDays(d, n)`. `needsDate_` makes
// buildSource add the include. A member outside the surface returns "" so the
// caller keeps reporting CP3002; for the closed Date surface sema has already
// rejected it as CP3305, so that path is defensive only.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

namespace detail {

// The Date value's C++ spelling (src/std/date/sema.cpp types it as the nominal
// class "Date"). `classify` calls this from its TypeKind::Class arm.
bool classifyDate(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class || t.name != "Date") return false;
    info.shape = Shape::Unknown;  // no Date-specific shape paths exist yet
    info.representable = true;
    info.cpp = "::cp3::runtime::date::Date";
    return true;
}

}  // namespace detail

using namespace detail;

namespace {

// A documented instance member -> its runtime spelling (cp3/std/date.hpp).
struct DateFn {
    const char* js;
    const char* fn;
};
constexpr DateFn kDateFns[] = {
    {"toISOString", "toIsoString"}, {"toString", "toString"},
    {"getFullYear", "getFullYear"}, {"getMonth", "getMonth"},
    {"getDate", "getDate"},         {"getHours", "getHours"},
    {"getMinutes", "getMinutes"},   {"getSeconds", "getSeconds"},
    {"addDays", "addDays"},         {"addHours", "addHours"},
    {"addMinutes", "addMinutes"},   {"addSeconds", "addSeconds"},
    {"isBefore", "isBefore"},       {"isAfter", "isAfter"},
    {"diffMs", "diffMs"},
};

const char* dateRuntimeFn(const std::string& name) {
    for (const DateFn& entry : kDateFns) {
        if (name == entry.js) return entry.fn;
    }
    return nullptr;
}

// Is `t` the nominal Date value type? (The codegen-side twin of sema's
// isDateClass; codegen cannot include sema's private headers.)
bool isDateValue(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Date";
}

}  // namespace

std::string Generator::emitDateCall(const CallExpr& c, const MemberExpr& m) {
    if (!m.object) return std::string();
    // The two statics on the namespace object (`Date.now()`,
    // `Date.fromISOString(s)`).
    if (isNamespaceObject(m.object.get(), "Date")) {
        if (m.property == "now") {
            needsDate_ = true;
            return "::cp3::runtime::date::now()";
        }
        if (m.property == "fromISOString") {
            needsDate_ = true;
            return "::cp3::runtime::date::fromIsoString(" +
                   joinStrings(emitPlainArgs(c), ", ") + ")";
        }
        return std::string();
    }
    // Instance methods on a Date value: receiver first.
    if (!isDateValue(effectiveType(*m.object))) return std::string();
    const char* fn = dateRuntimeFn(m.property);
    if (fn == nullptr) return std::string();
    needsDate_ = true;
    std::string out = "::cp3::runtime::date::" + std::string(fn) + "(" +
                      expr(*m.object);
    for (const std::string& arg : emitPlainArgs(c)) {
        out += ", " + arg;
    }
    return out + ")";
}

}  // namespace cp3
