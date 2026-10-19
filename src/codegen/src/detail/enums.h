// =============================================================================
// src/codegen/src/detail/enums.h - PRIVATE C++ enum model (spec 04).
//
// `collectEnums` (emit/enum.cpp) resolves each top-level `enum` declaration into
// an `EnumCppInfo`: the C++ `enum class <name> : long long` with resolved
// backing values (explicit numbers, auto-increment, string-literal indices) and,
// for a string-backed enum, the per-member string the `toString` helper prints
// (D19). This header is implementation detail: it lives under
// `src/codegen/src/detail/` and is never included by another module
// (STYLE.md §3).
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3 {
namespace detail {

// One member of an enum declaration: the cp3 name, the C++ enumerator spelling
// and the resolved backing value. `value` is the member's ordinal for a
// string-backed member (members sharing one string share one value, TS
// identity) and the explicit/auto-incremented number otherwise.
struct EnumCppMember {
    std::string name;
    std::string text;  // C++ enumerator spelling (mapIdent(name))
    long long value = 0;
    bool isString = false;
    std::string stringValue;  // the decoded literal (string-backed members)
};

// The resolved model of one `enum` declaration, in declaration order. `cpp` is
// the C++ enum-class name (`mapIdent(name)`).
struct EnumCppInfo {
    std::string name;
    std::string cpp;
    bool stringBacked = false;
    std::vector<EnumCppMember> members;

    // The member with this name, or nullptr when the name is unknown.
    const EnumCppMember* member(const std::string& name) const {
        for (const EnumCppMember& m : members) {
            if (m.name == name) return &m;
        }
        return nullptr;
    }

    // True when two members share one backing value (a C++ switch cannot give
    // them two case labels).
    bool hasDuplicateValues() const {
        for (std::size_t i = 0; i < members.size(); ++i) {
            for (std::size_t j = i + 1; j < members.size(); ++j) {
                if (members[i].value == members[j].value) return true;
            }
        }
        return false;
    }
};

}  // namespace detail
}  // namespace cp3
