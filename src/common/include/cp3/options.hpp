// =============================================================================
// cp3/options.hpp - compilation options shared by every stage.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md.
// Mirrors package.json fields documented in docs/language-spec/03-type-system.md
// and docs/cli-tooling/01-compilation-pipeline.md.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/native_bindings.hpp"

namespace cp3 {

// The project's active type-name vocabulary ("typeSystem" in package.json).
enum class TypeSystem { TypeScript, Rust, Cpp };

const char* typeSystemName(TypeSystem ts) noexcept;
// Parses "typescript" | "rust" | "cpp"; returns false if unrecognized.
bool parseTypeSystem(const std::string& text, TypeSystem& out) noexcept;

// cp3 build profiles (docs/cli-tooling/01-compilation-pipeline.md).
enum class BuildProfile {
    Incremental,  // default: reuse cached objects, -O2 per TU
    Release,      // --release: whole-program, LTO
    Debug,        // --debug: -O0 + debug info, incremental
};

struct CompileOptions {
    TypeSystem typeSystem = TypeSystem::TypeScript;
    bool strictNumericCasts = false;  // opt-in (default: C++-style implicit widening)

    BuildProfile profile = BuildProfile::Incremental;

    // When false, stop after sema ("cp3 check"): no C++ generation, no toolchain.
    bool emitCpp = true;

    // Codegen phase RAM cap in MB; 0 means "default" (70% of system RAM).
    std::uint64_t maxMemoryMb = 0;

    // Entry point (.cp3) for run/build; empty for a bare check of a project.
    std::string entryPath;

    // The native bindings the program imports (`import native "id" as alias`).
    // The module resolver loads them (one model per binding id) and hands
    // them to sema and codegen through this field: sema type-checks the
    // calls against them, codegen lowers them.
    std::vector<NativeBinding> nativeBindings;
};

}  // namespace cp3
