// =============================================================================
// src/common/src/detail/native_fields.hpp - the `native.json` field readers.
//
// OWNERSHIP: common module. Never included cross-module.
//
// The metadata document is read field by field: a type, a parameter, a
// function, a class, an enum. This private header owns those readers; the
// public reader (src/common/src/native_bindings_read.cpp) owns the document
// and the staleness check.
//
// Definitions live in src/common/src/detail/native_fields.cpp (STYLE.md §3:
// headers declare, `.cpp` defines).
// =============================================================================
#pragma once

#include <string>

#include "cp3/native_bindings.hpp"
#include "detail/native_json.hpp"

namespace cp3::detail {

// A field the reader could not make sense of.
struct FieldError {
    std::string message;
};

// The cp3 type a metadata type object spells.
bool readNativeType(const JsonValue& node, const std::string& id, Type& out,
                     FieldError& err);

// One parameter of a bound function.
bool readNativeParam(const JsonValue& node, const std::string& id,
                     NativeParam& out, FieldError& err);

// One bound function.
bool readNativeFunction(const JsonValue& node, const std::string& id,
                        NativeFunction& out, FieldError& err);

// One bound class.
bool readNativeClass(const JsonValue& node, const std::string& id,
                     NativeBoundClass& out, FieldError& err);

// One bound enum.
bool readNativeEnum(const JsonValue& node, NativeEnum& out, FieldError& err);

// The whole document: the lists, the functions, the classes and the enums.
bool readNativeModule(const JsonValue& root, NativeModule& out, FieldError& err);

}  // namespace cp3::detail
