# =============================================================================
# cp3_runtime unit drivers (STYLE.md §7). Each driver is a standalone `main()`
# using the tiny assert harness in `harness.hpp`; it links only the runtime
# library. Enabled by -DCP3_RUNTIME_TESTS=ON from the module's CMakeLists.txt.
# =============================================================================

set(cp3_runtime_test_units
    format_test
    arith_test
    io_test
    parallel_test
    promise_all_test
    promise_test
    worker_test
)

foreach(unit IN LISTS cp3_runtime_test_units)
    add_executable(${unit} "${unit}.cpp")
    target_include_directories(${unit} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../std/console/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../std/io/include")
    target_link_libraries(${unit} PRIVATE cp3_runtime)
    cp3_set_warnings(${unit})
endforeach()

# io_test drives real socket descriptors through libuv's poller. The runtime
# archive leaves the vendored native archives to the program link step, so the
# driver links the platform libuv build directly.
if(TARGET io_test)
    if(WIN32)
        set(CP3_TEST_UV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/third_party/libuv/lib/windows-x64")
        set(CP3_TEST_UV_LIBS uv ws2_32 userenv iphlpapi ole32 dbghelp synchronization)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(CP3_TEST_UV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/third_party/libuv/lib/linux-arm64")
        set(CP3_TEST_UV_LIBS uv pthread dl rt)
    else()
        set(CP3_TEST_UV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/third_party/libuv/lib/linux-x64")
        set(CP3_TEST_UV_LIBS uv pthread dl rt)
    endif()
    target_link_directories(io_test PRIVATE "${CP3_TEST_UV_DIR}")
    target_link_libraries(io_test PRIVATE ${CP3_TEST_UV_LIBS})
endif()
