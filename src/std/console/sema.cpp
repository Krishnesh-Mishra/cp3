// =============================================================================
// src/std/console/sema.cpp - `std/console` member typing.
//
// OWNERSHIP: the std/console family (src/std/console/**). The implemented-set
// table and the member function moved here from
// src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit keeps the
// namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames) and
// forwards here, so the rest of sema never needs to change for this family.
//
// Console is a CLOSED but surface-aware family (std_check.cpp
// isClosedStdNamespace): the implemented set below is what a typo is ranked
// against, so a member that would just report CP3002 is never recommended.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// Console's implemented set: a typo must never recommend a member that would
// just report CP3002, so the documented-but-pending members are not candidates.
const char* const kConsoleImplemented[] = {"log", "info", "warn", "error"};

}  // namespace

std::optional<Type> Analyzer::consoleMember(const std::string& prop) {
    // Surface-aware console (the user-facing typo example): the four
    // members codegen lowers today type as ambient (permissive signatures);
    // the documented but still pending ones keep the ambient fallthrough so
    // their CP3002 skip survives until the console wave lands.
    static const char* const kPending[] = {"assert", "clear", "input", "readKey"};
    for (const char* name : kConsoleImplemented) {
        if (prop == name) return ambient();
    }
    for (const char* name : kPending) {
        if (prop == name) return ambient();
    }
    return std::nullopt;
}

std::vector<std::string> consoleMemberNames() {
    std::vector<std::string> names;
    for (const char* name : kConsoleImplemented) names.push_back(name);
    return names;
}

}  // namespace cp3
