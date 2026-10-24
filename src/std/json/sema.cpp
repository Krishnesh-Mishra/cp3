// =============================================================================
// src/std/json/sema.cpp - `std/json` member typing and documentation.
//
// OWNERSHIP: the std/json family (src/std/json/**). The member table moved
// here from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit
// keeps the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames)
// and forwards here, so the rest of sema never needs to change for this
// family.
//
// The same table drives member typing, the CP3305 candidate list and the
// documentation the editor shows, so the three can never drift apart. JSON is
// a CLOSED family (std_check.cpp isClosedStdNamespace): a member outside the
// table is CP3305 with these names as candidates, never an untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, JSON section).
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
    {"parse", jsonParseSignature, "parse<T>(text: string): T",
     "Parses `text` as JSON.\n\n"
     "Without a type argument the result is `unknown` and has to be narrowed "
     "before use. With `parse<T>` the parsed value is validated against `T`'s "
     "shape at runtime, and a mismatch throws an ordinary catchable error."},
    {"stringify", jsonStringifySignature,
     "stringify<T>(value: T, indent?: number): string",
     "Serializes `value` as JSON text. With `indent`, the result is "
     "pretty-printed with that indentation."},
};

}  // namespace

std::optional<Type> Analyzer::jsonMember(const std::string& prop) {
    return lookupMember(kJsonRows, prop);
}

std::vector<std::string> jsonMemberNames() { return memberNames(kJsonRows); }

StdMemberDoc jsonMemberDoc(const std::string& prop) {
    return memberDoc(kJsonRows, prop);
}

}  // namespace cp3
