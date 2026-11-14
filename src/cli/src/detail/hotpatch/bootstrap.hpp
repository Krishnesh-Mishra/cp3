// =============================================================================
// src/cli/src/detail/hotpatch/bootstrap.hpp - dev module-libs bootstrap
// emission (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header (dev profile only).
//
// Emits the source of the tiny host executable for the CP3_DEV_MODULE_LIBS
// build: a Windows-only process that loads cp3runtime_dev.dll, every module
// library in compilation order, verifies each module's descriptor ABI
// (struct shape, ABI version, compiler key), runs each module's
// initializer, and finally calls the entry module's `cp3_module_entry_v1`.
// The bootstrap itself never links a module DLL; all binding is dynamic
// through LoadLibrary/GetProcAddress, which is what makes a future module
// swap a load of a new library rather than a relink.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cp3::cli::hotpatch {

// The DLL file stem for one unit's module name: path segments are split on
// '/' and '\\', empties dropped, a leading "src" segment removed, and each
// remaining segment sanitized to [A-Za-z0-9_-] (everything else becomes
// '-'). "src/index" -> "index", "src/lib/base/b0" -> "lib-base-b0".
std::string moduleLibDllStem(const std::string& sourceKey);

// True when the CP3_DEV_MODULE_LIBS environment seam is set to a non-zero
// value (the dev module-libs build). The default build is unchanged.
bool devModuleLibsEnabled();

// Emits the bootstrap TU. `moduleDllNames` are plain file names (no path)
// of the module libraries in initializer-run order - the entry module's
// DLL may appear anywhere as long as `entryDllName` names it; it is passed
// its own top-level statements wrapped in `cp3_module_entry_v1`, never run
// through the initializer loop. `expectedCompilerKey` is fnv1a64 of the
// toolchain cache key: a module DLL built against a different toolchain is
// refused loudly instead of executing with a mismatched ABI.
std::string emitBootstrapSource(const std::vector<std::string>& moduleDllNames,
                                const std::string& entryDllName,
                                std::uint64_t expectedCompilerKey);

}  // namespace cp3::cli::hotpatch
