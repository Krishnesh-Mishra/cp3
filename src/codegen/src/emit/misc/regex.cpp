// =============================================================================
// src/codegen/src/emit/regex.cpp - core regex literal lowering (D48).
//
// OWNERSHIP: codegen module. One unit per feature: the `/pattern/flags`
// literal, the documented `.test`/`.match`/`.exec` calls and the `.source` /
// `.flags` reads, all lowering 1:1 onto cp3/core/regex.hpp
// (namespace cp3::runtime::regex). `needsRegex_` makes buildSource/buildHeader
// add the include; the usage-based scan in code_generator.cpp covers a Regex
// spelling that only appears in a declaration (a parameter or export).
//
// A member outside the documented surface returns "" so the caller reports a
// use-site error; sema has already rejected it as CP3305 for the closed RegExp
// surface, so that path is defensive only.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

namespace detail {

// The core regex value type (a nominal class named "RegExp") -> the runtime's
// `::cp3::runtime::regex::Regex`. `classify` calls this from its Class arm.
bool classifyRegex(const Type& t, CppInfo& info) {
    if (t.kind != TypeKind::Class || t.name != "RegExp") return false;
    info.shape = Shape::Unknown;  // no RegExp-specific shape paths exist yet
    info.representable = true;
    info.cpp = "::cp3::runtime::regex::Regex";
    return true;
}

}  // namespace detail

using namespace detail;

namespace {

// Is `t` the nominal RegExp value type? (The codegen-side twin of sema's
// isRegexClass; codegen cannot include sema's private headers.)
bool isRegexValue(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "RegExp";
}

}  // namespace

// `/pattern/flags` -> `Regex::compile(pattern, flags)`. The literal's text is
// exactly what the lexer stored (pattern without delimiters, flags in source
// order); the runtime canonicalizes the flags for `.flags`.
std::string Generator::emitRegexLiteral(const RegExpLiteral& lit) {
    needsRegex_ = true;
    return "::cp3::runtime::regex::Regex::compile(" +
           cppStringLiteral(lit.pattern) + ", " + cppStringLiteral(lit.flags) +
           ")";
}

// `r.test(s)` / `r.match(s)` / `r.exec(s)`: the runtime member call, the
// receiver first (an instance member, not a free function).
std::string Generator::emitRegexCall(const CallExpr& c, const MemberExpr& m) {
    if (!m.object) return std::string();
    if (!isRegexValue(effectiveType(*m.object))) return std::string();
    if (m.property != "test" && m.property != "match" && m.property != "exec") {
        return std::string();
    }
    const std::vector<std::string> args = emitPlainArgs(c);
    if (args.size() != 1) return std::string();
    needsRegex_ = true;
    return "(" + expr(*m.object) + ")." + m.property + "(" + args.front() + ")";
}

// `r.source` / `r.flags`: the documented reads. The runtime spells them as
// accessor calls on the value (no `.lastIndex` is exposed in M1).
std::string Generator::emitRegexMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    if (!isRegexValue(effectiveType(*m.object))) return std::string();
    if (m.property != "source" && m.property != "flags") return std::string();
    needsRegex_ = true;
    return "(" + expr(*m.object) + ")." + m.property + "()";
}

}  // namespace cp3
