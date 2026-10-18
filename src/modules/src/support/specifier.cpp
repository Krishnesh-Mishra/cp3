// =============================================================================
// src/modules/src/support/specifier.cpp - module specifier classification.
//
// OWNERSHIP: modules module (src/modules/**). Implements detail/specifier.h.
// =============================================================================
#include "detail/specifier.h"

#include <string>

#include "detail/file_paths.h"

namespace cp3::modules {

ModuleResolution classifyModuleSpecifier(const std::string& specifier) noexcept {
    if (startsWith(specifier, "./") || startsWith(specifier, "../") ||
        endsWith(specifier, ".cp3")) {
        return ModuleResolution::Local;
    }
    if (startsWith(specifier, "std/")) return ModuleResolution::Std;
    if (startsWith(specifier, "cpp/")) return ModuleResolution::Cpp;
    return ModuleResolution::Package;
}

}  // namespace cp3::modules
