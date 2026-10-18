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
//   stat.size               -> static_cast<double>((stat).size)  and the
//                              nullary `isFile()` / `isDirectory()` call form)
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
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/builtin_tables.h"
#include "detail/cpp_writer.h"
#include "detail/type_map.h"

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
    notImplemented(c, "std/fs stream member '" + m.property + "'");
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
    if (m.property == "size") return "static_cast<double>(" + field + ")";
    return field;
}

// =============================================================================
// `stream.on(event, listener)`
// =============================================================================
std::string Generator::emitFsStreamOn(const CallExpr& c, const MemberExpr& m) {
    if (c.arguments.size() < 2 || !c.arguments[0] || !c.arguments[1]) {
        notImplemented(c, "stream.on(event, listener)");
        return "0.0";
    }
    const Expr& first = *c.arguments[0];
    if (first.kind != NodeKind::StringLiteral) {
        notImplemented(first, "a literal stream event name");
        return "0.0";
    }
    const std::string event = static_cast<const StringLiteral&>(first).value;
    const Type param = listenerParamType(event);
    std::vector<std::string> context;
    if (!param.isUnknown()) {
        // The chunk is the nominal std/buffer `Buffer`; the error message is a
        // string. Handing the spelling to emitArrow also types the lambda's
        // parameter for callers of this unit.
        context = {classify(param).cpp};
    } else if (event != "end") {
        notImplemented(c, "stream event '" + event + "'");
        return "0.0";
    }
    const Expr& listener = *c.arguments[1];
    if (listener.kind != NodeKind::ArrowFunction) {
        notImplemented(c, "a function listener");
        return "0.0";
    }
    const auto& arrow = static_cast<const ArrowFunction&>(listener);
    // Type the lambda's own parameter names for the body emission only: sema
    // keeps callback parameters permissive, so `chunk.toString()` inside the
    // body needs this declaration to lower through the std/buffer unit. The
    // temporary scope keeps the names out of the enclosing scope.
    pushScope();
    if (!arrow.params.empty()) {
        const Parameter* p = arrow.params.front().get();
        if (p && p->name && p->name->kind == NodeKind::Identifier && !param.isUnknown()) {
            declare(static_cast<const Identifier&>(*p->name).name, param);
        }
    }
    const std::string lambda = emitArrow(arrow, context);
    popScope();
    needsEventLoop_ = true;
    return "(" + expr(*m.object) + ").on(" + expr(first) + ", " + lambda + ")";
}

}  // namespace cp3
