// =============================================================================
// src/std/log/codegen.cpp - `std/log` `Logger` lowering.
//
// Lowers the family registered by sema (src/std/log/sema.cpp) onto
// cp3/std/log.hpp: `new Logger([name])`, the `level` / `name` reads and the
// member calls (`setLevel`, the four log methods, `setSink`, `resetSink`).
// A member outside the sema table never reaches this unit: sema reports
// CP3305 (src/std/log/sema.cpp).
//
// The handle reuses Shape::Collections (a `Ref<LoggerData>`) so no new Shape
// is needed; `classifyLogger` (type_map.h) owns the type model below and the
// Generator::emitLog* members own the emission only.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

namespace detail {

// The family's C++ mapping: `Logger` -> the runtime's `Ref<LoggerData>`.
// A Logger has no type arguments, so there is nothing else to classify.
bool classifyLogger(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class || t.name != "Logger") return false;
    info.shape = Shape::Collections;
    info.representable = true;
    info.cpp = "::cp3::runtime::log::Logger";
    return true;
}

}  // namespace detail

using namespace detail;

namespace {

bool isLogType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Logger";
}

bool isLogMethod(const std::string& prop) {
    return prop == "debug" || prop == "info" || prop == "warn" ||
           prop == "error";
}

}  // namespace

// `new Logger()` / `new Logger(name)`.
std::string Generator::emitLogNew(const NewExpr& n) {
    const Type t = effectiveType(n);
    if (!isLogType(t)) return std::string();
    if (n.arguments.size() > 1) {
        reportCodegenError(n, diagcode::kArityMismatch,
                           "arity mismatch: Logger's constructor takes at most "
                           "one argument (the name)");
        return "0.0";
    }
    needsLog_ = true;
    if (n.arguments.empty() || !n.arguments.front()) {
        return "::cp3::runtime::log::makeLogger()";
    }
    return "::cp3::runtime::log::makeLogger(" + expr(*n.arguments.front()) + ")";
}

// The member-call dispatcher (`log.info(...)`, `log.setLevel(...)`, ...).
std::string Generator::emitLogMemberCall(const CallExpr& c,
                                         const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (!isLogType(recv)) return std::string();
    needsLog_ = true;
    const std::string obj = "(" + expr(*m.object) + ")";
    const std::string& prop = m.property;
    const auto args = [&]() {
        return joinStrings(emitPlainArgs(c), ", ");
    };
    if (prop == "setLevel") {
        if (c.arguments.size() != 1 || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: setLevel takes exactly one "
                               "level name");
            return "0.0";
        }
        return obj + "->setLevel(" + args() + ")";
    }
    if (isLogMethod(prop)) {
        if (c.arguments.empty() || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: " + prop +
                                   " takes a message argument");
            return "0.0";
        }
        return obj + "->" + prop + "(" + args() + ")";
    }
    if (prop == "resetSink") return obj + "->resetSink()";
    if (prop == "setSink") {
        if (c.arguments.size() != 1 || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: setSink takes exactly one "
                               "callback");
            return "0.0";
        }
        const Expr& cb = *c.arguments.front();
        const std::string callback =
            cb.kind == NodeKind::ArrowFunction
                ? emitArrow(static_cast<const ArrowFunction&>(cb),
                            {"std::string"})
                : expr(cb);
        return obj + "->setSink(" + callback + ")";
    }
    return std::string();
}

// Property reads: `log.level` / `log.name`.
std::string Generator::emitLogMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (!isLogType(recv)) return std::string();
    if (m.property == "level") {
        needsLog_ = true;
        return "(" + expr(*m.object) + ")->level()";
    }
    if (m.property == "name") {
        needsLog_ = true;
        return "(" + expr(*m.object) + ")->name()";
    }
    return std::string();
}

}  // namespace cp3
