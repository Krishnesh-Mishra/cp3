// =============================================================================
// src/sema/src/detail/std_tables.hpp - PRIVATE shared std-member table helpers.
//
// The row model every std family check unit (`src/std/<family>/sema.cpp`)
// expresses its member surface in, plus the type factories more than one
// family uses. Extracted from `checks/std_check.cpp` (D72 stage 2) so a family
// unit is self-contained: its table drives member typing and the CP3305
// candidate list at once, and the dispatch core forwards by name.
//
// Everything here is implementation detail: this header lives under
// `src/sema/src/detail/` and is never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/types.hpp"

namespace cp3 {

// One implemented member of a std namespace: its name and a factory for its
// type. The same table drives member typing and the CP3305 candidate list, so
// the two can never drift apart.
struct MemberRow {
    const char* name;
    Type (*make)();
};

template <std::size_t N>
std::optional<Type> lookupMember(const MemberRow (&rows)[N], const std::string& prop) {
    for (const MemberRow& row : rows) {
        if (prop == row.name) return row.make();
    }
    return std::nullopt;
}

template <std::size_t N>
std::vector<std::string> memberNames(const MemberRow (&rows)[N]) {
    std::vector<std::string> names;
    names.reserve(N);
    for (const MemberRow& row : rows) names.push_back(row.name);
    return names;
}

// `(): number` - the one factory used by more than one family (Math's
// `random` and os's `cpus` / memory readers), hence shared here.
inline Type zeroToNumber() { return Type::function({}, Type::number()); }

}  // namespace cp3
