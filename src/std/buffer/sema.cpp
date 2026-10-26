// =============================================================================
// src/std/buffer/sema.cpp - `std/buffer` `Buffer` surface typing.
//
// OWNERSHIP: the std/buffer family (src/std/buffer/**). One per-module check
// unit; the core checkers consult it through the same hooks the typed-array
// family uses: `memberOfProbe` (member_check.cpp) for `buf.<prop>`,
// `inferCall` (call_check.cpp) for the `Buffer.from`/`Buffer.alloc` statics and
// the `buf.toString`/`buf.slice` calls, `checkAssignToTarget` (ops_check.cpp)
// for writes to `buf.length`, and `resolveNamedType` (type_check.cpp) for the
// `Buffer` annotation.
//
// The typed table mirrors docs/standard-library/04 section buffer, which is
// the pinned surface the run cases exercise:
//   Buffer.from(input: string, encoding?: string): Buffer
//   Buffer.alloc(size: number): Buffer
//   buf.toString(encoding?: string): string
//   buf.slice(start: number, end?: number): Buffer
//   buf.length: number
//
// D47: only `utf8` (default), `base64` and `hex` exist. A literal in any other
// encoding is a compile-time error (CP3401 `kTypeMismatch`: the argument
// carries a value the target cannot hold); a non-literal encoding cannot be
// checked statically, so the ABI header (cp3/std/buffer.hpp) validates it at
// runtime with a catchable error instead of decoding wrongly.
//
// `Buffer` is a nominal `TypeKind::Class` type named "Buffer" (like the
// typed-array classes). Its value binding comes from
// `import { Buffer } from "std/buffer"`; the annotation resolves only while
// that ambient binding is in scope, so `Buffer` never leaks into programs that
// do not import the module.
// =============================================================================
#include "detail/analyzer.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// The family tables (shared with the core hooks)
// =============================================================================
Type byteBufferType() { return Type::named(TypeKind::Class, "Buffer"); }

bool isByteBufferClass(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Buffer";
}

bool isByteBufferTypeName(const std::string& name) { return name == "Buffer"; }

bool isByteBufferReadOnlyProp(const Type& ot, const std::string& prop) {
    return isByteBufferClass(ot) && prop == "length";
}

namespace {

// A literal size argument: a number literal, or a negated number literal.
// `NaN`/`Infinity` are identifiers, so they never take this path (the runtime
// rejects them with a catchable error, like crypto.randomBytes).
bool literalNumberValue(const Expr& e, double& out) {
    if (e.kind == NodeKind::NumberLiteral) {
        out = static_cast<const NumberLiteral&>(e).value;
        return true;
    }
    if (e.kind == NodeKind::UnaryExpr) {
        const auto& u = static_cast<const UnaryExpr&>(e);
        if (u.op == UnaryOp::Negate && u.operand &&
            u.operand->kind == NodeKind::NumberLiteral) {
            out = -static_cast<const NumberLiteral&>(*u.operand).value;
            return true;
        }
    }
    return false;
}

}  // namespace

// -----------------------------------------------------------------------------
// D47: the encoding argument
// -----------------------------------------------------------------------------

// True when the encoding argument is acceptable. A string literal must be one
// of the three implemented names; any other literal is CP3401 (D47 makes it a
// compile-time error). A non-literal is left to the runtime check.
bool Analyzer::checkByteBufferEncoding(const Expr* arg) {
    if (arg == nullptr || arg->kind != NodeKind::StringLiteral) return true;
    const std::string& enc = static_cast<const StringLiteral&>(*arg).value;
    if (enc == "utf8" || enc == "base64" || enc == "hex") return true;
    report(diagcode::kTypeMismatch, arg->span,
           "type mismatch: encoding \"" + enc +
               "\" is not supported (only \"utf8\", \"base64\" and \"hex\")");
    return false;
}

// -----------------------------------------------------------------------------
// Members (member_check.cpp consults `byteBufferMember`)
// -----------------------------------------------------------------------------
Type Analyzer::byteBufferMember(const Type& recv, const std::string& prop) {
    if (prop == "length") return Type::number();
    if (prop == "toString") {
        return withOptionalTail(
            Type::function({Type::string()}, Type::string()));
    }
    if (prop == "slice") {
        return withOptionalTail(
            Type::function({Type::number(), Type::number()}, recv));
    }
    // A member outside the documented surface stays ambient here (the member
    // probe has no span to report against); codegen reports CP3305 at the use
    // site, so an unknown Buffer member is not lowered as an ambient value.
    return ambient();
}

// -----------------------------------------------------------------------------
// Calls (call_check.cpp consults `tryInferBufferCall`)
// -----------------------------------------------------------------------------

// `Buffer.from(input, encoding?)` / `Buffer.alloc(size)` on the std/buffer
// ambient binding (a user value that shadows `Buffer` is not the module).
bool Analyzer::tryInferBufferStaticCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (m.property != "from" && m.property != "alloc") return false;
    if (!m.object || m.object->kind != NodeKind::Identifier) return false;
    auto& id = static_cast<Identifier&>(*m.object);
    if (!isByteBufferTypeName(id.name)) return false;
    Binding b;
    if (!lookup(id.name, b) || !b.isAmbient) return false;
    setType(id, b.type);

    FuncSig sig;
    sig.name = "Buffer." + m.property;
    sig.ret = byteBufferType();
    if (m.property == "alloc") {
        sig.params = {Type::number()};
        sig.optional = {false};
    } else {
        sig.params = {Type::string(), Type::string()};
        sig.optional = {false, true};
    }
    checkArgs(sig, call);

    if (m.property == "alloc") {
        double size = 0.0;
        if (!call.arguments.empty() && call.arguments.front() &&
            literalNumberValue(*call.arguments.front(), size) && size < 0.0) {
            report(diagcode::kTypeMismatch, call.span,
                   "Buffer.alloc size must be a non-negative integer");
            out = withType(call, Type::error());
            return true;
        }
    } else {
        const Expr* enc =
            call.arguments.size() >= 2 ? call.arguments[1].get() : nullptr;
        if (!checkByteBufferEncoding(enc)) {
            out = withType(call, Type::error());
            return true;
        }
    }
    out = withType(call, sig.ret);
    return true;
}

// `inferCall`'s buffer hook: the statics first, then the instance methods
// `buf.toString(encoding?)` / `buf.slice(start, end?)` on a Buffer receiver.
bool Analyzer::tryInferBufferCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (tryInferBufferStaticCall(call, m, out)) return true;
    if (!m.object) return false;
    const Type recv = typeOfExpr(*m.object);
    if (!isByteBufferClass(recv)) return false;

    FuncSig sig;
    sig.name = "Buffer." + m.property;
    if (m.property == "toString") {
        sig.params = {Type::string()};
        sig.optional = {true};
        sig.ret = Type::string();
    } else if (m.property == "slice") {
        sig.params = {Type::number(), Type::number()};
        sig.optional = {false, true};
        sig.ret = recv;
    } else {
        return false;
    }
    checkArgs(sig, call);
    if (m.property == "toString" && !call.arguments.empty() && call.arguments.front() &&
        !checkByteBufferEncoding(call.arguments.front().get())) {
        out = withType(call, Type::error());
        return true;
    }
    out = withType(call, sig.ret);
    return true;
}

}  // namespace cp3
