// =============================================================================
// src/std/path/sema.cpp - `std/path` member typing and documentation.
//
// OWNERSHIP: the std/path family (src/std/path/**). The member table moved
// here from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit
// keeps the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames)
// and forwards here, so the rest of sema never needs to change for this
// family.
//
// The same table drives member typing, the CP3305 candidate list and the
// documentation the editor shows, so the three can never drift apart. path is
// a CLOSED family (std_check.cpp isClosedStdNamespace): a member outside the
// table is CP3305 with these names as candidates, never an untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, path section): Node-style
// path utilities.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type stringValue() { return Type::string(); }

Type twoStringToString() {
    return Type::function({Type::string(), Type::string()}, Type::string());
}
Type stringToString() { return Type::function({Type::string()}, Type::string()); }
Type stringToBool() {
    return Type::function({Type::string()}, Type::boolean());
}
Type optionalStringToString() {
    return withOptionalTail(twoStringToString());
}
Type variadicStringToString() {
    Type fn = Type::function({Type::string()}, Type::string());
    fn.name = kVariadicLabel;
    return fn;
}

const MemberRow kPathRows[] = {
    {"sep", stringValue, "sep: string",
     "The platform's path separator: `/` on macOS and Linux, `\\` on "
     "Windows."},
    {"join", variadicStringToString, "join(...parts: string[]): string",
     "Joins the parts into one path, separated by the platform separator."},
    {"resolve", variadicStringToString, "resolve(...parts: string[]): string",
     "Resolves the parts into an absolute path, normalizing `.` and `..` "
     "segments."},
    {"extname", stringToString, "extname(p: string): string",
     "Returns the extension of `p` including the dot (`.txt`), or an empty "
     "string when `p` has none."},
    {"dirname", stringToString, "dirname(p: string): string",
     "Returns the directory part of `p`."},
    {"normalize", stringToString, "normalize(p: string): string",
     "Normalizes `p`, resolving the `.` and `..` segments."},
    {"basename", optionalStringToString, "basename(p: string, ext?: string): string",
     "Returns the last component of `p`; with `ext`, that extension is "
     "removed from the result."},
    {"isAbsolute", stringToBool, "isAbsolute(p: string): boolean",
     "True when `p` is an absolute path."},
};

}  // namespace

std::optional<Type> Analyzer::pathMember(const std::string& prop) {
    return lookupMember(kPathRows, prop);
}

std::vector<std::string> pathMemberNames() { return memberNames(kPathRows); }

StdMemberDoc pathMemberDoc(const std::string& prop) {
    return memberDoc(kPathRows, prop);
}

}  // namespace cp3
