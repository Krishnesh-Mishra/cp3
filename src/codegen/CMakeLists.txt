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
    src/support/type/mangle.cpp
    src/support/type/module_writer.cpp
    src/support/core/cpp_writer.cpp
    src/support/core/line_map.cpp
    src/support/type/type_map.cpp
    src/support/core/binding_types.cpp
    src/support/type/type_classify.cpp
    src/support/type/type_resolve.cpp
    src/support/core/generic.cpp
    src/support/type/scope_env.cpp
    src/support/type/value_shapes.cpp
    src/support/type/structural_walk.cpp
    src/support/core/captures.cpp
    src/support/class/class_types.cpp
    src/support/class/class_model.cpp
    src/support/class/class_collect.cpp
    src/support/core/enum_values.cpp
    src/support/class/class_walk.cpp
    src/support/core/helper_blocks.cpp
    src/support/int/int_mode.cpp
    src/support/int/int_mode_body.cpp
    src/support/int/int_reads.cpp
    src/support/core/array_refs.cpp
    src/support/int/top_int64.cpp
    src/support/int/top_int64_scan_expr.cpp
    src/support/int/top_int64_scan_stmt.cpp
    src/support/parallel/parallel_combine.cpp
    src/support/parallel/parallel_safety.cpp
    src/support/type/usage_includes.cpp
    src/support/type/program_link.cpp
    src/emit/decl/program.cpp
    src/emit/expr/expr.cpp
    src/emit/expr/expr_mixed.cpp
    src/emit/expr/typeof.cpp
    src/emit/decl/template.cpp
    src/emit/expr/equality.cpp
    src/emit/expr/objects.cpp
    src/emit/control/closures.cpp
    src/emit/pattern/destructure.cpp
    src/emit/pattern/destructure_assign.cpp
    src/emit/control/assignment.cpp
    src/emit/call/call.cpp
    src/emit/call/call_args.cpp
    src/emit/call/call_spread.cpp
    src/emit/call/native_call.cpp
    src/emit/async/async.cpp
    src/emit/control/arrow.cpp
    src/emit/call/spread.cpp
    src/emit/call/spread_call.cpp
    src/emit/expr/object_rest.cpp
    src/emit/async/async_promise.cpp
    src/emit/decl/stmt.cpp
    src/emit/misc/push_reserve.cpp
    src/emit/control/control.cpp
    src/emit/pattern/patterns.cpp
    src/emit/pattern/pattern_array.cpp
    src/emit/decl/decl.cpp
    src/emit/decl/module_var.cpp
    src/emit/class/class_decl.cpp
    src/emit/class/class_method.cpp

    src/emit/class/class_shape.cpp
    src/emit/class/class_expr.cpp
    src/emit/class/class_params.cpp
    src/emit/class/class_init.cpp
    src/emit/decl/enum.cpp
    src/emit/misc/regex.cpp
    src/emit/misc/parallel.cpp
    src/emit/worker/worker.cpp
    src/emit/worker/worker_handles.cpp
    src/emit/worker/worker_listener.cpp
    src/emit/worker/worker_module.cpp
    src/emit/worker/worker_scan.cpp
    src/emit/worker/worker_scan_stmt.cpp
    ../std/typed-arrays/codegen.cpp
    ../std/typed-arrays/codegen_constructors.cpp
    ../std/typed-arrays/codegen_methods.cpp
    ../std/arrays/codegen.cpp
    ../std/arrays/codegen_methods.cpp
    src/emit/std/common.cpp
    ../std/math/codegen.cpp
    ../std/os/codegen.cpp
    ../std/path/codegen.cpp
    ../std/process/codegen.cpp
    ../std/env/codegen.cpp
    ../std/json/codegen.cpp
    ../std/object/codegen.cpp
    ../std/string/codegen.cpp
    ../std/crypto/codegen.cpp
    ../std/network/codegen/codegen.cpp
    ../std/network/codegen/codegen_methods.cpp
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
