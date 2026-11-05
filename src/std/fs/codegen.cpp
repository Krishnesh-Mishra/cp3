// =============================================================================
// src/std/fs/codegen.cpp - `std/fs` lowering.
//
// OWNERSHIP: the std/fs family (src/std/fs/**). The namespace dispatch arms
// live in emit/std/common.cpp (emitBuiltinCall / emitBuiltinMember); everything
// lowers onto cp3/std/fs.hpp:
//
//   fs.<member>(...)        -> ::cp3::runtime::fs::<member>(...)  (the table
//                              below mirrors src/std/fs/sema.cpp 1:1)
//   stat.isDirectory        -> (stat).isDirectory            (FileStats reads,
//   stat.size               -> (stat).size (u64; static_cast<double> only in
//                              a TypeScript project) and the nullary
//                              `isFile()` / `isDirectory()` call form)
//   stat.modifiedAt         -> (stat).modifiedAt            (a std/date value)
//   rs.on("data", (chunk) => ...) -> (rs).on("data", [](Buffer chunk){...})
//   ws.write(chunk)         -> (ws).write(chunk)  (string or Buffer overload)
//   ws.end()                -> (ws).end()
//   chunk.toString()        -> ::cp3::runtime::buffer::toString(chunk)
//
// Receivers are recognised by the `$fs:` marker names sema publishes
// (src/std/fs/sema.cpp). The listener lambda is emitted with declared parameter
// types (contextParams), because sema keeps callback parameters permissive;
// its `chunk` is declared in a temporary scope as the nominal `Buffer` class,
// so member calls inside the body lower through the std/buffer unit.
//
// Every async member and every stream use sets needsEventLoop_: promise
// continuations and the stream pump are microtasks, so `main` must drive the
// loop even when the unit has no `await`.
//
// CROSS-FAMILY NOTES (see cp3/std/fs.hpp and the wave report): the round-3
// Buffer seam is closed - chunks are the nominal `Buffer` and every chunk
// member lowers through src/std/buffer/codegen.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kFileStats = "$fs:FileStats";
constexpr const char* kReadStream = "$fs:ReadStream";
constexpr const char* kWriteStream = "$fs:WriteStream";

constexpr const char* kRuntimeNs = "::cp3::runtime::fs::";

// The documented member surface (keep in step with src/std/fs/sema.cpp).
const char* const kFsMembers[] = {
    "readFileSync", "writeFileSync", "appendFileSync", "existsSync",
    "mkdirSync",    "readdirSync",   "statSync",       "unlinkSync",
    "readFile",     "writeFile",     "appendFile",     "mkdir",
    "readdir",      "stat",          "unlink",         "readFileStream",
    "writeFileStream",
};

/// The Promise-valued members: calling one schedules a continuation.
bool isPromiseMember(const std::string& prop) {
    return prop == "readFile" || prop == "writeFile" || prop == "appendFile" ||
           prop == "mkdir" || prop == "readdir" || prop == "stat" ||
           prop == "unlink";
}

/// The stream entry points: the pump/close run through the event loop.
bool isStreamMember(const std::string& prop) {
    return prop == "readFileStream" || prop == "writeFileStream";
}

/// The sema marker behind a listener's first declared parameter, or Unknown.
Type listenerParamType(const std::string& event) {
    if (event == "data") return Type::named(TypeKind::Class, "Buffer");
    if (event == "error") return Type::string();
    return Type::unknown();
}

/// The documented members of one stream receiver (docs 04 §fs): the
/// `ReadStream` interface carries `on`, the `WriteStream` interface `write`,
/// `end` and `on`. A member outside the table is a member-not-found error
/// (CP3305) with these names as the candidate list - the fs family is closed,
/// so a member that is not documented is a typo, not a missing feature.
std::vector<std::string> streamMemberNames(const std::string& receiver) {
    if (receiver == kReadStream) return {"on"};
    if (receiver == kWriteStream) return {"write", "end", "on"};
    return {};
}

/// The cp3 name of a stream receiver (the marker names the family uses).
std::string streamReceiverName(const std::string& receiver) {
    if (receiver == kReadStream) return "ReadStream";
    if (receiver == kWriteStream) return "WriteStream";
    return receiver;
}

/// The candidate a typo most likely meant: the documented member that shares
/// the longest prefix with the name (the sema ranks the closed namespaces'
/// candidates the same way).
std::string streamMemberSuggestion(const std::string& prop,
                                   const std::vector<std::string>& candidates) {
    std::string best;
    std::size_t bestLen = 0;
    for (const std::string& candidate : candidates) {
        std::size_t common = 0;
        while (common < prop.size() && common < candidate.size() &&
               prop[common] == candidate[common]) {
            ++common;
        }
        if (common > bestLen) {
            bestLen = common;
            best = candidate;
        }
    }
    return bestLen >= 2 ? best : std::string();
}


/// The arity of a function-typed value (-1 when the type is not a function,
/// or not known yet).
int functionArity(const Type& t) {
    if (t.kind != TypeKind::Function || t.parts.empty()) return -1;
    return static_cast<int>(t.parts.size()) - 1;
}

/// The number of parameters a listener takes: an arrow's own parameter list,
/// or the arity of a function-typed value. -1 when it is not known.
int listenerArity(const Expr& listener, const Type& type) {
    if (listener.kind == NodeKind::ArrowFunction) {
        return static_cast<int>(
            static_cast<const ArrowFunction&>(listener).params.size());
    }
    return functionArity(type);
}

/// True when a listener can take the event's payload. A listener with no
/// parameters is fine for every event (the runtime's payload-less overload
/// fires on "data", "error" and "end"); a listener with a parameter must
/// take the payload's type.
bool listenerFits(const Type& listener, const Type& payload) {
    const int arity = functionArity(listener);
    if (arity < 0) return true;          // the shape is not known here
    if (arity == 0) return true;         // the payload-less overload
    if (arity > 1) return false;         // the runtime takes one payload
    if (payload.isUnknown()) return true;
    const Type& first = listener.parts.front();
    if (first.isUnknown() || first.isError()) return true;
    if (first.kind == TypeKind::Object && first.name == "$ambient") return true;
    if (payload.kind == TypeKind::Class || first.kind == TypeKind::Class) {
        return payload.kind == first.kind && payload.name == first.name;
    }
    return payload.kind == first.kind;
}

/// The diagnostic a listener value carries, or "" when it fits the event.
std::string listenerDiagnostic(const Type& listener, const Type& payload,
                               const std::string& event) {
    if (listener.kind != TypeKind::Function && !listener.isUnknown() &&
        !listener.isError()) {
        return "the stream listener must be a function";
    }
    if (!listenerFits(listener, payload)) {
        return "the stream listener does not take the payload of the \"" + event +
               "\" event";
    }
    return std::string();
}

}  // namespace

bool Generator::isFsReceiver(const Expr* e) const {
    if (e == nullptr) return false;
    const std::string& name = effectiveType(*e).name;
    return name == kFileStats || name == kReadStream || name == kWriteStream;
}

// =============================================================================
// The namespace table: `fs.<member>(...)`
// =============================================================================
std::string Generator::emitFsCall(const CallExpr& c, const MemberExpr& m) {
    if (!CP3_IN_TABLE(m.property, kFsMembers)) return std::string();
    needsFs_ = true;
    if (isPromiseMember(m.property) || isStreamMember(m.property)) {
        needsEventLoop_ = true;
    }
    return std::string(kRuntimeNs) + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

// =============================================================================
// Receiver members
// =============================================================================
std::string Generator::emitFsMemberCall(const CallExpr& c, const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    const std::string& receiver = effectiveType(*m.object).name;
    needsFs_ = true;
    if (receiver == kFileStats) {
        // `isFile` / `isDirectory` are interface fields (docs 04); the call
        // form lowers to the same field read for surface tolerance.
        if ((m.property == "isFile" || m.property == "isDirectory") &&
            c.arguments.empty()) {
            return "(" + expr(*m.object) + ")." + m.property;
        }
        return std::string();
    }
    if ((receiver == kReadStream || receiver == kWriteStream) && m.property == "on") {
        return emitFsStreamOn(c, m);
    }
    if (receiver == kWriteStream && m.property == "write") {
        return "(" + expr(*m.object) + ").write(" +
               joinStrings(emitPlainArgs(c), ", ") + ")";
    }
    if (receiver == kWriteStream && m.property == "end" && c.arguments.empty()) {
        return "(" + expr(*m.object) + ").end()";
    }
    // A member outside the documented stream interface (docs 04 §fs): the
    // documented members are the candidate list for a typo (CP3305), the same
    // shape the closed std namespaces report.
    const std::vector<std::string> candidates = streamMemberNames(receiver);
    std::string message = "'" + streamReceiverName(receiver) + "' has no member '" +
                          m.property + "'";
    const std::string suggestion =
        streamMemberSuggestion(m.property, candidates);
    if (!suggestion.empty()) message += "; did you mean '" + suggestion + "'?";
    reportCodegenError(c, diagcode::kMemberNotFound, message);
    return "0.0";
}

std::string Generator::emitFsMemberRead(const MemberExpr& m) {
    if (m.object == nullptr) return std::string();
    if (effectiveType(*m.object).name != kFileStats) return std::string();
    if (m.property != "size" && m.property != "isFile" &&
        m.property != "isDirectory" && m.property != "modifiedAt") {
        return std::string();
    }
    needsFs_ = true;
    const std::string field = "(" + expr(*m.object) + ")." + m.property;
    // `size` is the exact `u64` byte count; a TypeScript project sees it as
    // `number`, exactly as the std integer table says.
    if (m.property == "size" &&
        options_.typeSystem == TypeSystem::TypeScript) {
        return "static_cast<double>(" + field + ")";
    }
    return field;
}

// =============================================================================
// `stream.on(event, listener)`
// =============================================================================
std::string Generator::emitFsStreamOn(const CallExpr& c, const MemberExpr& m) {
    if (c.arguments.size() < 2 || !c.arguments[0] || !c.arguments[1]) {
        // The documented surface is `on(event, listener)`: a call with fewer
        // arguments is a program error, not a missing feature.
        reportCodegenError(c, diagcode::kArityMismatch,
                           "stream.on(event, listener) takes two arguments");
        return "0.0";
    }
    const Expr& first = *c.arguments[0];
    const Expr& listener = *c.arguments[1];
    const bool literal = first.kind == NodeKind::StringLiteral;
    const std::string event =
        literal ? static_cast<const StringLiteral&>(first).value : std::string();
    // The payload type depends on the event name ("data" -> Buffer, "error"
    // -> string, "end" -> nothing), so a computed name carries a listener
    // that takes no payload: the runtime dispatches it on the name it sees.
    const Type payload = literal ? listenerParamType(event) : Type::unknown();
    if (!literal && listenerArity(listener, effectiveType(listener)) != 0) {
        reportTypeError(first, "the stream event name must be a string literal "
                                "(\"data\", \"end\" or \"error\"): the listener's "
                                "payload type depends on it");
        return "0.0";
    }
    std::vector<std::string> context;
    if (!payload.isUnknown()) {
        // The chunk is the nominal std/buffer `Buffer`; the error message is
        // a string. The spelling types the lambda's parameter.
        context = {classify(payload).cpp};
    } else if (literal && event != "end") {
        // A stream event outside the documented set (docs 04 §fs: "data",
        // "end", "error"): a program error at the use site, not a missing
        // feature.
        reportTypeError(c, "type mismatch: stream event '" + event +
                              "' is not one of 'data', 'end', 'error'");
        return "0.0";
    }
    needsEventLoop_ = true;
    const std::string call = "(" + expr(*m.object) + ").on(" + expr(first) + ", ";
    if (listener.kind != NodeKind::ArrowFunction) {
        // A function value: a named function, or a binding holding one. The
        // runtime's overloads pick the shape that fits the event.
        const std::string problem =
            listenerDiagnostic(effectiveType(listener), payload, event);
        if (!problem.empty()) {
            reportTypeError(listener, problem);
            return "0.0";
        }
        return call + expr(listener) + ")";
    }
    const auto& arrow = static_cast<const ArrowFunction&>(listener);
    // Type the lambda's own parameter names for the body emission only:
    // sema keeps callback parameters permissive, so `chunk.toString()`
    // inside the body needs this declaration to lower through std/buffer.
    pushScope();
    const Parameter* p =
        arrow.params.empty() ? nullptr : arrow.params.front().get();
    if (p && p->name && p->name->kind == NodeKind::Identifier &&
        !payload.isUnknown()) {
        declare(static_cast<const Identifier&>(*p->name).name, payload);
    }
    const std::string lambda = emitArrow(arrow, context);
    popScope();
    return call + lambda + ")";
}

}  // namespace cp3
