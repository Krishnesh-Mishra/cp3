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
    src/support/module_writer.cpp
    src/support/cpp_writer.cpp
    src/support/line_map.cpp
    src/support/type_map.cpp
    src/support/binding_types.cpp
    src/support/type_classify.cpp
    src/support/type_resolve.cpp
    src/support/generic.cpp
    src/support/scope_env.cpp
    src/support/value_shapes.cpp
    src/support/structural_walk.cpp
    src/support/captures.cpp
    src/support/class_types.cpp
    src/support/class_model.cpp
    src/support/class_collect.cpp
    src/support/class_walk.cpp
    src/support/helper_blocks.cpp
    src/support/int_mode.cpp
    src/support/int_mode_body.cpp
    src/support/int_reads.cpp
    src/support/array_refs.cpp
    src/support/top_int64.cpp
    src/support/top_int64_scan_expr.cpp
    src/support/top_int64_scan_stmt.cpp
    src/support/parallel_combine.cpp
    src/support/parallel_safety.cpp
    src/support/usage_includes.cpp
    src/support/program_link.cpp
    src/emit/program.cpp
    src/emit/expr.cpp
    src/emit/typeof.cpp
    src/emit/template.cpp
    src/emit/equality.cpp
    src/emit/objects.cpp
    src/emit/closures.cpp
    src/emit/destructure.cpp
    src/emit/assignment.cpp
    src/emit/call.cpp
    src/emit/call_args.cpp
    src/emit/async.cpp
    src/emit/arrow.cpp
    src/emit/spread.cpp
    src/emit/stmt.cpp
    src/emit/push_reserve.cpp
    src/emit/control.cpp
    src/emit/patterns.cpp
    src/emit/decl.cpp
    src/emit/module_var.cpp
    src/emit/class_decl.cpp
    src/emit/class_shape.cpp
    src/emit/class_expr.cpp
    src/emit/enum.cpp
    src/emit/regex.cpp
    src/emit/parallel.cpp
    src/emit/worker.cpp
    src/emit/worker_handles.cpp
    src/emit/worker_scan.cpp
    src/emit/worker_scan_stmt.cpp
    ../std/typed-arrays/codegen.cpp
    ../std/typed-arrays/codegen_constructors.cpp
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
    ../std/collections/codegen_map.cpp
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
