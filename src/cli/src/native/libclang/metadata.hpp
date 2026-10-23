// =============================================================================
// src/cli/src/native/metadata.hpp - `native.json` rendering.
//
// OWNERSHIP: cli module (native-install). PRIVATE header.
//
// `native.json` is the compiler-facing half of a binding. The generated
// `index.cp3` is documentation; this command writes both. Reading and
// validating bindings for sema/codegen is a separate follow-up (spec 19).
// =============================================================================
#pragma once

#include <string>

#include "cp3/host/native_bindings.hpp"

namespace cp3::cli::native {

// The metadata schema written by this compiler (the shared model owns it).
inline constexpr const char* kNativeMetadataSchema = cp3::kNativeMetadataSchema;

// Renders `native.json` for `module` with the hash of `indexText`.
std::string renderMetadata(const NativeModule& module,
                           const std::string& indexText);

}  // namespace cp3::cli::native
