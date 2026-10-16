# =============================================================================
# cp3_runtime — runtime support for generated code (Undefined, print/println,
# cp3::Error, Promise, worker, parallel.*, console, event loop).
#
# The generated-code ABI declared in `include/cp3/**` is frozen (DECISIONS D14);
# the implementation lives in `src/**`. OWNERSHIP: runtime owner.
#
# NOTE: cp3_runtime is NOT yet linked into cp3c (see ARCHITECTURE.md §1); it
# becomes the support library a transpiled cp3 program links against.
#
# ADDING A SOURCE FILE: append one line to `cp3_runtime_sources` below. Policy:
# headers declare, `.cpp` defines (STYLE.md §3).
# =============================================================================

find_package(Threads REQUIRED)

set(cp3_runtime_sources
    src/console.cpp
    src/error.cpp
    src/eventloop.cpp
    src/parallel.cpp
    src/promise.cpp
    src/runtime.cpp
)

add_library(cp3_runtime STATIC ${cp3_runtime_sources})
target_include_directories(cp3_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_runtime PUBLIC cp3_common Threads::Threads)
cp3_set_warnings(cp3_runtime)

# -----------------------------------------------------------------------------
# Unit drivers (STYLE.md §7). Opt-in so the default library build stays lean:
#   cmake -B build-runtime -S . -DCP3_RUNTIME_TESTS=ON
# -----------------------------------------------------------------------------
option(CP3_RUNTIME_TESTS "Build the cp3_runtime unit drivers" OFF)
if(CP3_RUNTIME_TESTS)
    add_subdirectory(tests)
endif()
