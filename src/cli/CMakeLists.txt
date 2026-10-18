# =============================================================================
# cp3_cli - command surface (init/run/check/dev/build/fmt/clean/exec/version).
#
# OWNERSHIP: CLI/pipeline owner. All rendering of diagnostics happens here, and
# the exit-code contract (ARCHITECTURE.md §4) lives here.
#
# ADDING A SOURCE FILE: append one line to `cp3_cli_sources` below. The units
# live under `src/{commands,pkg,archive,toolchain}/`; private headers live in
# `src/detail/` (STYLE.md §8) - headers declare, `.cpp` defines.
# =============================================================================

set(cp3_cli_sources
    src/cli.cpp
    src/toolchain/util.cpp
    src/pkg/cache.cpp
    src/toolchain/toolchain.cpp
    src/toolchain/diagnostics.cpp
    src/toolchain/pipeline.cpp
    src/commands/common.cpp
    src/commands/check.cpp
    src/commands/run.cpp
    src/commands/build.cpp
    src/commands/dev.cpp
    src/commands/fmt.cpp
    src/commands/clean.cpp
    src/commands/init.cpp
    src/commands/exec.cpp
    src/commands/pack.cpp
    src/commands/install.cpp
    src/pkg/package_json.cpp
    src/pkg/signatures.cpp
    src/pkg/lockfile.cpp
    src/pkg/scripts.cpp
    src/pkg/json.cpp
    src/pkg/checksum.cpp
    src/archive/inflate.cpp
    src/archive/deflate.cpp
    src/archive/gzip.cpp
    src/archive/tar.cpp
    src/pkg/cp3pak.cpp
    src/pkg/semver.cpp
)

add_library(cp3_cli STATIC ${cp3_cli_sources})
target_include_directories(cp3_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from every unit subdirectory.
target_include_directories(cp3_cli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cp3_cli PUBLIC
    cp3_common cp3_lexer cp3_parser cp3_modules cp3_sema cp3_codegen)
cp3_set_warnings(cp3_cli)
