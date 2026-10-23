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
// A form outside the table returns "" so the caller keeps reporting CP3002.
// The C++ type spelling is `::cp3::runtime::buffer::Buffer`; `needsBuffer_`
// makes buildSource add the include (and the cp3_ts/cp3_truthy overloads).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

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
    if (m.property != "from" && m.property != "alloc") return std::string();
    needsBuffer_ = true;
    return "::cp3::runtime::buffer::" + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

// `buf.toString([enc])` / `buf.slice(start[, end])`: the member calls on a
// Buffer receiver. The receiver is the first runtime argument in every case.
std::string Generator::emitByteBufferMethodCall(const CallExpr& c,
                                                const MemberExpr& m) {
    if (!m.object) return std::string();
    if (m.property != "toString" && m.property != "slice") return std::string();
    if (info(*m.object).shape != Shape::Buffer) return std::string();
    const std::vector<std::string> args = emitPlainArgs(c);
    if (args.size() > 2) return std::string();  // sema reported the arity
    needsBuffer_ = true;
    std::string call =
        "::cp3::runtime::buffer::" + m.property + "(" + expr(*m.object);
    for (const std::string& arg : args) call += ", " + arg;
    return call + ")";
}

// `buf.length`: the byte count (the runtime helper already yields a double).
std::string Generator::emitByteBufferMember(const MemberExpr& m) {
    if (m.property != "length" || !m.object) return std::string();
    if (info(*m.object).shape != Shape::Buffer) return std::string();
    needsBuffer_ = true;
    return "::cp3::runtime::buffer::size(" + expr(*m.object) + ")";
}

}  // namespace cp3
