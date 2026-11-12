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
# headers live under `include/cp3/{core,async,parallel}/` and the sources under
# `src/{core,async,parallel}/`; since D72 the std families live under
# `src/std/<family>/` (ABI headers in `include/cp3/**`, TUs in `runtime*.cpp`).
# =============================================================================

find_package(Threads REQUIRED)

set(cp3_runtime_sources
    ../std/console/runtime.cpp
    ../std/console/runtime_pump.cpp
    ../std/console/runtime_pump_read.cpp
    src/core/error.cpp
    src/core/regex.cpp
    src/core/cycle.cpp
    src/async/eventloop.cpp
    src/async/uv_loop.cpp
    src/async/timers.cpp
    src/parallel/parallel.cpp
    src/async/promise.cpp
    src/core/runtime.cpp
    src/core/bucket_hash.cpp
    ../std/crypto/runtime.cpp
    ../std/process/child.cpp
    ../std/process/runtime.cpp
    ../std/io/runtime.cpp
    ../std/network/runtime/runtime.cpp
    ../std/network/runtime/runtime_fetch.cpp
    ../std/network/runtime/runtime_fetch_io.cpp
    ../std/network/runtime/runtime_dns.cpp
    ../std/network/server/runtime_server.cpp
    ../std/network/ws/runtime_ws.cpp
    ../std/network/ws/runtime_ws_client.cpp
    ../std/network/ws/runtime_ws_frames.cpp
)

add_library(cp3_runtime STATIC ${cp3_runtime_sources})
target_include_directories(cp3_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Native backends (D68/D70): the crypto/network TUs include the vendored
# headers; the static archives themselves are linked by cp3c at program link
# time (src/cli/src/detail/native_packages.hpp), not into this library.
target_include_directories(cp3_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/libsodium/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/unordered_dense/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/libuv/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/uWebSockets/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/pcre2/include"
    # Header-only: cp3/std/json_parse.hpp converts JSON numbers with
    # fast_float::from_chars (no archive to link). Needed here because the
    # network TUs include cp3/std/json.hpp.
    "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/third_party/fast_float/include"
)
# PCRE2 is consumed as a static 8-bit library: pcre2.h needs both defines
# before it is included (src/core/regex.cpp is the only TU that includes it).
target_compile_definitions(cp3_runtime PRIVATE
    PCRE2_STATIC=1
    PCRE2_CODE_UNIT_WIDTH=8
)
# Since D72 the std family ABI headers (cp3/std/*.hpp) and the typed-array
# headers live in each family's include/ tree under src/std/; the moved TUs
# include them directly, so the compiler needs those roots here.
target_include_directories(cp3_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/buffer/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/collections/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/console/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/crypto/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/json/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/fs/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/date/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/io/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/tls/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/body-parse/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/cookie/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/middleware/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/request/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/response/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/router/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/routing"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/settings/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/network/webserver/regex-routing"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/process/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../std/collections/include"
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
