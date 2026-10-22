# =============================================================================
# cp3_common - FROZEN shared interfaces (source/positions, tokens, AST, visitor,
# diagnostics, diagnostic codes, module graph, options, version, value types).
#
# Every other module links against this library; it links nothing itself.
# OWNERSHIP: scaffolding/architecture (the implementations behind the frozen
# `include/cp3/**` headers).
#
# ADDING A SOURCE FILE: append one line to `cp3_common_sources` below. That is
# the only file that needs to change (STYLE.md §3). Never touch the repository
# root for a module's sources.
# =============================================================================

set(cp3_common_sources
    src/source.cpp
    src/diagnostic.cpp
    src/token.cpp
    src/ast.cpp
    src/module.cpp
    src/types.cpp
    src/native_types.cpp
    src/native_bindings.cpp
    src/native_bindings_read.cpp
    src/detail/native_json.cpp
    src/detail/native_fields.cpp
    src/checksum.cpp
    src/options.cpp
    src/suggest.cpp
    src/version.cpp
)

add_library(cp3_common STATIC ${cp3_common_sources})
target_include_directories(cp3_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Private headers resolve as `detail/...` from the units under src/.
target_include_directories(cp3_common PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
cp3_set_warnings(cp3_common)
