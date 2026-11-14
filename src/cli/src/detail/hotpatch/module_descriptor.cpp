// =============================================================================
// src/cli/src/detail/hotpatch/module_descriptor.cpp - dev module descriptor
// emission (DEFINITIONS).
// =============================================================================
#include "detail/hotpatch/module_descriptor.hpp"

#include <cstdio>
#include <sstream>

namespace cp3::cli::hotpatch {

namespace {

// Escapes a string literal for emission. Module names and namespaces are
// filesystem/derived identifiers; the escape covers the pathological cases.
std::string escapeLiteral(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string braceHex(const std::string& hex) {
    return "0x" + hex + "u";
}

std::string hex64Hex(const std::string& hex) {
    return "0x" + hex + "ull";
}

}  // namespace

std::string emitModuleDescriptor(const std::string& moduleName,
                                 const std::string& namesp,
                                 const std::string& interfaceHashHex,
                                 const std::string& compilerKeyHex,
                                 const std::string& stateLayoutHashHex,
                                 bool exportSymbols,
                                 bool entryUnit) {
    const char* local = exportSymbols ? "" : "static ";
    std::ostringstream out;
    out << "\n"
           "// ---- dev descriptor (Incremental only) ----\n"
           "#include \"cp3/dev/hotpatch.hpp\"\n";
    out << "namespace {\n";
    out << "const char cp3_hotpatch_module_name[] = \""
        << escapeLiteral(moduleName) << "\";\n";
    out << "const char cp3_hotpatch_namespace[]   = \""
        << escapeLiteral(namesp) << "\";\n";
    out << "}  // namespace\n\n";
    out << "extern \"C\" {\n";
    out << local << "const std::uint64_t cp3_module_interface_hash_v1 = "
        << hex64Hex(interfaceHashHex) << ";\n";
    out << local << "const std::uint64_t cp3_module_abi_compiler_key_v1 = "
        << hex64Hex(compilerKeyHex) << ";\n";
    out << local << "const std::uint32_t cp3_module_abi_runtime_v1 = 1u;\n";
    out << local << "const std::uint64_t cp3_module_state_layout_hash_v1 = "
        << hex64Hex(stateLayoutHashHex) << ";\n";
    out << local << "const std::uint32_t cp3_module_state_size_v1 = 0u;\n";
    out << local << "const std::uint32_t cp3_module_state_align_v1 = 8u;\n";
    out << local << "const std::uint32_t cp3_module_state_field_count_v1 = 0u;\n";
    out << local << "const cp3::runtime::cp3_module_descriptor_v1"
           " cp3_module_descriptor_v1 = {\n"
           "    sizeof(cp3::runtime::cp3_module_descriptor_v1),\n"
           "    1u,   // struct_version\n"
           "    1u,   // abi_runtime (CP3_RUNTIME_ABI_VERSION)\n"
           "    0u,   // flags\n"
           "    "
        << hex64Hex(interfaceHashHex) << ",  // interface_hash\n"
           "    "
        << hex64Hex(compilerKeyHex) << ",  // compiler_key\n"
           "    "
        << hex64Hex(stateLayoutHashHex) << ",  // state_layout_hash\n"
           "    0u,  8u,  0u,  // state_size, state_align, field_count\n"
           "    nullptr,       // state_fields\n"
           "    0u,            // table_count\n"
           "    cp3_hotpatch_module_name,\n"
           "    cp3_hotpatch_namespace,\n"
           "};\n";
    out << local << "const cp3::runtime::cp3_module_descriptor_v1*\n"
           "cp3_get_module_descriptor(void) {\n"
           "    return &cp3_module_descriptor_v1;\n"
           "}\n";
    out << local << "bool cp3_module_fill_table_v1(void**, std::uint32_t) {\n"
           "    return false;\n"
           "}\n";
    out << local << "void cp3_module_state_init_v1(void*) {}\n";
    out << local << "void cp3_module_state_destroy_v1(void*) {}\n";
    if (exportSymbols) {
        // The module-library entry point the bootstrap calls to run this
        // module's top-level statements in compilation order. `namesp` is a
        // compiler-generated identifier, safe to splice unquoted.
        out << "void cp3_module_run_initializer_v1(void) {\n";
        if (!entryUnit) out << "    " << namesp << "::cp3_module_init();\n";
        out << "}\n";
    }
    out << "}  // extern \"C\"\n";
    return out.str();
}

}  // namespace cp3::cli::hotpatch
