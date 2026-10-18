// =============================================================================
// src/std/path/sema.cpp - `std/path` member typing.
//
// OWNERSHIP: the std/path family (src/std/path/**). The member table moved
// here from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit
// keeps the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames)
// and forwards here, so the rest of sema never needs to change for this
// family.
//
// The same table drives member typing and the CP3305 candidate list, so the
// two can never drift apart. path is a CLOSED family
// (std_check.cpp isClosedStdNamespace): a member outside the table is CP3305
// with these names as candidates instead of a CP3002 skip.
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
    {"sep", stringValue},
    {"join", variadicStringToString},
    {"resolve", variadicStringToString},
    {"extname", stringToString},
    {"dirname", stringToString},
    {"normalize", stringToString},
    {"basename", optionalStringToString},
    {"isAbsolute", stringToBool},
};

}  // namespace

std::optional<Type> Analyzer::pathMember(const std::string& prop) {
    return lookupMember(kPathRows, prop);
}

std::vector<std::string> pathMemberNames() { return memberNames(kPathRows); }

}  // namespace cp3
