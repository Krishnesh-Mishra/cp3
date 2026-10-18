# =============================================================================
# cp3_codegen - checked AST -> generated C++20 (+ source map).
#
# OWNERSHIP: codegen owner. Keep the frozen `CodeGenerator` boundary declared in
# `include/cp3/codegen.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_codegen_sources` below (STYLE.md §8
# lists the target decomposition: code_generator.cpp, emit/*.cpp, emit/std/*.cpp,
# support/*.cpp; private headers live under src/detail/).
# =============================================================================

set(cp3_codegen_sources
    src/code_generator.cpp
    src/support/mangle.cpp
    src/support/cpp_writer.cpp
    src/support/type_map.cpp
    src/support/int_mode.cpp
    src/support/int_reads.cpp
    src/emit/expr.cpp
    src/emit/call.cpp
    src/emit/async.cpp
    src/emit/stmt.cpp
    src/emit/decl.cpp
    src/emit/std/common.cpp
    src/emit/std/math.cpp
    src/emit/std/os.cpp
    src/emit/std/path.cpp
    src/emit/std/process.cpp
    src/emit/std/json.cpp
    src/emit/std/string.cpp
)

add_library(cp3_codegen STATIC ${cp3_codegen_sources})
target_include_directories(cp3_codegen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from every unit subdirectory.
target_include_directories(cp3_codegen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cp3_codegen PUBLIC cp3_common cp3_sema)
cp3_set_warnings(cp3_codegen)
