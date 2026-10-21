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
#
# THIS FILE DELIBERATELY KNOWS NOTHING ABOUT MODULE SOURCES. Every module owns
# its target, source list, include dirs and links in its own build file —
# `src/<module>/CMakeLists.txt` — and `src/CMakeLists.txt` wires the modules
# together and defines the `cp3c` executable. Adding a `.cpp` to a module means
# editing ONLY that module's CMakeLists.txt (STYLE.md §3): this keeps parallel
# module authors from colliding in a shared root file.
#
# What stays at the root, and why:
#   * toolchain floor / project identity (cmake_minimum_required, project),
#   * C++20 settings and output directories (global for every target),
#   * the single `cp3_set_warnings` helper,
#   * `find_package(Python3)` for the repository-level Python tooling targets,
#   * `add_subdirectory(src)`,
#   * the two top-level convenience targets (`cp3c-tests`, `lint`).
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
# be blocked by unrelated warnings). Defined here so every module's own
# CMakeLists.txt can call it.
function(cp3_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Python tooling (test harness + layout linter). Optional: the repository-level
# convenience targets degrade gracefully when no interpreter is found.
# -----------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(CP3_PYTHON "${Python3_EXECUTABLE}")
else()
    set(CP3_PYTHON "python")
    message(STATUS "Python3 not found — Python tooling targets will try '${CP3_PYTHON}' from PATH.")
endif()

# -----------------------------------------------------------------------------
# Modules + executable. All library/executable definitions live under src/.
# -----------------------------------------------------------------------------
add_subdirectory(src)

# -----------------------------------------------------------------------------
# Test harness (Python).  Optional: only wired up if Python is available.
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# Developer lint path: tools/lint.py runs the STYLE.md layout checks plus the
# zero-placeholder gate. The layout pass is advisory by design: never in ALL,
# never depends on cp3c, and must never block any other target - a warning in
# one module must not stop another module's build (STYLE.md §9). The gate is
# hard by design: it exits 1 while a retired marker remains in src, tests, docs
# or packaging.
#
#   cmake --build <build> --target lint
#   cmake --build <build> --target lint-cp3002
# =============================================================================
add_custom_target(lint
    COMMAND "${CP3_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Running layout checks and the zero-placeholder gate"
)

add_custom_target(lint-cp3002
    COMMAND "${CP3_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_cp3002.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Checking for retired placeholder markers"
)
