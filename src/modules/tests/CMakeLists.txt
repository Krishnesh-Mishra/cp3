# =============================================================================
# cp3_modules unit drivers (opt-in: -DCP3_MODULES_TESTS=ON).
#
# Lightweight, no third-party framework (STYLE.md §7): each driver is a plain
# executable returning 0 on success. Drivers may include the module's private
# headers (`../src`) - they are part of the module, not another module.
# =============================================================================

add_executable(cp3_modules_prelude_scan_test prelude_scan_test.cpp)
target_include_directories(cp3_modules_prelude_scan_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(cp3_modules_prelude_scan_test PRIVATE cp3_modules)
cp3_set_warnings(cp3_modules_prelude_scan_test)
