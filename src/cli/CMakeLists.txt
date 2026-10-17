# =============================================================================
# cp3_cli — command surface (init/run/check/dev/build/fmt/clean/exec/version).
#
# OWNERSHIP: CLI/pipeline owner. All rendering of diagnostics happens here, and
# the exit-code contract (ARCHITECTURE.md §4) lives here.
#
# ADDING A SOURCE FILE: append one line to `cp3_cli_sources` below. Substantial
# logic currently declared in `include/private/*.hpp` moves into `.cpp` files
# (STYLE.md §8) — headers declare, `.cpp` defines.
# =============================================================================

set(cp3_cli_sources
    src/cli.cpp
    src/util.cpp
    src/cache.cpp
    src/toolchain.cpp
    src/diagnostics.cpp
    src/pipeline.cpp
    src/commands_common.cpp
    src/commands_check.cpp
    src/commands_run.cpp
    src/commands_build.cpp
    src/commands_dev.cpp
    src/commands_fmt.cpp
    src/commands_clean.cpp
    src/commands_init.cpp
    src/commands_exec.cpp
)

add_library(cp3_cli STATIC ${cp3_cli_sources})
target_include_directories(cp3_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_cli PUBLIC
    cp3_common cp3_lexer cp3_parser cp3_modules cp3_sema cp3_codegen)
cp3_set_warnings(cp3_cli)
