# =============================================================================
# cp3_modules — module discovery, §18 prelude injection, import resolution.
#
# OWNERSHIP: modules owner. The public boundary is `include/cp3/modules.hpp`
# (`cp3::ModuleLoader`); everything under `src/` is private.
#
# Dependency edge (see src/CMakeLists.txt): cp3_modules -> cp3_parser
# (-> cp3_lexer -> cp3_common). It must NOT link sema/codegen: the loader only
# lexes, parses and resolves; type checking stays in the CLI pipeline.
#
# ADDING A SOURCE FILE: append one line to `cp3_modules_sources` below. Policy:
# headers declare, `.cpp` defines (STYLE.md §3), one concern per file.
# =============================================================================

set(cp3_modules_sources
    src/module_loader.cpp
    src/unit_loader.cpp
    src/prelude_scan.cpp
    src/import_resolver.cpp
    src/export_surface.cpp
    src/std_registry.cpp
    src/specifier.cpp
    src/file_paths.cpp
)

add_library(cp3_modules STATIC ${cp3_modules_sources})
target_include_directories(cp3_modules PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_modules PUBLIC cp3_common cp3_lexer cp3_parser)
cp3_set_warnings(cp3_modules)

# -----------------------------------------------------------------------------
# Unit drivers (STYLE.md §7). Opt-in so the default library build stays lean:
#   cmake -B build/scratch/modules -S . -DCP3_MODULES_TESTS=ON
# -----------------------------------------------------------------------------
option(CP3_MODULES_TESTS "Build the cp3_modules unit drivers" OFF)
if(CP3_MODULES_TESTS)
    add_subdirectory(tests)
endif()
