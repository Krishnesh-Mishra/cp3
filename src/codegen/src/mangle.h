// =============================================================================
// src/codegen/src/mangle.h — PRIVATE codegen name mangling.
//
// Turns cp3 module/identifier names into legal C++ spellings: the per-unit
// namespace (`suggestNamespace`), the include-guard fragment
// (`sanitizeIdent`/`upperSnake`), and the collision-avoiding identifier
// (`mapIdent` -> `<name>_cp3` for C++ keywords / generated scaffolding names).
// Shared by the codegen translation units only (STYLE.md §3/§8).
// =============================================================================
#pragma once

#include <string>

namespace cp3 {
namespace detail {

// Lower-cases alphanumerics, replaces every other byte with '_'; an empty name
// becomes "module" and a leading digit is prefixed with '_'.
std::string sanitizeIdent(const std::string& name);

// ASCII upper-cased copy of `s`, used for include-guard fragments.
std::string upperSnake(const std::string& s);

// True for a C++20 keyword or a name that would collide with generated
// scaffolding (`main`, `std`, `cp3`).
bool isCppKeyword(const std::string& name);

// The C++ spelling of a cp3 identifier: unchanged unless it needs escaping.
std::string mapIdent(const std::string& name);

// The C++ namespace for `moduleName` (`cp3_mod_<sanitized>`).
std::string suggestNamespace(const std::string& moduleName);

}  // namespace detail
}  // namespace cp3
