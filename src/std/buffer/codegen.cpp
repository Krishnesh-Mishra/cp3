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
// arity sema already rejected reports CP3403. The C++ type spelling is
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
    // `Buffer.from(a + b.toString() + c.toString())`: build the byte sequence
    // directly from the parts. The Buffer operands are appended as views, so
    // the 64 KiB copies the toString round trip would make disappear.
    if (m.property == "from" && c.arguments.size() == 1 &&
        c.arguments.front() &&
        c.arguments.front()->kind != NodeKind::SpreadElement &&
        info(*c.arguments.front()).shape == Shape::String) {
        std::vector<const Expr*> parts;
        auto collect = [&](auto&& self, const Expr& e) -> void {
            if (e.kind == NodeKind::BinaryExpr) {
                const auto& b = static_cast<const BinaryExpr&>(e);
                if (b.op == BinaryOp::Add && b.left && b.right &&
                    info(*b.left).shape == Shape::String &&
                    info(*b.right).shape == Shape::String) {
                    self(self, *b.left);
                    self(self, *b.right);
                    return;
                }
            }
            parts.push_back(&e);
        };
        collect(collect, *c.arguments.front());
        if (parts.size() >= 2) {
            // `buf.toString()` with the default (or literal "utf8") encoding
            // is exactly the buffer's bytes; append those bytes directly. Any
            // other encoding stays on the normal toString path.
            auto bufferReceiver = [&](const Expr& e) -> const Expr* {
                if (e.kind != NodeKind::CallExpr) return nullptr;
                const auto& call = static_cast<const CallExpr&>(e);
                if (!call.callee ||
                    call.callee->kind != NodeKind::MemberExpr) {
                    return nullptr;
                }
                const auto& member =
                    static_cast<const MemberExpr&>(*call.callee);
                if (member.property != "toString" || !member.object) {
                    return nullptr;
                }
                if (info(*member.object).shape != Shape::Buffer) return nullptr;
                if (call.arguments.empty()) return member.object.get();
                if (call.arguments.size() != 1 || !call.arguments.front()) {
                    return nullptr;
                }
                const Expr& arg = *call.arguments.front();
                if (arg.kind != NodeKind::StringLiteral) return nullptr;
                return static_cast<const StringLiteral&>(arg).value == "utf8"
                           ? member.object.get()
                           : nullptr;
            };
            std::string call = "::cp3::runtime::buffer::fromConcat(";
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i != 0) call += ", ";
                if (const Expr* buffer = bufferReceiver(*parts[i])) {
                    call += expr(*buffer);
                } else {
                    call += expr(*parts[i]);
                }
            }
            return call + ")";
        }
    }
    // `Buffer.alloc(size)`: `size` is the std length width. A Rust/C++
    // project's argument is already `u64` (cast so a widened literal picks
    // the exact overload); a TypeScript project's `number` passes through
    // to the `double` overload, keeping fractional/negative validation.
    if (m.property == "alloc" && c.arguments.size() == 1 &&
        options_.typeSystem != TypeSystem::TypeScript) {
        return "::cp3::runtime::buffer::alloc(static_cast<std::uint64_t>(" +
               joinStrings(emitPlainArgs(c), ", ") + "))";
    }
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
    for (const std::string& arg : args) {
        call += ", ";
        // `slice` bounds are the std signed-index width: cast in a Rust/C++
        // project so the exact overload is picked; a TypeScript project's
        // `number` keeps the `double` overload (trunc/NaN JS semantics).
        if (m.property == "slice" &&
            options_.typeSystem != TypeSystem::TypeScript) {
            call += "static_cast<std::int64_t>(" + arg + ")";
        } else {
            call += arg;
        }
    }
    return call + ")";
}

// `buf.length`: the exact std length width (`usize`/`size_t`); a TypeScript
// project keeps `number`, so the read widens there.
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
    const std::string read =
        "::cp3::runtime::buffer::size(" + expr(*m.object) + ")";
    if (options_.typeSystem == TypeSystem::TypeScript) {
        return "static_cast<double>(" + read + ")";
    }
    return read;
}

}  // namespace cp3
