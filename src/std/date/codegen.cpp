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
// buildSource add the include. A member outside the surface returns ""; for the
// closed Date surface sema has already rejected it as CP3305, so that path is
// defensive only.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

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
    {"getTime", "getTime"},         {"getDay", "getDay"},
    {"getUTCFullYear", "getUTCFullYear"}, {"getUTCMonth", "getUTCMonth"},
    {"getUTCDate", "getUTCDate"},   {"getUTCDay", "getUTCDay"},
    {"getUTCHours", "getUTCHours"}, {"getUTCMinutes", "getUTCMinutes"},
    {"getUTCSeconds", "getUTCSeconds"},
    {"setTime", "setTime"},         {"setFullYear", "setFullYear"},
    {"setMonth", "setMonth"},       {"setDate", "setDate"},
    {"setHours", "setHours"},       {"setMinutes", "setMinutes"},
    {"setSeconds", "setSeconds"},
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

// The timestamp-returning Date surface: the exact `i64` milliseconds width in
// a Rust/C++ project; a TypeScript project keeps `number`, so a read widens
// at the call site.
bool isTimestampResult(const std::string& fn) {
    return fn == "getTime" || fn == "diffMs" || fn == "setTime" ||
           fn == "setFullYear" || fn == "setMonth" || fn == "setDate" ||
           fn == "setHours" || fn == "setMinutes" || fn == "setSeconds";
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
        if (m.property == "parse") {
            needsDate_ = true;
            const std::string call =
                "::cp3::runtime::date::parse(" +
                joinStrings(emitPlainArgs(c), ", ") + ")";
            // `parse` returns a timestamp: `i64` in a Rust/C++ project, a
            // widened `number` in a TypeScript one.
            if (options_.typeSystem == TypeSystem::TypeScript) {
                return "static_cast<double>(" + call + ")";
            }
            return call;
        }
        return std::string();
    }
    // Instance methods on a Date value: receiver first.
    if (!isDateValue(effectiveType(*m.object))) return std::string();
    const char* fn = dateRuntimeFn(m.property);
    if (fn == nullptr) return std::string();
    needsDate_ = true;
    std::vector<std::string> args = emitPlainArgs(c);
    if (m.property == "setTime" && !args.empty()) {
        // The runtime takes the exact `i64` timestamp width; a TypeScript
        // project's `number` argument narrows here.
        args[0] =
            "static_cast<std::int64_t>(" + std::move(args[0]) + ")";
    }
    // CP3 `const` is a binding constness (JS semantics: the object stays
    // mutable), so a CP3 `const` Date binds to a C++ `const Date` and a
    // mutating setter would drop the qualifier. const_cast back: Date is
    // never actually a genuinely-const object here.
    const bool mutating = std::string(fn).rfind("set", 0) == 0;
    const std::string receiver =
        mutating ? "const_cast<::cp3::runtime::date::Date&>(" + expr(*m.object) + ")"
                 : expr(*m.object);
    std::string out = "::cp3::runtime::date::" + std::string(fn) + "(" +
                      receiver;
    for (const std::string& arg : args) {
        out += ", " + arg;
    }
    out += ")";
    // The timestamp surface is the exact `i64` in a Rust/C++ project; a
    // TypeScript project keeps `number`, so the read widens here.
    if (options_.typeSystem == TypeSystem::TypeScript &&
        isTimestampResult(fn)) {
        return "static_cast<double>(" + out + ")";
    }
    return out;
}

// `new Date()` -> now, `new Date(ms)` -> epoch-ms Date.
std::string Generator::emitDateNew(const NewExpr& n) {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::string();
    if (static_cast<const Identifier&>(*n.callee).name != "Date") return std::string();
    const Type t = effectiveType(n);
    if (!(t.kind == TypeKind::Class && t.name == "Date")) return std::string();
    needsDate_ = true;
    if (n.arguments.empty() || !n.arguments.front()) {
        return "::cp3::runtime::date::now()";
    }
    return "::cp3::runtime::date::Date{static_cast<std::int64_t>(" +
           expr(*n.arguments.front()) + ")}";
}

// Property-style getter reads (`d.getTime`, `d.getDay`, ...): the JS spelling
// used by the date_gaps corpus case. Zero-arg getters lower to the same free
// function as the call form.
std::string Generator::emitDateMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (!isDateValue(recv)) return std::string();
    const char* fn = dateRuntimeFn(m.property);
    if (fn == nullptr) return std::string();
    // Only zero-arg getters are property reads; setters/adders/isBefore etc.
    // require a call and keep the member-call path.
    static const char* const kGetters[] = {
        "toISOString", "toString", "getFullYear", "getMonth", "getDate",
        "getHours", "getMinutes", "getSeconds", "getTime", "getDay",
        "getUTCFullYear", "getUTCMonth", "getUTCDate", "getUTCDay",
        "getUTCHours", "getUTCMinutes", "getUTCSeconds"};
    bool isGetter = false;
    for (const char* g : kGetters) if (m.property == g) isGetter = true;
    if (!isGetter) return std::string();
    needsDate_ = true;
    std::string out = "::cp3::runtime::date::" + std::string(fn) + "(" +
                      expr(*m.object) + ")";
    if (options_.typeSystem == TypeSystem::TypeScript && isTimestampResult(fn)) {
        return "static_cast<double>(" + out + ")";
    }
    return out;
}

}  // namespace cp3
