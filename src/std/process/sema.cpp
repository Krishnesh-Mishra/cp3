// =============================================================================
// src/std/process/sema.cpp - `std/process` member typing.
//
// OWNERSHIP: the std/process family (src/std/process/**). Moved here from
// src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit keeps the
// namespace dispatch (stdNamespaceMember) and forwards here, so the rest of
// sema never needs to change for this family.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

std::optional<Type> Analyzer::processMember(const std::string& prop) {
    // OPEN family: only the members with a pinned signature are typed, the
    // rest (`exec` / `spawn` / streaming argv) stays permissive (the
    // std_check.cpp file header explains the split).
    if (prop == "exit") {
        return Type::function({Type::number()}, Type::voidType());
    }
    if (prop == "argv") return Type::array(Type::string());
    if (prop == "env") {
        // A minimal fixed shape: `process.env.get(name): string | undefined`.
        std::vector<Type> value;
        value.push_back(Type::string());
        value.push_back(Type::undefined());
        Field get;
        get.name = "get";
        get.type = Type::function({Type::string()}, Type::unionOf(std::move(value)));
        std::vector<Field> fields;
        fields.push_back(std::move(get));
        return makeObjectType(std::move(fields), false, false, {});
    }
    return std::nullopt;
}

}  // namespace cp3
