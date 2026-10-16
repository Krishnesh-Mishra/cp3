# =============================================================================
# cp3c — the C+++ (.cp3) reference compiler
#
# cp3 transpiles .cp3 source to C++20 (one .cpp/.hpp pair per .cp3 file) and
# drives the system C++ toolchain. This CMake project builds *the compiler
# itself*, which is written in C++ (see docs/cli-tooling/06-open-tooling-questions.md:
# "compiler implementation language decided: C++").
#
# NOTE: cp3 projects themselves never use CMake (docs/cli-tooling/01-compilation-pipeline.md),
# and neither does the `cp3` frontend at runtime. This file exists only to build
# the `cp3c` binary from this repository's own src/ tree.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(cp3c
    VERSION 0.1.0
    DESCRIPTION "C+++ (.cp3) reference compiler (transpiles to C++20)"
    LANGUAGES CXX)

# --- C++20 floor, non-negotiable (coroutines) -------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# A shared warnings helper; never -Werror (parallel module authors shouldn't
# be blocked by unrelated warnings).
function(cp3_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# Absolute source paths
set(CP3_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# -----------------------------------------------------------------------------
# cp3_common — FROZEN shared interfaces (source/diagnostics/tokens/AST/module).
# Every other module links against this. OWNERSHIP: scaffolding/architecture.
# -----------------------------------------------------------------------------
add_library(cp3_common STATIC
    "${CP3_SRC_DIR}/common/src/source.cpp"
    "${CP3_SRC_DIR}/common/src/diagnostic.cpp"
    "${CP3_SRC_DIR}/common/src/token.cpp"
    "${CP3_SRC_DIR}/common/src/ast.cpp"
    "${CP3_SRC_DIR}/common/src/module.cpp"
    "${CP3_SRC_DIR}/common/src/version.cpp"
)
target_include_directories(cp3_common PUBLIC "${CP3_SRC_DIR}/common/include")
cp3_set_warnings(cp3_common)

# -----------------------------------------------------------------------------
# cp3_lexer — .cp3 source text -> Token stream.  STUB.
# -----------------------------------------------------------------------------
add_library(cp3_lexer STATIC "${CP3_SRC_DIR}/lexer/lexer.cpp")
target_include_directories(cp3_lexer PUBLIC "${CP3_SRC_DIR}/lexer/include")
target_link_libraries(cp3_lexer PUBLIC cp3_common)
cp3_set_warnings(cp3_lexer)

# -----------------------------------------------------------------------------
# cp3_parser — Token stream -> AST.  STUB.
# -----------------------------------------------------------------------------
add_library(cp3_parser STATIC "${CP3_SRC_DIR}/parser/parser.cpp")
target_include_directories(cp3_parser PUBLIC "${CP3_SRC_DIR}/parser/include")
target_link_libraries(cp3_parser PUBLIC cp3_common cp3_lexer)
cp3_set_warnings(cp3_parser)

# -----------------------------------------------------------------------------
# cp3_sema — AST -> diagnostics / resolved semantics.  STUB.
# -----------------------------------------------------------------------------
add_library(cp3_sema STATIC "${CP3_SRC_DIR}/sema/sema.cpp")
target_include_directories(cp3_sema PUBLIC "${CP3_SRC_DIR}/sema/include")
target_link_libraries(cp3_sema PUBLIC cp3_common cp3_parser)
cp3_set_warnings(cp3_sema)

# -----------------------------------------------------------------------------
# cp3_codegen — checked AST -> generated C++ (+ source map).  STUB.
# -----------------------------------------------------------------------------
add_library(cp3_codegen STATIC "${CP3_SRC_DIR}/codegen/codegen.cpp")
target_include_directories(cp3_codegen PUBLIC "${CP3_SRC_DIR}/codegen/include")
target_link_libraries(cp3_codegen PUBLIC cp3_common cp3_sema)
cp3_set_warnings(cp3_codegen)

# -----------------------------------------------------------------------------
# cp3_runtime — placeholder for generated-code runtime support (Promise,
# deep-copy helpers, event loop bridge).  Intentionally minimal for now.
# -----------------------------------------------------------------------------
add_library(cp3_runtime STATIC "${CP3_SRC_DIR}/runtime/runtime.cpp")
target_include_directories(cp3_runtime PUBLIC "${CP3_SRC_DIR}/runtime/include")
target_link_libraries(cp3_runtime PUBLIC cp3_common)
cp3_set_warnings(cp3_runtime)

# -----------------------------------------------------------------------------
# cp3_cli — command surface (init/run/check/dev/build/fmt/...).  STUB.
# -----------------------------------------------------------------------------
add_library(cp3_cli STATIC "${CP3_SRC_DIR}/cli/cli.cpp")
target_include_directories(cp3_cli PUBLIC "${CP3_SRC_DIR}/cli/include")
target_link_libraries(cp3_cli PUBLIC
    cp3_common cp3_lexer cp3_parser cp3_sema cp3_codegen)
cp3_set_warnings(cp3_cli)

# -----------------------------------------------------------------------------
# cp3c executable
# -----------------------------------------------------------------------------
add_executable(cp3c "${CP3_SRC_DIR}/main.cpp")
target_link_libraries(cp3c PRIVATE cp3_cli)
cp3_set_warnings(cp3c)

# -----------------------------------------------------------------------------
# Test harness (Python).  Optional: only wired up if Python is available.
# -----------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(cp3c-tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/runner/run_tests.py"
                --cp3c "$<TARGET_FILE:cp3c>"
                --cases "${CMAKE_CURRENT_SOURCE_DIR}/tests/cases"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Running cp3c test harness"
    )
    add_dependencies(cp3c-tests cp3c)
else()
    message(STATUS "Python3 not found — 'cp3c-tests' target disabled (run tests/runner/run_tests.py manually).")
endif()
