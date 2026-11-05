// =============================================================================
// src/std/time/codegen.cpp - `time.*` / Stopwatch-value call lowering.
//
// OWNERSHIP: the std/time family (src/std/time/**). One per-module lowering
// unit; the dispatch arm lives in emit/std/common.cpp (emitBuiltinCall), so
// the rest of codegen never needs to change for this module.
//
// `time.stopwatch()` and the documented Stopwatch instance methods lower 1:1
// onto cp3/std/time.hpp (namespace cp3::runtime::time): an instance member
// passes the receiver as the first argument, so `sw.elapsed()` becomes
// `elapsed(sw)`. `needsTime_` makes buildSource add the include. A member
// outside the surface returns ""; for the closed time surface sema has
// already rejected it as CP3305, so that path is defensive only.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/int/builtin_tables.h"
#include "detail/generator/type_map.h"

namespace cp3 {

namespace detail {

// The Stopwatch value's C++ spelling (src/std/time/sema.cpp types it as the
// nominal class "Stopwatch"). `classify` calls this from its
// TypeKind::Class arm.
bool classifyStopwatch(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class || t.name != "Stopwatch") return false;
    info.shape = Shape::Unknown;  // no Stopwatch-specific shape paths exist
    info.representable = true;
    info.cpp = "::cp3::runtime::time::Stopwatch";
    return true;
}

}  // namespace detail

using namespace detail;

namespace {

// A documented instance member -> its runtime spelling (cp3/std/time.hpp).
struct TimeFn {
    const char* js;
    const char* fn;
};
constexpr TimeFn kTimeFns[] = {
    {"start", "start"},
    {"stop", "stop"},
    {"elapsed", "elapsed"},
    {"reset", "reset"},
};

const char* timeRuntimeFn(const std::string& name) {
    for (const TimeFn& entry : kTimeFns) {
        if (name == entry.js) return entry.fn;
    }
    return nullptr;
}

// Is `t` the nominal Stopwatch value type? (The codegen-side twin of sema's
// isStopwatchClass; codegen cannot include sema's private headers.)
bool isStopwatchValue(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Stopwatch";
}

}  // namespace

std::string Generator::emitTimeCall(const CallExpr& c, const MemberExpr& m) {
    if (!m.object) return std::string();
    // The static on the namespace object (`time.stopwatch()`).
    if (isNamespaceObject(m.object.get(), "time")) {
        if (m.property == "stopwatch") {
            needsTime_ = true;
            return "::cp3::runtime::time::stopwatch()";
        }
        return std::string();
    }
    // Instance methods on a Stopwatch value: receiver first.
    if (!isStopwatchValue(effectiveType(*m.object))) return std::string();
    const char* fn = timeRuntimeFn(m.property);
    if (fn == nullptr) return std::string();
    needsTime_ = true;
    std::string out = "::cp3::runtime::time::" + std::string(fn) + "(" +
                      expr(*m.object);
    for (const std::string& arg : emitPlainArgs(c)) {
        out += ", " + arg;
    }
    out += ")";
    return out;
}

}  // namespace cp3
