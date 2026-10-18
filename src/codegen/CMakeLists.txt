# =============================================================================
# cp3_codegen — checked AST -> generated C++20 (+ source map).
#
# OWNERSHIP: codegen owner. Keep the frozen `CodeGenerator` boundary declared in
# `include/cp3/codegen.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_codegen_sources` below (STYLE.md §8
# lists the target decomposition: code_generator.cpp, emit_expr.cpp,
# emit_call.cpp, emit_builtin.cpp, emit_async.cpp, emit_stmt.cpp, emit_decl.cpp,
# cpp_writer.cpp, mangle.cpp).
# =============================================================================

set(cp3_codegen_sources
    src/code_generator.cpp
    src/mangle.cpp
    src/cpp_writer.cpp
    src/type_map.cpp
    src/int_mode.cpp
    src/int_reads.cpp
    src/emit_expr.cpp
    src/emit_call.cpp
    src/emit_builtin.cpp
    src/emit_async.cpp
    src/emit_stmt.cpp
    src/emit_decl.cpp
)

add_library(cp3_codegen STATIC ${cp3_codegen_sources})
target_include_directories(cp3_codegen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_codegen PUBLIC cp3_common cp3_sema)
cp3_set_warnings(cp3_codegen)
