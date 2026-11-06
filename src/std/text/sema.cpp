// =============================================================================
// src/std/text/sema.cpp - `std/text` `TextEncoder` / `TextDecoder` typing.
//
// OWNERSHIP: the std/text family (src/std/text/**). One per-module check
// unit; the core checkers consult it through the same hooks the other class
// families use: `inferTextConstruct` (expr_check.cpp's `inferNew`) for
// `new TextEncoder(...)` / `new TextDecoder(...)`, `textMember`
// (member_check.cpp's `memberOfProbe` Class branch) for
// `enc.encode(...)` / `dec.decode(...)` / the `encoding` reads, and
// `resolveNamedType` (type_check.cpp) for the annotation spelling. The member
// tables below ARE the docs surface (docs/content/03-standard-library
// /20-text.md): they drive member typing, the CP3305 candidate list and the
// editor documentation at once, so the three cannot drift apart. text is a
// CLOSED family: a member outside the table is CP3305, never an untyped
// fallthrough.
//
// The typed table mirrors the pinned surface the run cases exercise:
//   TextEncoder: encode(text: string): Uint8Array, encoding: string
//   TextDecoder: decode(bytes: Uint8Array): string, encoding: string
//   new TextEncoder(encoding?) / new TextDecoder(encoding?)
//
// Only UTF-8 exists. A string-literal encoding outside `"utf-8"` / `"utf8"`
// (case-insensitive) is a compile-time error (CP3401 `kTypeMismatch`): the
// argument carries a value the target cannot hold. A non-literal encoding
// cannot be checked statically, so the ABI header (cp3/std/text.hpp)
// validates it at runtime with a catchable error instead of decoding wrongly.
//
// `TextEncoder` / `TextDecoder` are nominal `TypeKind::Class` types named
// "TextEncoder" / "TextDecoder" (like the std/buffer `Buffer` and the
// std/date `Date`). Their value bindings come from
// `import { TextEncoder, TextDecoder } from "std/text"`; an annotation
// resolves only while that ambient binding is in scope, so the names never
// leak into programs that do not import the module. `Uint8Array` is the
// JS-compat family's nominal class (src/language/typed-arrays/sema.cpp).
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

// The task-spec normalization shared with the ABI header (cp3/std/text.hpp):
// trim ASCII whitespace, lowercase.
std::string normalizeEncodingLabel(const std::string& encoding) {
    std::string label;
    label.reserve(encoding.size());
    for (char c : encoding) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
            c == '\v') {
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        label.push_back(c);
    }
    return label;
}

bool isUtf8Label(const std::string& encoding) {
    const std::string label = normalizeEncodingLabel(encoding);
    return label == "utf-8" || label == "utf8";
}

Type uint8ArrayType() { return Type::named(TypeKind::Class, "Uint8Array"); }

Type encoderValue() { return Type::named(TypeKind::Class, "TextEncoder"); }
Type decoderValue() { return Type::named(TypeKind::Class, "TextDecoder"); }

Type encodeMember() {
    return Type::function({Type::string()}, uint8ArrayType());
}

Type decodeMember() {
    return Type::function({uint8ArrayType()}, Type::string());
}

// The documented members of a `TextEncoder` (docs 20-text).
const DocRow kTextEncoderDocRows[] = {
    {"encode", "encode(text: string): Uint8Array",
     "Returns the UTF-8 bytes of `text` as a fresh `Uint8Array`."},
    {"encoding", "encoding: string",
     "The canonical encoding label: always `\"utf-8\"`."},
};

// The documented members of a `TextDecoder` (docs 20-text).
const DocRow kTextDecoderDocRows[] = {
    {"decode", "decode(bytes: Uint8Array): string",
     "Decodes `bytes` as UTF-8, replacing each invalid maximal subpart with "
     "U+FFFD; a leading BOM is stripped."},
    {"encoding", "encoding: string",
     "The canonical encoding label: always `\"utf-8\"`."},
};

template <std::size_t N>
std::vector<std::string> candidateNames(const DocRow (&rows)[N]) {
    std::vector<std::string> out;
    out.reserve(N);
    for (const DocRow& row : rows) out.emplace_back(row.name);
    return out;
}

}  // namespace

// =============================================================================
// The family model (shared with scopes.h and member_check.cpp)
// =============================================================================
bool isTextClass(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == "TextEncoder" || t.name == "TextDecoder");
}

bool isTextTypeName(const std::string& name) {
    return name == "TextEncoder" || name == "TextDecoder";
}

// -----------------------------------------------------------------------------
// The constructor encoding argument
// -----------------------------------------------------------------------------

// True when the encoding argument is acceptable. A string literal must be
// `"utf-8"` / `"utf8"` (case-insensitive); any other literal is CP3401 (the
// argument carries a value the target cannot hold). A non-literal is left to
// the runtime check in cp3/std/text.hpp.
bool Analyzer::checkTextEncoding(const Expr* arg, const std::string& cls) {
    if (arg == nullptr || arg->kind != NodeKind::StringLiteral) return true;
    const std::string& enc = static_cast<const StringLiteral&>(*arg).value;
    if (isUtf8Label(enc)) return true;
    reportAt(diagcode::kTypeMismatch, arg->span,
             "type mismatch: encoding \"" + enc + "\" is not supported by " +
                 cls + " (only \"utf-8\")");
    return false;
}

// =============================================================================
// `new TextEncoder(...)` / `new TextDecoder(...)`
// =============================================================================
Type Analyzer::inferTextConstruct(const std::string& name, ExprList& args,
                                  const SourceSpan& span) {
    if (name != "TextEncoder" && name != "TextDecoder") {
        return Type::unknown();
    }
    std::vector<Type> types;
    types.reserve(args.size());
    for (auto& a : args) {
        types.push_back(a ? inferExpr(*a) : Type::unknown());
    }
    if (args.size() > 1) {
        report(diagcode::kArityMismatch, span,
               "arity mismatch: expected 0 or 1, got " +
                   std::to_string(args.size()) + " arguments");
        return Type::error();
    }
    if (!args.empty()) {
        if (!isStringish(types[0]) && !isPermissive(types[0])) {
            report(diagcode::kTypeMismatch, args.front()->span,
                   "type mismatch: '" + pretty(types[0]) +
                       "' is not an encoding label");
            return Type::error();
        }
        if (!checkTextEncoding(args.front().get(), name)) {
            return Type::error();
        }
    }
    return name == "TextEncoder" ? encoderValue() : decoderValue();
}

// =============================================================================
// The member surface
// =============================================================================
Type Analyzer::textMember(const Type& recv, const std::string& prop,
                          const SourceSpan& span) {
    if (recv.name == "TextEncoder") {
        if (prop == "encode") return encodeMember();
        if (prop == "encoding") return Type::string();
        reportMemberMiss("TextEncoder", prop, span,
                         candidateNames(kTextEncoderDocRows));
        return Type::unknown();
    }
    if (prop == "decode") return decodeMember();
    if (prop == "encoding") return Type::string();
    reportMemberMiss("TextDecoder", prop, span,
                     candidateNames(kTextDecoderDocRows));
    return Type::unknown();
}

StdMemberDoc textMemberDoc(const std::string& family,
                           const std::string& prop) {
    if (family == "TextEncoder") return memberDoc(kTextEncoderDocRows, prop);
    if (family == "TextDecoder") return memberDoc(kTextDecoderDocRows, prop);
    return StdMemberDoc{};
}

std::vector<std::string> textMemberNames(const std::string& family) {
    if (family == "TextEncoder") return docRowNames(kTextEncoderDocRows);
    if (family == "TextDecoder") return docRowNames(kTextDecoderDocRows);
    return {};
}

}  // namespace cp3
