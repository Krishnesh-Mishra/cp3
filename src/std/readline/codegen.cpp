// =============================================================================
// src/std/readline/codegen.cpp - `std/readline` lowering.
//
// OWNERSHIP: the std/readline family (src/std/readline/**). The namespace
// dispatch arms live in emit/std/common.cpp (lead wiring, see the ledger
// report); this unit lowers:
//
//   readline.readLine()    -> ::cp3::runtime::readline::readLine()
//   readline.readLines()   -> ::cp3::runtime::readline::readLines()
//   readline.readAll()     -> ::cp3::runtime::readline::readAll()
//   readline.createReader()-> ::cp3::runtime::readline::createReader()
//   reader.readLine()      -> LineReader::readLine()
//   reader.readLines()     -> LineReader::readLines()
//   reader.on(event, cb)   -> LineReader::on(event, cb)
//   reader.close()         -> LineReader::close()
//
// The runtime header (cp3/std/readline.hpp) is included by module_writer
// when needsReadline_ is set; it in turn includes cp3/std/process.hpp for
// the stdin pump, and every read keeps the event loop alive
// (needsEventLoop_), exactly like process.stdin.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/int/builtin_tables.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kLineReader = "$readline:LineReader";
constexpr const char* kRuntimeNs = "::cp3::runtime::readline::";

/// The literal string value of `e` ("" when it is not a string literal).
std::string stringLiteralOf(const Expr* e) {
    if (!e || e->kind != NodeKind::StringLiteral) return std::string();
    return static_cast<const StringLiteral&>(*e).value;
}

}  // namespace

bool Generator::isReadlineObject(const Expr* e) const {
    if (e == nullptr) return false;
    return effectiveType(*e).name == kLineReader;
}

std::string Generator::emitReadlineCall(const CallExpr& c, const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "readline")) return std::string();

    const std::string& prop = m.property;
    if (prop == "readLine" || prop == "readLines" || prop == "readAll") {
        if (!c.arguments.empty()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "readline." + prop + "() takes no arguments");
            return "0.0";
        }
        needsReadline_ = true;
        needsEventLoop_ = true;
        return std::string(kRuntimeNs) + prop + "()";
    }
    if (prop == "createReader") {
        if (!c.arguments.empty()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "readline.createReader() takes no arguments");
            return "0.0";
        }
        needsReadline_ = true;
        return std::string(kRuntimeNs) + "createReader()";
    }
    return std::string();
}

std::string Generator::emitReadlineMember(const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "readline")) return std::string();
    // Every readline namespace member is a call (lowered in
    // emitReadlineCall); there are no property reads to lower here.
    return std::string();
}

std::string Generator::emitReadlineObjectCall(const CallExpr& c,
                                               const MemberExpr& m) {
    if (!m.object || !isReadlineObject(m.object.get())) return std::string();

    const std::string& prop = m.property;
    if ((prop == "readLine" || prop == "readLines" || prop == "close") &&
        c.arguments.empty()) {
        needsReadline_ = true;
        needsEventLoop_ = true;
        return "(" + expr(*m.object) + ")." + prop + "()";
    }
    if (prop != "on" || c.arguments.size() != 2) return std::string();
    const Expr* event = c.arguments[0].get();
    const Expr* listener = c.arguments[1].get();
    if (!event || !listener || event->kind == NodeKind::SpreadElement ||
        listener->kind == NodeKind::SpreadElement) {
        return std::string();
    }
    needsReadline_ = true;
    needsEventLoop_ = true;
    const std::string eventName = stringLiteralOf(event);
    const std::string call = "(" + expr(*m.object) + ").on(" + expr(*event) + ", ";
    if (listener->kind != NodeKind::ArrowFunction) {
        return call + expr(*listener) + ")";
    }
    const auto& arrow = static_cast<const ArrowFunction&>(*listener);
    if (eventName.empty() && !arrow.params.empty()) {
        reportTypeError(*event, "the readline event name must be a string "
                                "literal (\"line\" or \"close\"): the "
                                "listener's payload type depends on it");
        return "0.0";
    }
    pushScope();
    for (const auto& paramPtr : arrow.params) {
        const Parameter* p = paramPtr.get();
        if (!p || !p->name || p->name->kind != NodeKind::Identifier) continue;
        declare(static_cast<const Identifier&>(*p->name).name, Type::string());
    }
    std::string text = call + emitArrow(arrow, {"std::string"}) + ")";
    popScope();
    return text;
}

}  // namespace cp3
