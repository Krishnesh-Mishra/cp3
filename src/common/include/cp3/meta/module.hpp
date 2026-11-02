// =============================================================================
// cp3/module.hpp - compilation units and the module graph.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md.
//
// A CompilationUnit is one parsed `.cp3` file. The ModuleGraph owns the units,
// the import edges between them, and the prelude injection bookkeeping
// (docs/language-spec/18-modules-imports.md).
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/host/native_bindings.hpp"
#include "cp3/core/source.hpp"

namespace cp3 {

// How a module specifier resolves (docs/language-spec/18/19, CLI §4).
enum class ModuleResolution {
    Local,     // "./x.cp3", "worker.cp3"
    Std,       // "std/console"
    Package,   // "mathkit"
    Native,    // import native "opencv"
    Cpp,       // import "cpp/iostream"
    Unknown,
};

const char* moduleResolutionName(ModuleResolution r) noexcept;

// A prelude file variant (docs/language-spec/18-modules-imports.md).
enum class PreludeKind {
    None,
    Same,    // _prelude.same.cp3  - same folder only
    Strong,  // _prelude.cp3       - recursive, unstoppable
    Weak,    // _prelude.weak.cp3  - recursive, stopped by any nested prelude
};

const char* preludeKindName(PreludeKind k) noexcept;

// One parsed `.cp3` file plus the metadata the pipeline needs about it.
struct CompilationUnit {
    std::uint32_t id = 0;
    const SourceFile* file = nullptr;     // never null once loaded
    std::string moduleName;               // logical name (specifier or project-relative path)

    ProgramPtr ast;                        // null until the parser finishes
    bool parsed = false;
    bool checked = false;

    bool isEntry = false;
    bool isPrelude = false;
    PreludeKind preludeKind = PreludeKind::None;

    // A unit loaded through `import ... from "<package>"`. The package's
    // generated `index.cp3` is a declaration surface: its C++ implementation
    // (the `pak/` sources shipped in the archive) is compiled and linked by the
    // CLI instead of being re-emitted as empty stub bodies. `packageRoot` names
    // the installed package directory so the build can find those sources.
    bool isPackage = false;
    std::filesystem::path packageRoot;

    // Prelude files whose items were injected at the top of this unit, in
    // application order (same -> strong -> weak), as documented in §18.
    std::vector<std::uint32_t> injectedPreludes;

    CompilationUnit() = default;
    explicit CompilationUnit(const SourceFile* f) : file(f) {}
};

// A directed import edge.
struct ModuleDependency {
    std::uint32_t importer = 0;
    std::uint32_t imported = 0;   // UINT32_MAX if unresolved
    std::string specifier;        // as written in source
    ModuleResolution resolution = ModuleResolution::Unknown;
    SourceSpan span{};            // location of the import
    bool resolved = false;
};

// Owns every compilation unit in the program.
class ModuleGraph {
public:
    ModuleGraph() = default;

    CompilationUnit* addUnit(const SourceFile* file);
    CompilationUnit* unit(std::uint32_t id) const;

    void addDependency(ModuleDependency dep);
    const std::vector<ModuleDependency>& dependencies() const noexcept { return deps_; }

    // The native bindings the program imports (`import native "id" as alias`).
    // The module resolver loads each one (native.json + its index.cp3) and
    // parks it here; sema and codegen read the model from this list.
    void addNativeBinding(NativeBinding binding);
    const std::vector<NativeBinding>& nativeBindings() const noexcept {
        return nativeBindings_;
    }
    // The binding with this id, or nullptr.
    const NativeBinding* findNativeBinding(const std::string& id) const;

    void setEntry(CompilationUnit* unit) noexcept { entry_ = unit; }
    CompilationUnit* entry() const noexcept { return entry_; }

    std::vector<CompilationUnit*>& units() noexcept { return unitsView_; }
    const std::vector<CompilationUnit*>& units() const noexcept { return unitsView_; }

    // Depth-first post-order over the import graph starting at the entry unit;
    // each unit appears after the units it depends on. Cycles are tolerated
    // (ES-module semantics, §23) - a unit is emitted at most once.
    std::vector<CompilationUnit*> compilationOrder() const;

private:
    std::vector<std::unique_ptr<CompilationUnit>> storage_;
    std::vector<CompilationUnit*> unitsView_;
    std::vector<ModuleDependency> deps_;
    std::vector<NativeBinding> nativeBindings_;
    CompilationUnit* entry_ = nullptr;
};

}  // namespace cp3
