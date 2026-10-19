// =============================================================================
// src/sema/src/detail/enums.h - PRIVATE enum model shared by the sema checks.
//
// `registerEnum` (checks/enum_check.cpp) resolves one `enum` declaration into
// an `EnumInfo`; `memberOfProbe` (checks/member_check.cpp) resolves member
// accesses (`Color.Green`) against the table. Everything here is
// implementation detail: this header lives under `src/sema/src/detail/` and is
// never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3 {

// One member of an enum declaration: the name and whether its value is
// string-backed (a string-literal initializer). Members without an initializer
// inherit the declaration's backing kind.
struct EnumMemberInfo {
    std::string name;
    bool isString = false;
};

// The resolved model of one `enum` declaration (docs/language-spec/04-enums.md):
// the type name, the backing kind and the members in declaration order.
// `stringBacked` is true when the declaration has at least one string-literal
// initializer (matching the parser's `EnumDeclaration::stringBacked` flag).
struct EnumInfo {
    std::string name;
    bool stringBacked = false;
    std::vector<EnumMemberInfo> members;

    // The member with this name, or nullptr when the name is unknown.
    const EnumMemberInfo* member(const std::string& name) const {
        for (const EnumMemberInfo& m : members) {
            if (m.name == name) return &m;
        }
        return nullptr;
    }
};

}  // namespace cp3
