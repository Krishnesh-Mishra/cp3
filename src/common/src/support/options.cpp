// =============================================================================
// cp3/options.cpp - the project option vocabulary (cp3/options.hpp).
//
// Only the type-system names live here: package.json stores the lower-case
// spelling ("typescript", "rust", "cpp") and every stage compares the enum.
// =============================================================================
#include "cp3/host/options.hpp"

#include <string>

namespace cp3 {

const char* typeSystemName(TypeSystem ts) noexcept {
    switch (ts) {
        case TypeSystem::TypeScript: return "typescript";
        case TypeSystem::Rust: return "rust";
        case TypeSystem::Cpp: return "cpp";
    }
    return "typescript";
}

bool parseTypeSystem(const std::string& text, TypeSystem& out) noexcept {
    if (text == "typescript") {
        out = TypeSystem::TypeScript;
        return true;
    }
    if (text == "rust") {
        out = TypeSystem::Rust;
        return true;
    }
    if (text == "cpp") {
        out = TypeSystem::Cpp;
        return true;
    }
    return false;
}

}  // namespace cp3
