// =============================================================================
// src/sema/src/checks/regex_check.cpp - core regex literal typing (D48).
//
// OWNERSHIP: sema module. The regex literal `/pattern/flags` is core language
// (no import): expr_check.cpp types a RegExpLiteral as `regexLiteralType()`
// and member_check.cpp routes a RegExp receiver's members here. Pattern and
// flag validity belong to the LEXER (CP3107, src/lexer/src/regexp.cpp); sema
// trusts what was lexed.
//
// The surface is CLOSED like Date: a member outside the table is CP3305 with
// the implemented names as candidates instead of a CP3002 skip. The table
// drives member typing and the candidate list at once, so the two can never
// drift apart.
//
// The documented surface (D48, mirrored by cp3/core/regex.hpp):
//   test(subject: string): boolean;
//   match(subject: string): string[];
//   exec(subject: string): string[];
//   source: string; flags: string;
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type regexValue() { return Type::named(TypeKind::Class, "RegExp"); }

Type stringToBoolean() {
    return Type::function({Type::string()}, Type::boolean());
}

Type stringToStrings() {
    return Type::function({Type::string()}, Type::array(Type::string()));
}

// `.source` / `.flags` are plain string PROPERTIES in JS (reading them yields
// a string; there is nothing to call). Codegen lowers the property read to
// the runtime's accessor call, so the type here stays `string`.
Type stringValue() { return Type::string(); }

const MemberRow kRegexInstanceRows[] = {
    {"test", stringToBoolean},
    {"match", stringToStrings},
    {"exec", stringToStrings},
    {"source", stringValue},
    {"flags", stringValue},
};

}  // namespace

// A regex literal's type: the nominal class "RegExp" (codegen maps it to
// cp3/core/regex.hpp's cp3::runtime::regex::Regex).
Type regexLiteralType() { return regexValue(); }

// The receiver is part of the signature for symmetry with the other families;
// the RegExp surface does not depend on it.
std::optional<Type> Analyzer::regexInstanceMember(const Type& recv,
                                                  const std::string& prop) {
    (void)recv;
    return lookupMember(kRegexInstanceRows, prop);
}

// The closed-surface probe member_check.cpp calls for a RegExp receiver: a
// miss is CP3305 with the implemented names as candidates instead of the
// permissive ambient fallthrough marker-typed receivers use.
Type Analyzer::memberOfRegex(const Type& recv, const std::string& prop,
                             const SourceSpan& span) {
    if (auto t = regexInstanceMember(recv, prop)) return *t;
    reportMemberMiss("RegExp", prop, span, regexInstanceMemberNames());
    return Type::unknown();
}

// A RegExp value's nominal type (TypeKind::Class, name "RegExp").
bool isRegexClass(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "RegExp";
}

std::vector<std::string> regexInstanceMemberNames() {
    return memberNames(kRegexInstanceRows);
}

}  // namespace cp3
