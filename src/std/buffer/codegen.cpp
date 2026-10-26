// =============================================================================
// src/std/buffer/codegen.cpp - `std/buffer` `Buffer` lowering.
//
// OWNERSHIP: the std/buffer family (src/std/buffer/**). One per-module lowering
// unit; the core dispatch rows already exist (emit/std/common.cpp's
// emitBuiltinCall / emitBuiltinMember and type_map.cpp's classify), so the rest
// of codegen never needs to change for this module.
//
// Every documented form lowers 1:1 onto the header-only runtime surface in
// cp3/std/buffer.hpp (namespace cp3::runtime::buffer):
//   Buffer.from(input[, enc]) -> buffer::from(input[, enc])
//   Buffer.alloc(size)        -> buffer::alloc(size)
//   buf.toString([enc])       -> buffer::toString(buf[, enc])
//   buf.slice(start[, end])   -> buffer::slice(buf, start[, end])
//   buf.length                -> buffer::size(buf)
// A member outside the documented table reports CP3305 at the use site; an
// arity sema already rejected reports CP3400. The C++ type spelling is
// `::cp3::runtime::buffer::Buffer`; `needsBuffer_` makes buildSource add the
// include (and the cp3_ts/cp3_truthy overloads).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace detail {

// The family's C++ mapping (see type_map.h): the nominal `Buffer` class type.
// Every other type is left for `classify`'s own arms.
bool classifyByteBuffer(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class || t.name != "Buffer") return false;
    info.shape = Shape::Buffer;
    info.representable = true;
    info.cpp = "::cp3::runtime::buffer::Buffer";
    return true;
}

}  // namespace detail

using namespace detail;

// `Buffer.from(...)` / `Buffer.alloc(...)`: the statics on the std/buffer
// ambient binding (the same recognizer the other std namespaces use).
std::string Generator::emitByteBufferStaticCall(const CallExpr& c,
                                                const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "Buffer")) return std::string();
    // A local value named `Buffer` shadows the module: its members are not the
    // std/buffer statics.
    if (hasValueBinding("Buffer")) return std::string();
    if (m.property != "from" && m.property != "alloc") {
        reportCodegenError(c, diagcode::kMemberNotFound,
                           "Buffer has no static method '" + m.property + "'");
        return "0.0";
    }
    needsBuffer_ = true;
    return "::cp3::runtime::buffer::" + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

// `buf.toString([enc])` / `buf.slice(start[, end])`: the member calls on a
// Buffer receiver. The receiver is the first runtime argument in every case.
std::string Generator::emitByteBufferMethodCall(const CallExpr& c,
                                                const MemberExpr& m) {
    if (!m.object) return std::string();
    if (info(*m.object).shape != Shape::Buffer) return std::string();
    if (m.property != "toString" && m.property != "slice") {
        reportCodegenError(c, diagcode::kMemberNotFound,
                           "Buffer has no method '" + m.property + "'");
        return "0.0";
    }
    const std::vector<std::string> args = emitPlainArgs(c);
    const std::size_t maxArgs = m.property == "toString" ? 1 : 2;
    if (args.size() > maxArgs) {
        // Sema reports the arity before codegen; keep the use site honest if
        // the lowering is ever reached without it.
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: Buffer." + m.property +
                               " takes at most " + std::to_string(maxArgs) +
                               " argument(s)");
        return "0.0";
    }
    needsBuffer_ = true;
    std::string call =
        "::cp3::runtime::buffer::" + m.property + "(" + expr(*m.object);
    for (const std::string& arg : args) call += ", " + arg;
    return call + ")";
}

// `buf.length`: the byte count (the runtime helper already yields a double).
std::string Generator::emitByteBufferMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    if (info(*m.object).shape != Shape::Buffer) return std::string();
    if (m.property != "length") {
        // The documented instance surface is `length`, `toString` and
        // `slice`; a method read as a value stays with the ordinary function
        // path, while any other name is a closed-surface miss.
        if (m.property == "toString" || m.property == "slice") {
            return std::string();
        }
        reportCodegenError(m, diagcode::kMemberNotFound,
                           "Buffer has no member '" + m.property + "'");
        return "0.0";
    }
    needsBuffer_ = true;
    return "::cp3::runtime::buffer::size(" + expr(*m.object) + ")";
}

}  // namespace cp3
