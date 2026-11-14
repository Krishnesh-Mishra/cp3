// =============================================================================
// src/cli/src/detail/hotpatch/module_descriptor.hpp - dev module descriptor
// emission (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// The hot patch's module descriptor (hotpatch.hpp ABI). v1 documents the
// module's identity: interface/compiler/state-layout fingerprints and its
// logical name. The callable table and the state block materialize in a
// later Slice; v1's `cp3_module_fill_table_v1` returns false and the state
// block is empty. The descriptor is only appended when the caller opts in
// (the dev profile plus the CP3_DEV_MODULE_LIBS environment seam), so the
// default build is byte-identical to before.
// =============================================================================
#pragma once

#include <string>

namespace cp3::cli::hotpatch {

// Appends the dev descriptor to the end of one unit TU. `moduleName` is the
// logical module name ("src/lib/base/b0"); `namesp` its C++ namespace.
// The hashes are 16-hex-character strings without a `0x` prefix (as produced
// by incrementalHex64). Emits valid C++ referencing cp3/dev/hotpatch.hpp.
// `exportSymbols` selects the module-library ABI: TU-local statics (the
// default, for the single-binary dev build) or extern "C" exports plus a
// `cp3_module_run_initializer_v1` entry point (for CP3_DEV_MODULE_LIBS).
// `entryUnit` makes the run-initializer a no-op: the entry module carries
// the statements, not a module initializer.
std::string emitModuleDescriptor(const std::string& moduleName,
                                 const std::string& namesp,
                                 const std::string& interfaceHashHex,
                                 const std::string& compilerKeyHex,
                                 const std::string& stateLayoutHashHex,
                                 bool exportSymbols = false,
                                 bool entryUnit = false);

}  // namespace cp3::cli::hotpatch
