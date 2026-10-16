# =============================================================================
# cp3_runtime unit drivers (STYLE.md §7). Each driver is a standalone `main()`
# using the tiny assert harness in `harness.hpp`; it links only the runtime
# library. Enabled by -DCP3_RUNTIME_TESTS=ON from the module's CMakeLists.txt.
# =============================================================================

set(cp3_runtime_test_units
    format_test
    parallel_test
    promise_all_test
    promise_test
    worker_test
)

foreach(unit IN LISTS cp3_runtime_test_units)
    add_executable(${unit} "${unit}.cpp")
    target_include_directories(${unit} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${unit} PRIVATE cp3_runtime)
    cp3_set_warnings(${unit})
endforeach()
