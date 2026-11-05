// =============================================================================
// src/support/std_integer.cpp - the std integer width table.
//
// One row per semantic kind. The canonical id is what `Type::native` stores;
// the rust/cpp spellings are only for documentation, so every vocabulary
// resolves to the same width and codegen.
// =============================================================================
#include "cp3/host/std_integer.hpp"

namespace cp3 {

namespace {

struct StdIntegerRow {
    StdIntegerKind kind;
    const char* id;
    const char* rust;
    const char* cpp;
};

constexpr StdIntegerRow kRows[] = {
    {StdIntegerKind::Status, "u16", "u16", "uint16_t"},
    {StdIntegerKind::Port, "u16", "u16", "uint16_t"},
    {StdIntegerKind::Pid, "i32", "i32", "int32_t"},
    {StdIntegerKind::ExitCode, "i32", "i32", "int32_t"},
    {StdIntegerKind::Timestamp, "i64", "i64", "int64_t"},
    {StdIntegerKind::FileSize, "u64", "u64", "uint64_t"},
    {StdIntegerKind::Length, "u64", "usize", "size_t"},
    {StdIntegerKind::Index, "u64", "usize", "size_t"},
    {StdIntegerKind::Count, "u64", "usize", "size_t"},
};

const StdIntegerRow& rowOf(StdIntegerKind kind) noexcept {
    for (const StdIntegerRow& row : kRows) {
        if (row.kind == kind) return row;
    }
    return kRows[0];
}

}  // namespace

const char* stdIntegerNativeId(StdIntegerKind kind) noexcept {
    return rowOf(kind).id;
}

Type stdIntegerType(StdIntegerKind kind, TypeSystem ts) {
    if (ts == TypeSystem::TypeScript) return Type::number();
    return Type::native(stdIntegerNativeId(kind));
}

const char* stdIntegerSpelling(StdIntegerKind kind, TypeSystem ts) noexcept {
    const StdIntegerRow& row = rowOf(kind);
    switch (ts) {
        case TypeSystem::TypeScript: return "number";
        case TypeSystem::Rust: return row.rust;
        case TypeSystem::Cpp: return row.cpp;
    }
    return "number";
}

}  // namespace cp3
