// =============================================================================
// src/std/log/sema.cpp - `std/log` `Logger` typing and documentation.
//
// OWNERSHIP: the std/log family (src/std/log/**). `Logger` is a nominal
// `TypeKind::Class` type with no type arguments, the same shape as the
// collections classes (src/std/collections/sema.cpp) minus the generics.
//
// The member table below IS the docs surface: it drives member typing and
// the CP3305 candidate list at once (`reportMemberMiss`), so the two cannot
// drift apart. Logger is a CLOSED family: a member outside the table is
// CP3305, never an untyped fallthrough. The sink parameter type is the
// documented `(line: string) => void`.
//
// The signatures and the descriptions are the documented ones
// (docs/content/03-standard-library/22-log.md).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// The documented members of a `Logger`
// (docs/content/03-standard-library/22-log.md).
const char* const kLoggerMemberNames[] = {"name",  "level",   "setLevel",
                                          "debug", "info",    "warn",
                                          "error", "setSink", "resetSink"};

const DocRow kLoggerDocRows[] = {
    {"name", "name: string",
     "The logger name given to the constructor (\"\" when unnamed)."},
    {"level", "level: string",
     "The current minimum level: \"debug\", \"info\", \"warn\" or \"error\". "
     "The default is \"info\"."},
    {"setLevel", "setLevel(level: \"debug\" | \"info\" | \"warn\" | \"error\"): void",
     "Sets the minimum emitted level. Unknown names leave the level "
     "unchanged."},
    {"debug", "debug(msg: string, ...args: unknown[]): void",
     "Emits one `DEBUG name message` line when the level is `debug`."},
    {"info", "info(msg: string, ...args: unknown[]): void",
     "Emits one `INFO name message` line when the level is `info` or "
     "`debug`."},
    {"warn", "warn(msg: string, ...args: unknown[]): void",
     "Emits one `WARN name message` line when the level is `warn` or more "
     "verbose."},
    {"error", "error(msg: string, ...args: unknown[]): void",
     "Emits one `ERROR name message` line (always emitted)."},
    {"setSink", "setSink(sink: (line: string) => void): void",
     "Replaces stdout: the callback receives each emitted line (no trailing "
     "newline)."},
    {"resetSink", "resetSink(): void",
     "Restores the default stdout sink."},
};

}  // namespace

// =============================================================================
// The family model (declared with the collections family in scopes.h)
// =============================================================================
bool isLogClass(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Logger";
}

// =============================================================================
// `new Logger()` / `new Logger(name)`
// =============================================================================
Type Analyzer::inferLogConstruct(const std::string& name, ExprList& args,
                                 const SourceSpan& span) {
    if (name != "Logger") return Type::unknown();
    if (args.size() > 1) {
        report(diagcode::kArityMismatch, span,
               "arity mismatch: Logger's constructor takes at most one "
               "argument (the name), got " +
                   std::to_string(args.size()));
    }
    for (auto& a : args) {
        Type given = a ? inferExpr(*a) : Type::unknown();
        if (!given.isUnknown() && given.kind != TypeKind::String &&
            !given.isNullable() && !given.isUnion()) {
            report(diagcode::kTypeMismatch, a->span,
                   "type mismatch: Logger's name must be a string");
        }
    }
    return Type(TypeKind::Class, "Logger");
}

// =============================================================================
// The member surface
// =============================================================================
Type Analyzer::logMember(const Type& recv, const std::string& prop,
                         const SourceSpan& span) {
    (void)recv;
    if (prop == "name" || prop == "level") return Type::string();
    if (prop == "setLevel") {
        return Type::function({Type::string()}, Type::voidType());
    }
    if (prop == "debug" || prop == "info" || prop == "warn" || prop == "error") {
        // `msg` first, then any tail: the last parameter is the rest.
        Type f = Type::function({Type::string(), Type::unknown()},
                                Type::voidType());
        f.name = kVariadicLabel;
        return f;
    }
    if (prop == "setSink") {
        const Type sink = Type::function({Type::string()}, Type::voidType());
        return Type::function({sink}, Type::voidType());
    }
    if (prop == "resetSink") return Type::function({}, Type::voidType());
    std::vector<std::string> candidates;
    candidates.reserve(sizeof(kLoggerMemberNames) / sizeof(kLoggerMemberNames[0]));
    for (const char* name : kLoggerMemberNames) candidates.emplace_back(name);
    reportMemberMiss("Logger", prop, span, candidates);
    return Type::unknown();
}

StdMemberDoc logMemberDoc(const std::string& prop) {
    return memberDoc(kLoggerDocRows, prop);
}

std::vector<std::string> logMemberNames() { return docRowNames(kLoggerDocRows); }

}  // namespace cp3
