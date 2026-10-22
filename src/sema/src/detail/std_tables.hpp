// =============================================================================
// src/sema/src/detail/std_tables.hpp - PRIVATE shared std-member table helpers.
//
// The row model every std family check unit (`src/std/<family>/sema.cpp`)
// expresses its member surface in, plus the type factories more than one
// family uses. Extracted from `checks/std_check.cpp` (D72 stage 2) so a family
// unit is self-contained: its table drives member typing and the CP3305
// candidate list at once, and the dispatch core forwards by name.
//
// The same table also carries the member's DOCUMENTATION (the documented
// signature and the Markdown description the editor shows), so a member
// cannot be typed and suggested without being documented: the three views of
// a std member live in one row and cannot drift apart.
//
// Everything here is implementation detail: this header lives under
// `src/sema/src/detail/` and is never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/std_surface.hpp"
#include "cp3/types.hpp"

namespace cp3 {

// One implemented member of a std namespace: its name, a factory for its
// type and its documentation. The same table drives member typing, the CP3305
// candidate list and the editor documentation, so the two can never drift
// apart.
struct MemberRow {
    const char* name;
    Type (*make)();
    const char* signature;
    const char* body;
};

// One documented member without a type factory: a receiver's documented
// fields (`FileStats.size`), whose type depends on the receiver rather than
// on the member name.
struct DocRow {
    const char* name;
    const char* signature;
    const char* body;
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

// The documentation of the member `prop`. Empty when the table has no entry
// for it - the caller decides what an undocumented member looks like.
template <std::size_t N>
StdMemberDoc memberDoc(const MemberRow (&rows)[N], const std::string& prop) {
    for (const MemberRow& row : rows) {
        if (prop != row.name) continue;
        StdMemberDoc doc;
        doc.signature = row.signature != nullptr ? row.signature : "";
        doc.body = row.body != nullptr ? row.body : "";
        return doc;
    }
    return StdMemberDoc{};
}

// The documentation of the member `prop` in a documentation-only table.
template <std::size_t N>
StdMemberDoc memberDoc(const DocRow (&rows)[N], const std::string& prop) {
    for (const DocRow& row : rows) {
        if (prop != row.name) continue;
        StdMemberDoc doc;
        doc.signature = row.signature != nullptr ? row.signature : "";
        doc.body = row.body != nullptr ? row.body : "";
        return doc;
    }
    return StdMemberDoc{};
}

// The member names of a documentation-only table.
template <std::size_t N>
std::vector<std::string> docRowNames(const DocRow (&rows)[N]) {
    std::vector<std::string> names;
    names.reserve(N);
    for (const DocRow& row : rows) names.push_back(row.name);
    return names;
}

// `(): number` - the one factory used by more than one family (Math's
// `random` and os's `cpus` / memory readers), hence shared here.
inline Type zeroToNumber() { return Type::function({}, Type::number()); }

}  // namespace cp3
