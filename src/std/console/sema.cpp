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
// against with "did you mean". Since the round-1 wave all twelve documented
// members are implemented, so every one of them is a suggestion candidate and
// an unknown member is exactly the documented-shape CP3305.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// The twelve documented members (docs/standard-library/04-imported-std-modules.md
// §console): log, info, warn, error, input, readKey, clear, assert, on, off,
// hide, show. All are real, so all are candidate suggestions for a typo.
const char* const kConsoleImplemented[] = {
    "log",   "info",  "warn", "error", "input", "readKey",
    "clear", "assert", "on",   "off",   "hide",  "show",
};

}  // namespace

std::optional<Type> Analyzer::consoleMember(const std::string& prop) {
    // The listener surface (D75) has a real signature: `on(event, listener)`
    // and `off(event, listener)` take a string event and a listener. The
    // signature is what gives the call its arity check (CP3403) and the
    // event's type check (CP3401); the listener itself stays permissive,
    // because a listener may declare the value parameter or none.
    if (prop == "on" || prop == "off") {
        return Type::function({Type::string(),
                               Type::function({Type::string()}, Type::voidType())},
                              Type::voidType());
    }
    // The user-facing typo example: the documented members type as ambient
    // (permissive signatures, like the other std families' surface members),
    // and anything else keeps the CP3305 closed-shape report with the
    // implemented set as candidates.
    for (const char* name : kConsoleImplemented) {
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
