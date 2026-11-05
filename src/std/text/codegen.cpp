// =============================================================================
// src/std/text/codegen.cpp - `std/text` `TextEncoder` / `TextDecoder` lowering.
//
// OWNERSHIP: the std/text family (src/std/text/**). One per-module lowering
// unit; the core dispatch rows already exist (emit/std/common.cpp's
// emitBuiltinCall / emitBuiltinMember, expr.cpp's emitNewExprValue and
// type_map.cpp's classifyClassValue), so the rest of codegen never needs to
// change for this module.
//
// Every documented form lowers 1:1 onto the header-only runtime surface in
// cp3/std/text.hpp (namespace cp3::runtime::text):
//   new TextEncoder([enc])  -> ::cp3::runtime::text::TextEncoder([enc])
//   new TextDecoder([enc])  -> ::cp3::runtime::text::TextDecoder([enc])
//   enc.encode(text)        -> (enc).encode(text)
//   dec.decode(bytes)       -> (dec).decode(bytes)
//   enc.encoding            -> (enc).encoding()
//   dec.encoding            -> (dec).encoding()
// A member outside the documented table reports CP3305 at the use site; an
// arity sema already rejected reports CP3403. The C++ value spellings are
// `::cp3::runtime::text::TextEncoder` / `::cp3::runtime::text::TextDecoder`
// (the std/date value-type pattern, not a `Ref` handle); `needsText_` makes
// buildSource add the include. The `Uint8Array` argument/result spelling
// comes from the JS-compat family (cp3/typed_array.hpp), so `needsTypedArray_`
// is set alongside.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace detail {

// The family's C++ mapping (see type_map.h): the two nominal class values.
// Like the Date value (src/std/date/codegen.cpp), there is no text-specific
// shape yet; both spell to their plain runtime value types.
bool classifyText(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class) return false;
    if (t.name != "TextEncoder" && t.name != "TextDecoder") return false;
    info.shape = Shape::Unknown;
    info.representable = true;
    info.cpp = "::cp3::runtime::text::" + t.name;
    return true;
}

}  // namespace detail

using namespace detail;

namespace {

// The codegen-side twin of sema's `isTextClass` (codegen cannot include
// sema's private headers).
bool isTextValue(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == "TextEncoder" || t.name == "TextDecoder");
}

}  // namespace

// `new TextEncoder([encoding])` / `new TextDecoder([encoding])`: the optional
// encoding argument passes through; the runtime constructor validates it and
// throws a catchable cp3 error for anything but UTF-8.
std::string Generator::emitTextNew(const NewExpr& n) {
    const Type t = effectiveType(n);
    if (!isTextValue(t)) return std::string();
    if (n.arguments.size() > 1) {
        reportCodegenError(n, diagcode::kArityMismatch,
                           "arity mismatch: expected 0 or 1, got " +
                               std::to_string(n.arguments.size()) +
                               " arguments");
        return "0.0";
    }
    needsText_ = true;
    std::string args;
    for (const auto& arg : n.arguments) {
        if (!args.empty()) args += ", ";
        args += arg ? expr(*arg) : "0.0";
    }
    return "::cp3::runtime::text::" + t.name + "(" + args + ")";
}

// `enc.encode(text)` / `dec.decode(bytes)`: the receiver is the object of the
// plain runtime member call.
std::string Generator::emitTextMemberCall(const CallExpr& c,
                                          const MemberExpr& m) {
    if (!m.object) return std::string();
    if (!isTextValue(effectiveType(*m.object))) return std::string();
    if (m.property != "encode" && m.property != "decode") {
        reportCodegenError(c, diagcode::kMemberNotFound,
                           "'" + effectiveType(*m.object).name +
                               "' has no method '" + m.property + "'");
        return "0.0";
    }
    needsText_ = true;
    // `encode` returns the family's `Uint8Array`, whose spelling lives in
    // cp3/typed_array.hpp; `decode` takes one.
    needsTypedArray_ = true;
    return "(" + expr(*m.object) + ")." + m.property + "(" +
           joinStrings(emitPlainArgs(c), ", ") + ")";
}

// `enc.encoding` / `dec.encoding`: the canonical `"utf-8"` label.
std::string Generator::emitTextMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    if (!isTextValue(effectiveType(*m.object))) return std::string();
    if (m.property != "encoding") {
        // `encode` / `decode` read as values keep the ordinary function
        // path; any other name is a closed-surface miss.
        if (m.property == "encode" || m.property == "decode") {
            return std::string();
        }
        reportCodegenError(m, diagcode::kMemberNotFound,
                           "'" + effectiveType(*m.object).name +
                               "' has no member '" + m.property + "'");
        return "0.0";
    }
    needsText_ = true;
    return "(" + expr(*m.object) + ").encoding()";
}

}  // namespace cp3
