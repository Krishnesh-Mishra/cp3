# =============================================================================
# cp3_codegen - checked AST -> generated C++20 (+ source map).
#
# OWNERSHIP: codegen owner. Keep the frozen `CodeGenerator` boundary declared in
# `include/cp3/codegen.hpp`.
#
# ADDING A SOURCE FILE: append one line to `cp3_codegen_sources` below (STYLE.md §8
# lists the target decomposition: code_generator.cpp, emit/*.cpp, support/*.cpp;
# private headers live under src/detail/). Since D72 the std family lowering
# units live under ../std/<family>/ (codegen.cpp / codegen_methods.cpp) and
# emit/std/ keeps only the shared dispatch (common.cpp).
# =============================================================================

set(cp3_codegen_sources
    src/code_generator.cpp
    src/support/mangle.cpp
    src/support/cpp_writer.cpp
    src/support/line_map.cpp
    src/support/type_map.cpp
    src/support/scope_env.cpp
    src/support/value_shapes.cpp
    src/support/helper_blocks.cpp
    src/support/int_mode.cpp
    src/support/int_reads.cpp
    src/emit/expr.cpp
    src/emit/equality.cpp
    src/emit/objects.cpp
    src/emit/assignment.cpp
    src/emit/call.cpp
    src/emit/async.cpp
    src/emit/stmt.cpp
    src/emit/decl.cpp
    src/emit/enum.cpp
    src/emit/regex.cpp
    src/emit/parallel.cpp
    src/emit/worker.cpp
    src/emit/worker_scan.cpp
    ../std/typed-arrays/codegen.cpp
    ../std/typed-arrays/codegen_methods.cpp
    ../std/arrays/codegen_methods.cpp
    src/emit/std/common.cpp
    ../std/math/codegen.cpp
    ../std/os/codegen.cpp
    ../std/path/codegen.cpp
    ../std/process/codegen.cpp
    ../std/env/codegen.cpp
    ../std/json/codegen.cpp
    ../std/string/codegen.cpp
    ../std/crypto/codegen.cpp
    ../std/network/codegen.cpp
    ../std/network/codegen_methods.cpp
    ../std/collections/codegen.cpp
    ../std/date/codegen.cpp
    ../std/buffer/codegen.cpp
    ../std/console/codegen.cpp
    ../std/fs/codegen.cpp
)

add_library(cp3_codegen STATIC ${cp3_codegen_sources})
target_include_directories(cp3_codegen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from every unit subdirectory.
target_include_directories(cp3_codegen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cp3_codegen PUBLIC cp3_common cp3_sema)
cp3_set_warnings(cp3_codegen)
