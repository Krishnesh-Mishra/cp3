// =============================================================================
// src/std/json/sema.cpp - `std/json` member typing.
//
// OWNERSHIP: the std/json family (src/std/json/**). The member table moved
// here from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit
// keeps the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames)
// and forwards here, so the rest of sema never needs to change for this
// family.
//
// The same table drives member typing and the CP3305 candidate list, so the
// two can never drift apart. JSON is a CLOSED family
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

// `(text: string) => unknown` / `(value: unknown, indent?: number) => string`.
// The real call typing stays in tryInferJsonCall (call_check.cpp); these rows
// only mark the surface closed and give member reads a type.
Type jsonParseSignature() {
    return Type::function({Type::string()}, Type::unknown());
}
Type jsonStringifySignature() {
    return withOptionalTail(
        Type::function({Type::unknown(), Type::number()}, Type::string()));
}

const MemberRow kJsonRows[] = {
    {"parse", jsonParseSignature},
    {"stringify", jsonStringifySignature},
};

}  // namespace

std::optional<Type> Analyzer::jsonMember(const std::string& prop) {
    return lookupMember(kJsonRows, prop);
}

std::vector<std::string> jsonMemberNames() { return memberNames(kJsonRows); }

}  // namespace cp3
