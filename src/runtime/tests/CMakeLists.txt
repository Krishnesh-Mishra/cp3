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
    cycle_test
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

# parallel_test drives parallel.for/reduce/run through the oneTBB scheduler
# (src/parallel/parallel.cpp). The runtime archive leaves the vendored native
# archives to the program link step, so the driver links the vendored libtbb
# the same way. Windows links the original root copy; Linux uses per-arch
# directories - the linux-x64 archive is staged by the maintainer
# (vendor/third_party/oneTBB/lib/linux-x64).
if(TARGET parallel_test)
    if(WIN32)
        set(CP3_TEST_TBB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/third_party/oneTBB/lib")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(CP3_TEST_TBB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/third_party/oneTBB/lib/linux-arm64")
    else()
        set(CP3_TEST_TBB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/third_party/oneTBB/lib/linux-x64")
    endif()
    target_link_directories(parallel_test PRIVATE "${CP3_TEST_TBB_DIR}")
    target_link_libraries(parallel_test PRIVATE tbb)
endif()
