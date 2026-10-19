// =============================================================================
// cp3/std_modules.hpp - the known `std/...` module specifiers.
//
// OWNERSHIP: modules module (src/modules/**). Declaration-only public header
// for callers outside the loader (the LSP completion of `import ... from "|"`).
// The table itself stays where it lives: src/modules/src/support/std_registry.cpp.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3 {

// Every specifier `stdModuleSurface` resolves, in table order (docs 04).
// Removed specifiers (D74: `std/events`) are deliberately absent: they do not
// resolve, so an editor must not offer them.
std::vector<std::string> stdModuleSpecifiers();

}  // namespace cp3
