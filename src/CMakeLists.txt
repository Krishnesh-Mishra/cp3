# =============================================================================
# src/CMakeLists.txt — module wiring for the cp3c compiler.
#
# Layout rule (STYLE.md §3): every module owns its own build file —
# `src/<module>/CMakeLists.txt` declares that module's target, source list,
# include directories and links. Adding a `.cpp` to a module means editing ONLY
# that module's CMakeLists.txt: never this file, never the repository root.
#
# This file does two things:
#   1. `add_subdirectory`s each module in *dependency order* (a module may only
#      link a module that has already been added).
#   2. Defines the `cp3c` executable from `src/main.cpp`.
#
# Dependency edges (do not introduce cycles):
#   cp3c -> cp3_cli -> { common, lexer, parser, modules, sema, codegen }
#                       cp3_parser  -> cp3_lexer -> cp3_common
#                       cp3_modules -> cp3_parser  (prelude/import resolution)
#                       cp3_sema    -> cp3_parser
#                       cp3_codegen -> cp3_sema
#                       cp3_runtime -> cp3_common   (not yet linked into cp3c)
# =============================================================================

add_subdirectory(common)   # frozen interfaces — links nothing
add_subdirectory(lexer)    # -> cp3_common
add_subdirectory(parser)   # -> cp3_lexer  (-> cp3_common)
add_subdirectory(modules)  # -> cp3_parser (prelude injection, import resolution)
add_subdirectory(sema)     # -> cp3_parser (-> cp3_lexer -> cp3_common)
add_subdirectory(codegen)  # -> cp3_sema
add_subdirectory(runtime)  # -> cp3_common (kept out of cp3c for now)
add_subdirectory(cli)      # -> common, lexer, parser, modules, sema, codegen

# -----------------------------------------------------------------------------
# cp3c executable
# Trivial by design: all behavior lives in cp3::runCli (src/cli/).
# -----------------------------------------------------------------------------
add_executable(cp3c "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_link_libraries(cp3c PRIVATE cp3_cli)
cp3_set_warnings(cp3c)
