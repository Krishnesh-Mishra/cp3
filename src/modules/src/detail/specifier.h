// =============================================================================
// src/modules/src/detail/specifier.h - module specifier classification (PRIVATE header).
//
// OWNERSHIP: modules module (src/modules/**). Never included cross-module.
//
// Definitions live in src/support/specifier.cpp. Rules (DECISIONS D4 + D27):
//   starts with "./" / "../" or ends with ".cp3" -> Local
//   starts with "std/"  -> Std
//   starts with "cpp/"  -> Cpp
//   anything else bare  -> Package
// `import native "..."` is classified by the import declaration's kind, not by
// this function.
// =============================================================================
#pragma once

#include <string>

#include "cp3/module.hpp"

namespace cp3::modules {

ModuleResolution classifyModuleSpecifier(const std::string& specifier) noexcept;

}  // namespace cp3::modules
