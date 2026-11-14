// =============================================================================
// src/runtime/include/cp3/dev/hotpatch.hpp - dev hot-patch ABI (DECLARATIONS).
//
// OWNERSHIP: runtime module (src/runtime/**). PRIVATE-ish header (dev path
// only; release never includes it).
//
// New header, additive only (DECISIONS D14: the frozen runtime surface is not
// reshaped). Defines the ABI between a module lib and the host process:
// the uniform `cp3_get_module_descriptor` discovery point, the function
// table fill call the host uses to grab exported entry points, and the
// state block lifecycle (init/destroy) the host uses to keep module state
// alive across code swaps.
//
// The ABI never depends on C++ names, vtable layout or mangled symbols; the
// layout of the descriptor is fixed-width and carries its own struct_bytes
// for forward compatibility. The host never reads anything the descriptor
// does not declare.
// =============================================================================
#pragma once

#include <cstdint>

namespace cp3::runtime {

// Bumped whenever the ABI changes in a way an already-loaded module lib
// cannot interpret. A mismatch refuses the swap (Phase 4 Slice 4).
inline constexpr std::uint32_t CP3_RUNTIME_ABI_VERSION = 1u;

// Layout of one mutable module-scope binding as declared by the descriptor.
// `name` and `cpp_type` point at static storage inside the lib, valid until
// process exit (module libs are never unloaded before exit in v1).
struct cp3_state_field_v1 {
    const char* name;
    const char* cpp_type;
    std::uint32_t offset;  // bytes from block base
    std::uint32_t size;
    std::uint32_t align;
};

// The descriptor every module lib exports via `cp3_get_module_descriptor`.
// struct_bytes is sizeof(cp3_module_descriptor_v1) so older hosts refuse
// unknown shapes instead of reading past the struct.
struct cp3_module_descriptor_v1 {
    std::uint32_t struct_bytes;
    std::uint32_t struct_version;  // == 1
    std::uint32_t abi_runtime;     // == CP3_RUNTIME_ABI_VERSION
    std::uint32_t flags;           // bit0: has top-level await (refuse swap)
    std::uint64_t interface_hash;  // fnv1a64 of the unit's canonical export surface
    std::uint64_t compiler_key;   // fnv1a64 of the compiler key string
    std::uint64_t state_layout_hash;  // fnv1a64 of the canonical field lines
    std::uint32_t state_size;
    std::uint32_t state_align;
    std::uint32_t state_field_count;
    const cp3_state_field_v1* state_fields;
    std::uint32_t table_count;    // callable exports, in canonical name order
    const char* module_name;     // logical module name ("src/lib/base/b0")
    const char* namesp;          // C++ namespace ("cp3_mod_src_lib_base_b0")
};

// Every module lib exports this one uniform name (extern "C", no mangling).
extern "C" const cp3_module_descriptor_v1* cp3_get_module_descriptor(void);
extern "C" bool cp3_module_fill_table_v1(void** out_entries,
                                         std::uint32_t entry_count);
extern "C" void cp3_module_state_init_v1(void* block);
extern "C" void cp3_module_state_destroy_v1(void* block);

}  // namespace cp3::runtime
