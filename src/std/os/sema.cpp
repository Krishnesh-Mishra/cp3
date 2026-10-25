// =============================================================================
// src/std/os/sema.cpp - `std/os` member typing and documentation.
//
// OWNERSHIP: the std/os family (src/std/os/**). The member table moved here
// from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit keeps
// the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames) and
// forwards here, so the rest of sema never needs to change for this family.
//
// The same table drives member typing, the CP3305 candidate list and the
// documentation the editor shows, so the three can never drift apart. os is a
// CLOSED family (std_check.cpp isClosedStdNamespace): a member outside the
// table is CP3305 with these names as candidates, never an untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, os section): Node-style
// OS info, a thin wrapper over the OS calls.
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
    {"platform", zeroToString, "platform(): string",
     "Returns the platform name: `\"windows\"`, `\"macos\"` or `\"linux\"`."},
    {"homedir", zeroToString, "homedir(): string",
     "Returns the path of the current user's home directory."},
    {"tmpdir", zeroToString, "tmpdir(): string",
     "Returns the path of the system's temporary directory."},
    {"hostname", zeroToString, "hostname(): string",
     "Returns the host name of the machine."},
    {"cpus", zeroToNumber, "cpus(): number",
     "Returns the number of logical CPU cores."},
    {"totalMemory", zeroToNumber, "totalMemory(): number",
     "Returns the total amount of system memory in bytes."},
    {"freeMemory", zeroToNumber, "freeMemory(): number",
     "Returns the amount of free system memory in bytes."},
};

}  // namespace

std::optional<Type> Analyzer::osMember(const std::string& prop) {
    return lookupMember(kOsRows, prop);
}

std::vector<std::string> osMemberNames() { return memberNames(kOsRows); }

StdMemberDoc osMemberDoc(const std::string& prop) {
    return memberDoc(kOsRows, prop);
}

}  // namespace cp3
