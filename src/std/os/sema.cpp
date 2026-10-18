// =============================================================================
// src/std/os/sema.cpp - `std/os` member typing.
//
// OWNERSHIP: the std/os family (src/std/os/**). The member table moved here
// from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit keeps
// the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames) and
// forwards here, so the rest of sema never needs to change for this family.
//
// The same table drives member typing and the CP3305 candidate list, so the
// two can never drift apart. os is a CLOSED family
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

Type zeroToString() { return Type::function({}, Type::string()); }

const MemberRow kOsRows[] = {
    {"platform", zeroToString}, {"homedir", zeroToString},
    {"tmpdir", zeroToString},   {"hostname", zeroToString},
    {"cpus", zeroToNumber},     {"totalMemory", zeroToNumber},
    {"freeMemory", zeroToNumber},
};

}  // namespace

std::optional<Type> Analyzer::osMember(const std::string& prop) {
    return lookupMember(kOsRows, prop);
}

std::vector<std::string> osMemberNames() { return memberNames(kOsRows); }

}  // namespace cp3
