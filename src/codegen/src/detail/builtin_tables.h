// =============================================================================
// src/codegen/src/detail/builtin_tables.h - PRIVATE shared dispatch helpers for
// the `emit/std/` family lowering units.
//
// The stable surface every `emit/std/<family>.cpp` shares: the namespace-shape
// predicates (`isNamespaceObject`, `isNsMember`) and the table membership
// helper (`inTable` / `CP3_IN_TABLE`). Keeping them here lets a new std family
// (network, crypto) land as one new `emit/std/<family>.cpp` plus one dispatch
// arm in `emit/std/common.cpp`, without touching this header.
//
// The per-family `needs*` flags (`needsMath_`, `needsString_`, ...) stay on
// `Generator` (detail/generator.h): each family sets its own and
// `code_generator.cpp`'s buildSource consumes them for the generated include
// assembly.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>

#include "cp3/ast.hpp"

namespace cp3 {
namespace detail {

// Is `e` the member expression `ns.<prop>` on a bare namespace identifier?
inline bool isNsMember(const Expr* e, const char* ns, const char* prop) {
    if (!e || e->kind != NodeKind::MemberExpr) return false;
    const auto& m = static_cast<const MemberExpr&>(*e);
    if (m.property != prop) return false;
    return m.object && m.object->kind == NodeKind::Identifier &&
           static_cast<const Identifier&>(*m.object).name == ns;
}

// Is `e` the bare namespace identifier `ns`?
inline bool isNamespaceObject(const Expr* e, const char* ns) {
    return e && e->kind == NodeKind::Identifier &&
           static_cast<const Identifier&>(*e).name == ns;
}

// Linear membership test of `name` in a static string table.
inline bool inTable(const std::string& name, const char* const* table,
                    std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (name == table[i]) return true;
    }
    return false;
}

}  // namespace detail
}  // namespace cp3

#define CP3_IN_TABLE(name, table) \
    inTable((name), (table), sizeof(table) / sizeof((table)[0]))
