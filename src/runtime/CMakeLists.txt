# =============================================================================
# cp3_runtime - runtime support for generated code (Undefined, print/println,
# cp3::Error, Promise, worker, parallel.*, console, event loop).
#
# The generated-code ABI declared in `include/cp3/**` is frozen (DECISIONS D14);
# the implementation lives in `src/**`. OWNERSHIP: runtime owner.
#
# NOTE: cp3_runtime is NOT yet linked into cp3c (see ARCHITECTURE.md §1); it
# becomes the support library a transpiled cp3 program links against.
#
# ADDING A SOURCE FILE: append one line to `cp3_runtime_sources` below. Policy:
# headers declare, `.cpp` defines (STYLE.md §3). Since the structure sweep the
# headers live under `include/cp3/{core,async,parallel,std}/` and the sources
# under `src/{core,async,parallel,std}/`.
# =============================================================================

find_package(Threads REQUIRED)

set(cp3_runtime_sources
    src/std/console.cpp
    src/core/error.cpp
    src/async/eventloop.cpp
    src/parallel/parallel.cpp
    src/async/promise.cpp
    src/core/runtime.cpp
    src/std/crypto.cpp
    src/std/network.cpp
    src/std/network_fetch.cpp
    src/std/network_server.cpp
    src/std/network_ws.cpp
    src/std/network_ws_client.cpp
    src/std/network_ws_frames.cpp
)

add_library(cp3_runtime STATIC ${cp3_runtime_sources})
target_include_directories(cp3_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Native backends (D68/D70): the crypto/network TUs include the vendored
# headers; the static archives themselves are linked by cp3c at program link
# time (src/cli/src/detail/native_packages.hpp), not into this library.
target_include_directories(cp3_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libsodium/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libuv/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/uWebSockets/include"
)
target_compile_definitions(cp3_runtime PRIVATE UWS_NO_ZLIB)
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
